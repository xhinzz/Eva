// Eva 2 - categorias de palavras que emergem do uso (o começo da gramática).
//
// Ninguém diz a uma criança o que é substantivo. Ela percebe que "gato" e
// "cachorro" aparecem nos mesmos lugares: depois de "o", antes de "é",
// antes de "come". Palavras que vivem nos mesmos contextos são da mesma
// espécie (Redington, Chater e Finch 1998; Mintz 2003, "frames frequentes").
// E ouve as terminações: "bonita" e "casa" terminam igual, como "bonito" e
// "gato" (o gênero no português se ouve).
//
// Como funciona aqui, sem rótulo e sem professor:
//  1. Para cada palavra, contam-se os vizinhos imediatos (à esquerda e à
//     direita), mas só os vizinhos frequentes contam como contexto ("o", "a",
//     "é", "de", início e fim de frase), como fazem os modelos de aquisição.
//  2. Pesa-se pela surpresa (PPMI): um vizinho que vem com todo mundo informa pouco.
//  3. O perfil passa por uma projeção aleatória esparsa e uma competição
//     k-vencedores, como o cérebro da mosca faz com cheiros (Dasgupta et al.
//     2017): perfis parecidos dão códigos parecidos. Esse código são as
//     "colunas de categoria" da palavra, somadas às colunas de identidade dela.
//  4. O córtex de sequências não sabe nada disso. Mas quando uma palavra
//     aparece num contexto novo, suas colunas explodem, e as colunas de
//     categoria carregam o que as palavras irmãs já aprenderam: "a girafa",
//     nunca ouvida como sujeito, prevê "é", "come", "está", porque "gato" e
//     "vaca" preveem. É generalização por substituição.
//
// Os códigos mudam no sono (quando o perfil mudou o bastante) e o sono
// regrava as conversas com os códigos novos: consolidação.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eva2 {

struct CategoryConfig {
    int columns = 2048;      // colunas de categoria (espaço separado das de identidade)
    int active = 40;         // colunas de categoria por palavra
    int fanout = 64;         // cada traço de contexto projeta para tantas colunas
    int context_words = 120; // só os vizinhos mais frequentes servem de contexto
    float min_count = 4.0f;  // ocorrências antes de a palavra ganhar categoria
    float keep_overlap = 0.8f;  // no sono: mantém o código se o novo se sobrepõe tanto
    float morph_weight = 0.8f;  // quanto a terminação pesa frente ao contexto
};

class WordCategories {
public:
    explicit WordCategories(CategoryConfig cfg = {}) : cfg_(cfg) {}

    // uma frase ouvida (só as palavras; -1 = início, -2 = fim são implícitos)
    void observe(const std::vector<int>& ids, const std::vector<std::string>& words) {
        const int n = static_cast<int>(ids.size());
        for (int i = 0; i < n; ++i) {
            const int w = ids[i];
            if (w >= static_cast<int>(count_.size())) grow(w + 1);
            count_[w] += 1.0f;
            const int left = i > 0 ? ids[i - 1] : kBegin;
            const int right = i + 1 < n ? ids[i + 1] : kEnd;
            ctx_[w][key(left, 0)] += 1.0f;
            ctx_[w][key(right, 1)] += 1.0f;
            if (word_.size() <= static_cast<size_t>(w)) word_.resize(w + 1);
            if (word_[w].empty()) word_[w] = words[i];
        }
        total_ += n;
    }

    float count(int w) const { return w >= 0 && w < static_cast<int>(count_.size()) ? count_[w] : 0.0f; }
    bool has_code(int w) const { return w >= 0 && w < static_cast<int>(code_.size()) && !code_[w].empty(); }
    const std::vector<int>& code(int w) const { static const std::vector<int> none; return w < static_cast<int>(code_.size()) ? code_[w] : none; }

    // calcula (ou recalcula) o código de categoria de uma palavra. Devolve true se mudou.
    bool update(int w, bool force = false) {
        if (count(w) < cfg_.min_count) return false;
        if (frequent_dirty_) refresh_frequent();
        std::vector<int> fresh = compute(w);
        if (fresh.empty()) return false;
        auto& old = code_[w];
        if (!force && !old.empty() && overlap(old, fresh) >= cfg_.keep_overlap * cfg_.active) return false;
        old = std::move(fresh);
        return true;
    }
    // no sono: todas as palavras com evidência suficiente; devolve quantas mudaram
    int update_all() {
        frequent_dirty_ = true;
        int changed = 0;
        for (int w = 0; w < static_cast<int>(count_.size()); ++w) changed += update(w);
        return changed;
    }
    void mark_dirty() { frequent_dirty_ = true; }

    // semelhança entre dois códigos (0..1)
    float similarity(int a, int b) const {
        if (!has_code(a) || !has_code(b)) return 0.0f;
        return static_cast<float>(overlap(code_[a], code_[b])) / cfg_.active;
    }
    static int overlap(const std::vector<int>& a, const std::vector<int>& b) {
        int n = 0; size_t i = 0, j = 0;
        while (i < a.size() && j < b.size()) {
            if (a[i] == b[j]) { ++n; ++i; ++j; } else if (a[i] < b[j]) ++i; else ++j;
        }
        return n;
    }
    const CategoryConfig& config() const { return cfg_; }
    size_t words_with_code() const { size_t n = 0; for (const auto& c : code_) n += !c.empty(); return n; }

    template <class Out> void save(Out& out) const {
        const uint32_t n = static_cast<uint32_t>(count_.size());
        wr(out, n); wr(out, total_);
        for (uint32_t w = 0; w < n; ++w) {
            wr(out, count_[w]);
            const uint32_t m = static_cast<uint32_t>(ctx_[w].size());
            wr(out, m);
            for (const auto& [k, v] : ctx_[w]) { wr(out, k); wr(out, v); }
            const uint32_t c = static_cast<uint32_t>(code_[w].size());
            wr(out, c);
            for (int x : code_[w]) { const int32_t y = x; wr(out, y); }
            const std::string& s = w < word_.size() ? word_[w] : std::string();
            const uint32_t len = static_cast<uint32_t>(s.size());
            wr(out, len); out.write(s.data(), len);
        }
    }
    template <class In> bool load(In& in) {
        uint32_t n = 0;
        if (!rd(in, n)) return false;
        rd(in, total_);
        grow(n);
        for (uint32_t w = 0; w < n; ++w) {
            rd(in, count_[w]);
            uint32_t m = 0; rd(in, m);
            for (uint32_t i = 0; i < m; ++i) { int64_t k; float v; rd(in, k); rd(in, v); ctx_[w][k] = v; }
            uint32_t c = 0; rd(in, c);
            code_[w].resize(c);
            for (auto& x : code_[w]) { int32_t y; rd(in, y); x = y; }
            uint32_t len = 0; rd(in, len);
            word_[w].assign(len, ' '); in.read(word_[w].data(), len);
        }
        frequent_dirty_ = true;
        return static_cast<bool>(in);
    }

private:
    static constexpr int kBegin = -1, kEnd = -2;
    static int64_t key(int word, int side) { return (static_cast<int64_t>(word) + 8) * 4 + side; }

    void grow(size_t n) {
        if (count_.size() >= n) return;
        count_.resize(n, 0.0f); ctx_.resize(n); code_.resize(n); word_.resize(n);
    }

    void refresh_frequent() {
        std::vector<std::pair<float, int>> by;
        for (int w = 0; w < static_cast<int>(count_.size()); ++w) if (count_[w] > 0) by.push_back({count_[w], w});
        std::sort(by.rbegin(), by.rend());
        is_frequent_.assign(count_.size() + 8, 0);
        is_frequent_[0] = is_frequent_[1] = 1;  // início e fim de frase (deslocados por +2 abaixo)
        for (int i = 0; i < static_cast<int>(by.size()) && i < cfg_.context_words; ++i) is_frequent_[by[i].second + 2] = 1;
        // quanto cada traço de contexto aparece no total (para o PPMI)
        feat_total_.clear();
        double sum = 0;
        for (int w = 0; w < static_cast<int>(count_.size()); ++w)
            for (const auto& [k, v] : ctx_[w]) if (frequent_key(k)) { feat_total_[k] += v; sum += v; }
        feat_sum_ = static_cast<float>(sum);
        frequent_dirty_ = false;
    }
    bool frequent_key(int64_t k) const {
        const int64_t word = k / 4 - 8;
        const int64_t idx = word + 2;
        return idx >= 0 && idx < static_cast<int64_t>(is_frequent_.size()) && is_frequent_[idx];
    }

    // projeção aleatória fixa: cada traço manda para `fanout` colunas (determinístico por hash)
    static uint64_t mix(uint64_t x) {
        x += 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    }
    void project(uint64_t feature, float value, std::vector<float>& act) const {
        uint64_t h = mix(feature);
        for (int r = 0; r < cfg_.fanout; ++r) {
            h = mix(h + r);
            act[h % static_cast<uint64_t>(cfg_.columns)] += value;
        }
    }

    std::vector<int> compute(int w) {
        // contexto: PPMI sobre vizinhos frequentes, normalizado
        std::vector<std::pair<int64_t, float>> feats;
        float norm = 0.0f;
        const float cw = count_[w] * 2.0f;  // dois traços por ocorrência
        for (const auto& [k, v] : ctx_[w]) {
            if (!frequent_key(k)) continue;
            const float p_fw = v / cw;
            const float p_f = feat_total_[k] / std::max(1.0f, feat_sum_);
            const float pmi = std::log(p_fw / std::max(1e-9f, p_f));
            if (pmi <= 0.0f) continue;
            feats.push_back({k, pmi});
            norm += pmi * pmi;
        }
        if (feats.empty()) return {};
        norm = std::sqrt(norm);
        std::vector<float> act(cfg_.columns, 0.0f);
        for (const auto& [k, v] : feats) project(static_cast<uint64_t>(k), v / norm, act);
        // terminação: o som do fim da palavra (gênero e número se ouvem)
        const std::string& s = word_[w];
        if (!s.empty()) {
            const float m = cfg_.morph_weight;
            project(0xE1D0000000000000ull ^ std::hash<std::string>{}("1" + s.substr(s.size() - 1)), m * 0.7f, act);
            if (s.size() >= 2) project(0xE2D0000000000000ull ^ std::hash<std::string>{}("2" + s.substr(s.size() - 2)), m * 0.7f, act);
        }
        // k vencedores
        std::vector<int> idx(cfg_.columns);
        for (int i = 0; i < cfg_.columns; ++i) idx[i] = i;
        std::partial_sort(idx.begin(), idx.begin() + cfg_.active, idx.end(), [&](int a, int b) { return act[a] > act[b]; });
        std::vector<int> out(idx.begin(), idx.begin() + cfg_.active);
        std::sort(out.begin(), out.end());
        return out;
    }

    template <class Out, class T> static void wr(Out& out, const T& v) { out.write(reinterpret_cast<const char*>(&v), sizeof v); }
    template <class In, class T> static bool rd(In& in, T& v) { return static_cast<bool>(in.read(reinterpret_cast<char*>(&v), sizeof v)); }

    CategoryConfig cfg_;
    std::vector<float> count_;
    std::vector<std::unordered_map<int64_t, float>> ctx_;
    std::vector<std::vector<int>> code_;
    std::vector<std::string> word_;
    float total_ = 0.0f;
    bool frequent_dirty_ = true;
    std::vector<uint8_t> is_frequent_;
    std::unordered_map<int64_t, float> feat_total_;
    float feat_sum_ = 0.0f;
};

}  // namespace eva2
