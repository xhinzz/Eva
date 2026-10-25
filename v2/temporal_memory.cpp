#include "temporal_memory.hpp"

#include <algorithm>
#include <istream>
#include <ostream>
#include <unordered_set>

namespace eva2 {

TemporalMemory::TemporalMemory(TMConfig cfg, unsigned seed) : cfg_(cfg), rng_(seed) {
    const int n = cfg_.columns * cfg_.cells_per_column;
    cell_segments_.assign(n, {});
    is_active_.assign(n, 0);
    is_prev_active_.assign(n, 0);
    predictive_.assign(n, 0);
}

void TemporalMemory::reset() {
    active_cells_.clear();
    winner_cells_.clear();
    prev_active_.clear();
    prev_winner_.clear();
    std::fill(is_active_.begin(), is_active_.end(), 0);
    std::fill(is_prev_active_.begin(), is_prev_active_.end(), 0);
    std::fill(predictive_.begin(), predictive_.end(), 0);
    active_segments_.clear();
    matching_segments_.clear();
    for (auto& s : segments_) { s.active_count = 0; s.matching_count = 0; }
}

// Para cada segmento, conta quantas das suas sinapses apontam para células
// ativas AGORA. Isso define o que será previsto no próximo passo.
void TemporalMemory::compute_segment_activity() {
    active_segments_.clear();
    matching_segments_.clear();
    std::fill(predictive_.begin(), predictive_.end(), 0);
    // só segmentos que têm alguma sinapse para célula ativa precisam ser visitados
    std::unordered_set<int> touched;
    for (int c : active_cells_) (void)c;
    // percorre todos os segmentos (simples e suficiente para este tamanho)
    for (size_t i = 0; i < segments_.size(); ++i) {
        Segment& s = segments_[i];
        int act = 0, match = 0;
        for (const auto& sy : s.syn) {
            if (!is_active_[sy.cell]) continue;
            ++match;
            if (sy.perm >= cfg_.connected) ++act;
        }
        s.active_count = act;
        s.matching_count = match;
        if (act >= cfg_.activation_threshold) {
            active_segments_.push_back(static_cast<int>(i));
            predictive_[s.cell] = 1;
        }
        if (match >= cfg_.min_threshold) matching_segments_.push_back(static_cast<int>(i));
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
        if (is_prev_active_[sy.cell]) sy.perm += (reinforce ? cfg_.inc : -cfg_.predicted_decrement) * scale;
        else if (reinforce) sy.perm -= cfg_.dec * scale;
        sy.perm = std::clamp(sy.perm, 0.0f, 1.0f);
    }
    // sinapses mortas saem
    s.syn.erase(std::remove_if(s.syn.begin(), s.syn.end(), [](const Synapse& x) { return x.perm <= 0.0f; }),
                s.syn.end());
}

void TemporalMemory::grow_synapses(Segment& s, int n, std::mt19937& rng) {
    if (n <= 0 || prev_winner_.empty()) return;
    std::vector<int> candidates;
    for (int c : prev_winner_) {
        bool has = false;
        for (const auto& sy : s.syn) if (sy.cell == c) { has = true; break; }
        if (!has) candidates.push_back(c);
    }
    std::shuffle(candidates.begin(), candidates.end(), rng);
    for (int k = 0; k < n && k < static_cast<int>(candidates.size()); ++k) {
        if (static_cast<int>(s.syn.size()) >= cfg_.max_synapses_per_segment) break;
        s.syn.push_back({candidates[k], cfg_.initial_permanence});
    }
}

void TemporalMemory::step(const std::vector<int>& columns, bool learn, float dopamine) {
    // o passo anterior vira "anterior"
    prev_active_ = active_cells_;
    prev_winner_ = winner_cells_;
    std::swap(is_prev_active_, is_active_);
    std::fill(is_active_.begin(), is_active_.end(), 0);
    active_cells_.clear();
    winner_cells_.clear();

    const float scale = std::clamp(dopamine, 0.2f, 3.0f);
    int unpredicted = 0;
    std::vector<uint8_t> col_active(cfg_.columns, 0);
    for (int c : columns) col_active[c] = 1;

    for (int col : columns) {
        // células desta coluna que estavam previstas
        std::vector<int> predicted;
        for (int i = 0; i < cfg_.cells_per_column; ++i) {
            const int cell = cell_index(col, i);
            if (predictive_[cell]) predicted.push_back(cell);
        }
        if (!predicted.empty()) {
            // previsão confirmada: só as células previstas disparam (contexto preservado)
            for (int cell : predicted) {
                active_cells_.push_back(cell);
                winner_cells_.push_back(cell);
                is_active_[cell] = 1;
                if (learn)
                    for (int sidx : cell_segments_[cell]) {
                        Segment& s = segments_[sidx];
                        if (s.active_count >= cfg_.activation_threshold) {
                            adapt_segment(s, true, scale);
                            grow_synapses(s, cfg_.max_new_synapses - s.active_count, rng_);
                        }
                    }
            }
        } else {
            // surpresa: a coluna inteira dispara ("burst") e uma célula aprende o contexto
            ++unpredicted;
            for (int i = 0; i < cfg_.cells_per_column; ++i) {
                const int cell = cell_index(col, i);
                active_cells_.push_back(cell);
                is_active_[cell] = 1;
            }
            int winner;
            const int best = best_matching_segment(col);
            if (best >= 0) {
                winner = segments_[best].cell;
                if (learn) {
                    adapt_segment(segments_[best], true, scale);
                    grow_synapses(segments_[best], cfg_.max_new_synapses - segments_[best].matching_count, rng_);
                }
            } else {
                winner = least_used_cell(col, rng_);
                if (learn && !prev_winner_.empty() &&
                    static_cast<int>(cell_segments_[winner].size()) < cfg_.max_segments_per_cell) {
                    segments_.push_back({winner, {}, 0, 0});
                    cell_segments_[winner].push_back(static_cast<int>(segments_.size()) - 1);
                    grow_synapses(segments_.back(), cfg_.max_new_synapses, rng_);
                }
            }
            winner_cells_.push_back(winner);
        }
    }

    // segmentos que previram e a coluna NÃO veio: enfraquecem (punição à previsão errada)
    if (learn)
        for (int sidx : matching_segments_) {
            Segment& s = segments_[sidx];
            if (!col_active[column_of(s.cell)]) adapt_segment(s, false, scale);
        }

    last_surprise_ = columns.empty() ? 0.0f : static_cast<float>(unpredicted) / columns.size();
    compute_segment_activity();
}

std::vector<int> TemporalMemory::predicted_columns() const {
    std::vector<int> cols;
    for (size_t cell = 0; cell < predictive_.size(); ++cell)
        if (predictive_[cell]) {
            const int col = column_of(static_cast<int>(cell));
            if (cols.empty() || cols.back() != col) cols.push_back(col);
        }
    return cols;  // já sai ordenado porque as células estão em ordem de coluna
}

// ---------------------------------------------------------------- persistência

void TemporalMemory::save(std::ostream& out) const {
    const uint32_t n = static_cast<uint32_t>(segments_.size());
    out.write(reinterpret_cast<const char*>(&n), sizeof n);
    for (const auto& s : segments_) {
        const uint32_t m = static_cast<uint32_t>(s.syn.size());
        out.write(reinterpret_cast<const char*>(&s.cell), sizeof s.cell);
        out.write(reinterpret_cast<const char*>(&m), sizeof m);
        for (const auto& sy : s.syn) {
            out.write(reinterpret_cast<const char*>(&sy.cell), sizeof sy.cell);
            out.write(reinterpret_cast<const char*>(&sy.perm), sizeof sy.perm);
        }
    }
}

bool TemporalMemory::load(std::istream& in) {
    uint32_t n = 0;
    if (!in.read(reinterpret_cast<char*>(&n), sizeof n)) return false;
    segments_.clear();
    for (auto& v : cell_segments_) v.clear();
    for (uint32_t i = 0; i < n; ++i) {
        Segment s;
        uint32_t m = 0;
        in.read(reinterpret_cast<char*>(&s.cell), sizeof s.cell);
        in.read(reinterpret_cast<char*>(&m), sizeof m);
        s.syn.resize(m);
        for (auto& sy : s.syn) {
            in.read(reinterpret_cast<char*>(&sy.cell), sizeof sy.cell);
            in.read(reinterpret_cast<char*>(&sy.perm), sizeof sy.perm);
        }
        if (!in) return false;
        cell_segments_[s.cell].push_back(static_cast<int>(segments_.size()));
        segments_.push_back(std::move(s));
    }
    reset();
    return true;
}

}  // namespace eva2
