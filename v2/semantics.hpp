// Eva 2 - córtex associativo: sentido.
//
// Sequência não é sentido. "mar" significa o mar porque a palavra e a
// percepção acontecem JUNTAS, em qualquer ordem, muitas vezes, e porque
// "mar" acontece com o mar mais do que com qualquer outra coisa. Esta camada
// guarda co-ocorrências hebbianas entre percepções ([ve:mar]) e palavras,
// escaladas pela dopamina, e pontua a ligação pela especificidade (uma
// palavra que aparece com tudo, como "olha", não significa nada em especial).
// Funciona nas duas direções: ver -> nomear; ouvir -> imaginar (e agir).
#pragma once
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace eva2 {

class Semantics {
public:
    // Um momento: estas percepções (cada uma com seu peso: atenção, ativação)
    // e estas palavras aconteceram juntas, com esta dopamina. A frequência de
    // cada palavra e de cada percepção conta UMA vez por momento, não uma vez
    // por par: assim, ver muitas coisas ao mesmo tempo (os olhos trazem dezenas
    // de traços) não dilui o sentido de nenhuma palavra.
    void bind_event(const std::vector<std::pair<std::string, float>>& perceptions,
                    const std::vector<std::string>& words, float dopamine) {
        const float g = std::max(0.1f, dopamine);
        // atenção vale como repetição: um momento com algo apontado (peso 3)
        // conta como três momentos, para a palavra também; senão a palavra
        // pareceria mais específica do que é
        float intensity = 1.0f;
        for (const auto& [p, weight] : perceptions) intensity = std::max(intensity, weight);
        std::vector<std::string> seen;
        for (const auto& w : words) {
            if (std::find(seen.begin(), seen.end(), w) != seen.end()) continue;
            seen.push_back(w);
            count_w_[w] += g * intensity;
        }
        for (const auto& [p, weight] : perceptions) {
            const float gp = g * weight;
            count_p_[p] += gp;
            for (const auto& w : seen) { assoc_[p][w] += gp; assoc_rev_[w][p] += gp; }
        }
    }
    void bind(const std::string& perception, const std::string& word, float dopamine) { bind_event({{perception, 1.0f}}, {word}, dopamine); }

    // força normalizada da ligação (0..1): co-ocorrência / sqrt(freq. de cada um)
    float strength(const std::string& perception, const std::string& word) const {
        auto a = assoc_.find(perception);
        if (a == assoc_.end()) return 0.0f;
        auto b = a->second.find(word);
        if (b == a->second.end()) return 0.0f;
        const float cp = count_p_.at(perception), cw = count_w_.at(word);
        return std::min(1.0f, b->second / std::sqrt(cp * cw));
    }

    // palavras que esta percepção evoca, da mais forte para a mais fraca
    std::vector<std::pair<std::string, float>> words_for(const std::string& perception, float min_strength) const {
        std::vector<std::pair<std::string, float>> out;
        auto a = assoc_.find(perception);
        if (a == assoc_.end()) return out;
        for (const auto& [w, _] : a->second) {
            const float s = strength(perception, w);
            if (s >= min_strength) out.push_back({w, s});
        }
        std::sort(out.begin(), out.end(), [](auto& x, auto& y) { return x.second > y.second; });
        return out;
    }

    // percepções que esta palavra evoca (imaginar)
    // uma palavra ouvida uma vez só "significa" o que estava na frente dela: por
    // isso só se imagina a partir de palavras ouvidas algumas vezes (min_count)
    std::vector<std::pair<std::string, float>> perceptions_for(const std::string& word, float min_strength,
                                                               float min_count = 0.0f) const {
        std::vector<std::pair<std::string, float>> out;
        auto a = assoc_rev_.find(word);
        if (a == assoc_rev_.end()) return out;
        if (count_w_.at(word) < min_count) return out;
        for (const auto& [p, _] : a->second) {
            const float s = strength(p, word);
            if (s >= min_strength) out.push_back({p, s});
        }
        std::sort(out.begin(), out.end(), [](auto& x, auto& y) { return x.second > y.second; });
        return out;
    }

    // palavras que aparecem em quase todas as frases ("a", "o", "e") não apontam
    // para nada em especial; bebês também as ignoram no começo
    void note_turn(const std::vector<std::string>& words) {
        ++turns_;
        std::vector<std::string> seen;
        for (const auto& w : words) {
            if (std::find(seen.begin(), seen.end(), w) != seen.end()) continue;
            seen.push_back(w);
            turns_with_[w] += 1;
        }
    }
    float frequency(const std::string& w) const {
        auto it = turns_with_.find(w);
        return (it == turns_with_.end() || turns_ == 0) ? 0.0f : static_cast<float>(it->second) / turns_;
    }

    size_t size() const { return assoc_.size(); }

    template <class Out> void save(Out& out) const {
        const uint32_t n = static_cast<uint32_t>(assoc_.size());
        out.write(reinterpret_cast<const char*>(&n), sizeof n);
        for (const auto& [p, m] : assoc_) {
            write_str(out, p);
            const uint32_t k = static_cast<uint32_t>(m.size());
            out.write(reinterpret_cast<const char*>(&k), sizeof k);
            for (const auto& [w, v] : m) { write_str(out, w); out.write(reinterpret_cast<const char*>(&v), sizeof v); }
        }
        write_counts(out, count_p_);
        write_counts(out, count_w_);
        // em quantas frases cada palavra apareceu (o que separa "o" de "porco")
        std::map<std::string, float> tw;
        for (const auto& [w, n] : turns_with_) tw[w] = static_cast<float>(n);
        write_counts(out, tw);
        const int32_t t = turns_;
        out.write(reinterpret_cast<const char*>(&t), sizeof t);
    }
    template <class In> bool load(In& in) {
        uint32_t n = 0;
        if (!in.read(reinterpret_cast<char*>(&n), sizeof n)) return false;
        assoc_.clear(); assoc_rev_.clear(); count_p_.clear(); count_w_.clear();
        for (uint32_t i = 0; i < n; ++i) {
            std::string p = read_str(in);
            uint32_t k = 0;
            in.read(reinterpret_cast<char*>(&k), sizeof k);
            for (uint32_t j = 0; j < k; ++j) {
                std::string w = read_str(in);
                float v = 0;
                in.read(reinterpret_cast<char*>(&v), sizeof v);
                assoc_[p][w] = v; assoc_rev_[w][p] = v;
            }
        }
        read_counts(in, count_p_);
        read_counts(in, count_w_);
        std::map<std::string, float> tw;
        read_counts(in, tw);
        turns_with_.clear();
        for (const auto& [w, n] : tw) turns_with_[w] = static_cast<int>(n);
        int32_t t = 0;
        in.read(reinterpret_cast<char*>(&t), sizeof t);
        turns_ = t;
        return static_cast<bool>(in);
    }

private:
    template <class Out> static void write_counts(Out& out, const std::map<std::string, float>& m) {
        const uint32_t n = static_cast<uint32_t>(m.size());
        out.write(reinterpret_cast<const char*>(&n), sizeof n);
        for (const auto& [k, v] : m) { write_str(out, k); out.write(reinterpret_cast<const char*>(&v), sizeof v); }
    }
    template <class In> static void read_counts(In& in, std::map<std::string, float>& m) {
        uint32_t n = 0;
        in.read(reinterpret_cast<char*>(&n), sizeof n);
        m.clear();
        for (uint32_t i = 0; i < n; ++i) { std::string k = read_str(in); float v = 0; in.read(reinterpret_cast<char*>(&v), sizeof v); m[k] = v; }
    }
    template <class Out> static void write_str(Out& out, const std::string& s) {
        const uint32_t len = static_cast<uint32_t>(s.size());
        out.write(reinterpret_cast<const char*>(&len), sizeof len);
        out.write(s.data(), len);
    }
    template <class In> static std::string read_str(In& in) {
        uint32_t len = 0;
        in.read(reinterpret_cast<char*>(&len), sizeof len);
        std::string s(len, ' ');
        in.read(s.data(), len);
        return s;
    }
    std::map<std::string, std::map<std::string, float>> assoc_, assoc_rev_;
    std::map<std::string, float> count_p_, count_w_;
    std::map<std::string, int> turns_with_;
    int turns_ = 0;
};

}  // namespace eva2
