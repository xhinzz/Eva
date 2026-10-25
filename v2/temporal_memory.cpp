#include "temporal_memory.hpp"

#include <algorithm>
#include <cmath>
#include <istream>
#include <ostream>

namespace eva2 {

TemporalMemory::TemporalMemory(TMConfig cfg, unsigned seed) : cfg_(cfg), rng_(seed) {
    const int n = cfg_.columns * cfg_.cells_per_column;
    cell_segments_.assign(n, {});
    presyn_.assign(n, {});
    is_active_.assign(n, 0);
    is_prev_active_.assign(n, 0);
    predictive_.assign(n, 0);
    sure_predictive_.assign(n, 0);
    is_burst_.assign(n, 0);
    col_active_.assign(cfg_.columns, 0);
    connected_q_ = static_cast<uint8_t>(std::lround(cfg_.connected * 255.0f));
}

uint8_t TemporalMemory::to_perm(float p) {
    p = std::clamp(p, 0.0f, 1.0f) * 255.0f;
    const float lo = std::floor(p);
    return static_cast<uint8_t>(lo + (unif_(rng_) < p - lo ? 1.0f : 0.0f));
}

void TemporalMemory::reset() {
    for (int c : active_cells_) is_active_[c] = 0;
    for (int c : prev_active_) is_prev_active_[c] = 0;
    active_cells_.clear();
    winner_cells_.clear();
    prev_active_.clear();
    prev_winner_.clear();
    for (int c : predictive_cells_) predictive_[c] = 0;
    predictive_cells_.clear();
    for (int c : sure_cells_) sure_predictive_[c] = 0;
    sure_cells_.clear();
    std::fill(is_burst_.begin(), is_burst_.end(), 0);
    active_segments_.clear();
    matching_segments_.clear();
    for (int s : touched_segments_) { segments_[s].active_count = 0; segments_[s].matching_count = 0; segments_[s].sure_count = 0; }
    touched_segments_.clear();
}

// Para cada segmento que recebe sinapse de alguma célula ativa AGORA, conta
// quantas sinapses ativas ele tem. Isso define o que será previsto no próximo
// passo. Os outros segmentos (a imensa maioria) nem são visitados.
void TemporalMemory::compute_segment_activity() {
    for (int s : touched_segments_) { segments_[s].active_count = 0; segments_[s].matching_count = 0; segments_[s].sure_count = 0; }
    touched_segments_.clear();
    active_segments_.clear();
    matching_segments_.clear();
    for (int c : predictive_cells_) predictive_[c] = 0;
    predictive_cells_.clear();
    for (int c : sure_cells_) sure_predictive_[c] = 0;
    sure_cells_.clear();

    if (seg_stamp_.size() < segments_.size()) seg_stamp_.resize(segments_.size(), 0);
    if (++stamp_ == 0) { std::fill(seg_stamp_.begin(), seg_stamp_.end(), 0); stamp_ = 1; }
    for (int cell : active_cells_) {
        const bool sure = !is_burst_[cell];
        auto& pre = presyn_[cell];
        for (size_t i = 0; i < pre.size();) {
            const int sidx = static_cast<int>(pre[i] >> 6);
            const int slot = static_cast<int>(pre[i] & 63);
            Segment& s = segments_[sidx];
            if (slot >= static_cast<int>(s.syn.size()) || s.syn[slot].cell() != cell || s.syn[slot].perm() == 0) {
                pre[i] = pre.back(); pre.pop_back();  // entrada velha (a sinapse morreu ou a vaga foi reusada)
                continue;
            }
            if (seg_stamp_[sidx] != stamp_) {
                seg_stamp_[sidx] = stamp_;
                s.active_count = 0; s.matching_count = 0; s.sure_count = 0;
                touched_segments_.push_back(sidx);
            }
            ++s.matching_count;
            if (s.syn[slot].perm() >= connected_q_) { ++s.active_count; s.sure_count += sure; }
            ++i;
        }
    }
    for (int sidx : touched_segments_) {
        const Segment& s = segments_[sidx];
        if (s.active_count >= cfg_.activation_threshold) {
            active_segments_.push_back(sidx);
            if (!predictive_[s.cell]) { predictive_[s.cell] = 1; predictive_cells_.push_back(s.cell); }
            if (s.sure_count >= cfg_.activation_threshold && !sure_predictive_[s.cell]) { sure_predictive_[s.cell] = 1; sure_cells_.push_back(s.cell); }
        }
        if (s.matching_count >= cfg_.min_threshold) matching_segments_.push_back(sidx);
    }
}

int TemporalMemory::least_used_cell(int column, std::mt19937& rng) const {
    int best = -1, best_n = 1 << 30;
    std::vector<int> ties;
    for (int i = 0; i < cfg_.cells_per_column; ++i) {
        const int cell = cell_index(column, i);
        const int n = static_cast<int>(cell_segments_[cell].size());
        if (n < best_n) { best_n = n; ties.clear(); ties.push_back(cell); best = cell; }
        else if (n == best_n) ties.push_back(cell);
    }
    if (ties.size() > 1) {
        std::uniform_int_distribution<size_t> pick(0, ties.size() - 1);
        best = ties[pick(rng)];
    }
    return best;
}

int TemporalMemory::best_matching_segment(int column) const {
    int best = -1, best_match = -1;
    for (int i = 0; i < cfg_.cells_per_column; ++i)
        for (int sidx : cell_segments_[cell_index(column, i)]) {
            const Segment& s = segments_[sidx];
            if (s.matching_count >= cfg_.min_threshold && s.matching_count > best_match) {
                best_match = s.matching_count;
                best = sidx;
            }
        }
    return best;
}

void TemporalMemory::adapt_segment(Segment& s, bool reinforce, float scale) {
    for (auto& sy : s.syn) {
        if (sy.perm() == 0) continue;  // lápide
        float p = sy.perm() / 255.0f;
        if (is_prev_active_[sy.cell()]) p += (reinforce ? cfg_.inc : -cfg_.predicted_decrement) * scale;
        else if (reinforce) p -= cfg_.dec * scale;
        else continue;
        sy.set_perm(to_perm(p));  // se chegar a 0, vira lápide; a vaga será reaproveitada
    }
}

int TemporalMemory::live_synapses(const Segment& s) const {
    int n = 0;
    for (const auto& sy : s.syn) n += sy.perm() > 0;
    return n;
}

void TemporalMemory::grow_synapses(Segment& s, int seg_index, int n, std::mt19937& rng) {
    if (n <= 0 || prev_winner_.empty()) return;
    std::vector<int> candidates;
    for (int c : prev_winner_) {
        bool has = false;
        for (const auto& sy : s.syn) if (sy.perm() > 0 && sy.cell() == c) { has = true; break; }
        if (!has) candidates.push_back(c);
    }
    std::shuffle(candidates.begin(), candidates.end(), rng);
    const uint8_t init = std::max<uint8_t>(to_perm(cfg_.initial_permanence), connected_q_);
    const int maxs = std::min(64, cfg_.max_synapses_per_segment);
    size_t tomb = 0;
    for (int k = 0; k < n && k < static_cast<int>(candidates.size()); ++k) {
        // vaga: uma lápide, ou uma nova no fim
        while (tomb < s.syn.size() && s.syn[tomb].perm() != 0) ++tomb;
        int slot;
        if (tomb < s.syn.size()) slot = static_cast<int>(tomb++);
        else if (static_cast<int>(s.syn.size()) < maxs) { s.syn.push_back(Synapse()); slot = static_cast<int>(s.syn.size()) - 1; tomb = s.syn.size(); }
        else break;
        const bool same_cell_again = s.syn[slot].cell() == candidates[k];
        s.syn[slot] = Synapse(candidates[k], init);
        const uint32_t addr = (static_cast<uint32_t>(seg_index) << 6) | static_cast<uint32_t>(slot);
        auto& pre = presyn_[candidates[k]];
        // vaga reusada pela mesma célula: o endereço pode já estar no índice (não duplicar)
        if (!same_cell_again || std::find(pre.begin(), pre.end(), addr) == pre.end()) pre.push_back(addr);
    }
}

void TemporalMemory::step(const std::vector<int>& columns, bool learn, float dopamine) {
    // o passo anterior vira "anterior"
    for (int c : prev_active_) is_prev_active_[c] = 0;
    prev_active_.swap(active_cells_);
    prev_winner_.swap(winner_cells_);
    for (int c : prev_active_) { is_prev_active_[c] = 1; is_active_[c] = 0; is_burst_[c] = 0; }
    active_cells_.clear();
    winner_cells_.clear();

    const float scale = std::clamp(dopamine, 0.2f, 3.0f);
    const bool fresh = cfg_.fresh_context_after_burst && last_surprise_ > 0.5f && !prev_winner_.empty();
    int unpredicted = 0;
    for (int c : columns) col_active_[c] = 1;

    for (int col : columns) {
        // células desta coluna que estavam previstas
        bool any = false;
        for (int i = 0; i < cfg_.cells_per_column; ++i) {
            const int cell = cell_index(col, i);
            if (!predictive_[cell]) continue;
            // previsão confirmada: só as células previstas disparam (contexto preservado)
            any = true;
            active_cells_.push_back(cell);
            winner_cells_.push_back(cell);
            is_active_[cell] = 1;
            if (learn)
                for (int sidx : cell_segments_[cell]) {
                    Segment& s = segments_[sidx];
                    if (s.active_count >= cfg_.activation_threshold) {
                        adapt_segment(s, true, scale);
                        grow_synapses(s, sidx, cfg_.max_new_synapses - s.active_count, rng_);
                    }
                }
        }
        if (any) continue;
        // surpresa: a coluna inteira dispara ("burst") e uma célula aprende o contexto
        ++unpredicted;
        for (int i = 0; i < cfg_.cells_per_column; ++i) {
            const int cell = cell_index(col, i);
            active_cells_.push_back(cell);
            is_active_[cell] = 1;
            is_burst_[cell] = 1;
        }
        int winner;
        const int best = fresh ? -1 : best_matching_segment(col);
        if (best >= 0) {
            winner = segments_[best].cell;
            if (learn) {
                adapt_segment(segments_[best], true, scale);
                grow_synapses(segments_[best], best, cfg_.max_new_synapses - segments_[best].matching_count, rng_);
            }
        } else {
            winner = least_used_cell(col, rng_);
            if (learn && !prev_winner_.empty() &&
                static_cast<int>(cell_segments_[winner].size()) < cfg_.max_segments_per_cell) {
                segments_.push_back({winner, {}, 0, 0});
                const int sidx = static_cast<int>(segments_.size()) - 1;
                cell_segments_[winner].push_back(sidx);
                grow_synapses(segments_.back(), sidx, cfg_.max_new_synapses, rng_);
            }
        }
        winner_cells_.push_back(winner);
    }

    // segmentos que previram e a coluna NÃO veio: enfraquecem (punição à previsão errada)
    if (learn)
        for (int sidx : matching_segments_) {
            Segment& s = segments_[sidx];
            if (!col_active_[column_of(s.cell)]) adapt_segment(s, false, scale);
        }
    for (int c : columns) col_active_[c] = 0;

    last_surprise_ = columns.empty() ? 0.0f : static_cast<float>(unpredicted) / columns.size();
    compute_segment_activity();
}

std::vector<int> TemporalMemory::predicted_columns(bool sure) const {
    std::vector<int> cols;
    const auto& src = sure ? sure_cells_ : predictive_cells_;
    cols.reserve(src.size());
    for (int cell : src) cols.push_back(column_of(cell));
    std::sort(cols.begin(), cols.end());
    cols.erase(std::unique(cols.begin(), cols.end()), cols.end());
    return cols;
}

int TemporalMemory::predicted_cells_in(const std::vector<int>& columns) const {
    int n = 0;
    for (int col : columns)
        for (int i = 0; i < cfg_.cells_per_column; ++i) n += predictive_[cell_index(col, i)];
    return n;
}

long long TemporalMemory::n_synapses() const {
    long long n = 0;
    for (const auto& s : segments_) n += live_synapses(s);
    return n;
}

size_t TemporalMemory::memory_bytes() const {
    size_t b = segments_.size() * sizeof(Segment);
    for (const auto& s : segments_) b += s.syn.capacity() * sizeof(Synapse);
    for (const auto& v : cell_segments_) b += v.capacity() * sizeof(int);
    for (const auto& v : presyn_) b += v.capacity() * sizeof(uint32_t);
    b += cell_segments_.size() * 2 * sizeof(std::vector<int>) + is_active_.size() * 3;
    return b;
}

// ---------------------------------------------------------------- persistência

void TemporalMemory::save(std::ostream& out) const {
    const uint32_t n = static_cast<uint32_t>(segments_.size());
    out.write(reinterpret_cast<const char*>(&n), sizeof n);
    for (const auto& s : segments_) {
        std::vector<Synapse> live;
        for (const auto& sy : s.syn) if (sy.perm() > 0) live.push_back(sy);
        const uint32_t m = static_cast<uint32_t>(live.size());
        out.write(reinterpret_cast<const char*>(&s.cell), sizeof s.cell);
        out.write(reinterpret_cast<const char*>(&m), sizeof m);
        out.write(reinterpret_cast<const char*>(live.data()), m * sizeof(Synapse));
    }
}

bool TemporalMemory::load(std::istream& in) {
    uint32_t n = 0;
    if (!in.read(reinterpret_cast<char*>(&n), sizeof n)) return false;
    segments_.clear();
    for (auto& v : cell_segments_) v.clear();
    for (auto& v : presyn_) v.clear();
    for (uint32_t i = 0; i < n; ++i) {
        Segment s;
        uint32_t m = 0;
        in.read(reinterpret_cast<char*>(&s.cell), sizeof s.cell);
        in.read(reinterpret_cast<char*>(&m), sizeof m);
        s.syn.resize(m);
        in.read(reinterpret_cast<char*>(s.syn.data()), m * sizeof(Synapse));
        if (!in || s.cell < 0 || s.cell >= static_cast<int>(cell_segments_.size()) || m > 64) return false;
        const int sidx = static_cast<int>(segments_.size());
        cell_segments_[s.cell].push_back(sidx);
        for (size_t slot = 0; slot < s.syn.size(); ++slot) {
            const int pc = s.syn[slot].cell();
            if (pc >= static_cast<int>(presyn_.size())) return false;
            presyn_[pc].push_back((static_cast<uint32_t>(sidx) << 6) | static_cast<uint32_t>(slot));
        }
        segments_.push_back(std::move(s));
    }
    seg_stamp_.assign(segments_.size(), 0);
    reset();
    return true;
}

}  // namespace eva2
