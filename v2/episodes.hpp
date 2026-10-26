// Eva 2 - memória de episódios, papéis e perguntas.
//
// Três coisas pequenas que uma criança de 2 a 3 anos aprende vendo e ouvindo:
//
//  1. MEMÓRIA DE EPISÓDIOS (o hipocampo): cada cena vivida fica guardada como
//     um episódio, quem, o quê, com o quê, onde, e o quanto importou. Uma pista
//     parcial ("o gato") traz de volta o episódio mais recente e marcante que
//     combina com ela. Na Eva 1 o hipocampo era de pulsos; aqui é um índice de
//     episódios, mais abstrato, porque o que importa agora é o que ela faz com
//     a lembrança.
//  2. PAPÉIS (quem faz o quê): ela vê quem age e quem sofre a ação, e ouve a
//     frase. Contando, cena após cena, em que posição da frase aparece quem age
//     (1ª, 2ª, 3ª palavra com sentido), ela aprende a ordem do português:
//     quem faz vem antes da ação, o alvo depois. Ninguém ensina a regra; é
//     estatística de cenas (como bebês de 2 anos já usam a ordem para entender
//     "o Garibaldo faz cócegas no Come-Come"; Hirsh-Pasek e Golinkoff).
//  3. PERGUNTAS: quando alguém pergunta e responde sobre a cena ("cadê o gato?
//     o gato está na sala"), ela liga cada palavra da pergunta ao que a resposta
//     trouxe de novo (o lugar, o alvo, quem fez). Assim "cadê" passa a pedir
//     um lugar, "quem" a pedir quem fez, sem regra escrita.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace eva2 {

// O que ela percebe de uma cena (símbolos de percepção, não palavras)
struct Scene {
    std::string agent, action, patient, place;  // ex.: "gato", "come", "peixe", "cozinha"
    bool agent_many = false, patient_many = false;  // um ou muitos (ela vê quantos são)
};

enum Role { kAgent = 0, kAction = 1, kPatient = 2, kPlace = 3, kRoles = 4 };
inline const char* role_name(int r) { static const char* n[] = {"quem faz", "ação", "alvo", "lugar"}; return n[r]; }

struct Episode {
    Scene scene;
    double time = 0;
    float salience = 0.5f;  // dopamina do momento
};

class EpisodicMemory {
public:
    void store(const Scene& s, double time, float salience) {
        episodes_.push_back({s, time, salience});
        if (episodes_.size() > max_) episodes_.erase(episodes_.begin());
    }
    // Pista parcial -> episódio. Cada papel da pista que bate soma; o mais
    // recente vence entre os que batem igual (o hipocampo completa o padrão).
    // `need` = papel que a lembrança precisa ter preenchido.
    const Episode* recall(const Scene& cue, int need) const {
        const Episode* best = nullptr;
        double best_score = 0;
        for (auto it = episodes_.rbegin(); it != episodes_.rend(); ++it) {
            const Scene& s = it->scene;
            if (need == kPatient && s.patient.empty()) continue;
            if (need == kPlace && s.place.empty()) continue;
            double score = 0;
            bool clash = false;
            auto cmp = [&](const std::string& c, const std::string& v) {
                if (c.empty()) return;
                if (c == v) score += 1.0; else clash = true;
            };
            cmp(cue.agent, s.agent);
            cmp(cue.action, s.action);
            cmp(cue.patient, s.patient);
            if (clash || score == 0) continue;
            score += 0.1 * it->salience;
            if (score > best_score + 1e-9) { best_score = score; best = &*it; }
        }
        return best;
    }
    // Para lembrar sozinha: um episódio marcante e recente
    const Episode* salient(double now, double u01) const {
        if (episodes_.empty()) return nullptr;
        std::vector<double> w;
        for (const auto& e : episodes_) w.push_back(e.salience / (1.0 + std::max(0.0, now - e.time) / 600000.0));
        double total = 0;
        for (double x : w) total += x;
        double r = u01 * total;
        for (size_t i = 0; i < w.size(); ++i) { if ((r -= w[i]) <= 0) return &episodes_[i]; }
        return &episodes_.back();
    }
    // contar uma lembrança gasta um pouco dela (ninguém conta a mesma coisa sem parar)
    void told(const Episode* e) { for (auto& x : episodes_) if (&x == e) x.salience *= 0.4f; }
    // um conceito passou a ser outro (os olhos juntaram duas coisas que eram uma)
    void rename(const std::string& from, const std::string& into) {
        for (auto& e : episodes_) {
            if (e.scene.agent == from) e.scene.agent = into;
            if (e.scene.patient == from) e.scene.patient = into;
            if (e.scene.place == from) e.scene.place = into;
        }
    }
    size_t size() const { return episodes_.size(); }
    const std::vector<Episode>& all() const { return episodes_; }
    void clear() { episodes_.clear(); }

    template <class Out> void save(Out& out) const {
        const uint32_t n = static_cast<uint32_t>(episodes_.size());
        out.write(reinterpret_cast<const char*>(&n), sizeof n);
        for (const auto& e : episodes_) {
            for (const auto* f : {&e.scene.agent, &e.scene.action, &e.scene.patient, &e.scene.place}) wstr(out, *f);
            const uint8_t many = (e.scene.agent_many ? 1 : 0) | (e.scene.patient_many ? 2 : 0);
            out.write(reinterpret_cast<const char*>(&many), 1);
            out.write(reinterpret_cast<const char*>(&e.time), sizeof e.time);
            out.write(reinterpret_cast<const char*>(&e.salience), sizeof e.salience);
        }
    }
    template <class In> bool load(In& in) {
        uint32_t n = 0;
        if (!in.read(reinterpret_cast<char*>(&n), sizeof n)) return false;
        episodes_.assign(n, {});
        for (auto& e : episodes_) {
            for (auto* f : {&e.scene.agent, &e.scene.action, &e.scene.patient, &e.scene.place}) *f = rstr(in);
            uint8_t many = 0;
            in.read(reinterpret_cast<char*>(&many), 1);
            e.scene.agent_many = many & 1; e.scene.patient_many = many & 2;
            in.read(reinterpret_cast<char*>(&e.time), sizeof e.time);
            in.read(reinterpret_cast<char*>(&e.salience), sizeof e.salience);
        }
        return static_cast<bool>(in);
    }

private:
    template <class Out> static void wstr(Out& out, const std::string& s) {
        const uint32_t l = static_cast<uint32_t>(s.size());
        out.write(reinterpret_cast<const char*>(&l), sizeof l);
        out.write(s.data(), l);
    }
    template <class In> static std::string rstr(In& in) {
        uint32_t l = 0;
        in.read(reinterpret_cast<char*>(&l), sizeof l);
        if (l > 4096) return {};
        std::string s(l, ' ');
        in.read(s.data(), l);
        return s;
    }
    std::vector<Episode> episodes_;
    size_t max_ = 50000;
};

// Em que posição da frase (entre as palavras que têm sentido) aparece cada papel
class RoleOrder {
public:
    static constexpr int kSlots = 5;
    void bind(int slot, int role) { if (slot >= 0 && slot < kSlots) ++count_[slot][role]; }
    // posição esperada de um papel (média)
    double expected_slot(int role) const {
        double n = 0, s = 0;
        for (int k = 0; k < kSlots; ++k) { n += count_[k][role]; s += k * count_[k][role]; }
        return n > 0 ? s / n : 99.0;
    }
    double evidence(int role) const { double n = 0; for (int k = 0; k < kSlots; ++k) n += count_[k][role]; return n; }
    // papel mais provável para uma posição, entre os papéis dados
    int role_for_slot(int slot, const std::vector<int>& allowed) const {
        int best = -1; double bv = 0;
        for (int r : allowed) {
            double tot = 0;
            for (int k = 0; k < kSlots; ++k) tot += count_[k][r];
            const double p = tot > 0 ? count_[slot][r] / tot : 0;
            if (p > bv) { bv = p; best = r; }
        }
        return best;
    }
    void clear() { for (auto& a : count_) a.fill(0); }
    template <class Out> void save(Out& out) const { out.write(reinterpret_cast<const char*>(count_.data()), sizeof count_); }
    template <class In> bool load(In& in) { return static_cast<bool>(in.read(reinterpret_cast<char*>(count_.data()), sizeof count_)); }
private:
    std::array<std::array<double, kRoles>, kSlots> count_{};
};

// Que papel uma palavra de pergunta pede ("cadê" -> lugar)
class QuestionWords {
public:
    void bind(const std::string& w, int role) { auto& c = count_[w]; c[role] += 1.0; }
    // o papel que a pergunta pede, e quão segura ela está (0..1)
    std::pair<int, double> asks(const std::vector<std::string>& words, double min_count = 3.0) const {
        int best = -1; double bp = 0;
        for (const auto& w : words) {
            auto it = count_.find(w);
            if (it == count_.end()) continue;
            double tot = 0;
            for (double x : it->second) tot += x;
            if (tot < min_count) continue;
            for (int r = 0; r < kRoles; ++r) if (it->second[r] / tot > bp) { bp = it->second[r] / tot; best = r; }
        }
        return {best, bp};
    }
    const std::map<std::string, std::array<double, kRoles>>& counts() const { return count_; }
    template <class Out> void save(Out& out) const {
        const uint32_t n = static_cast<uint32_t>(count_.size());
        out.write(reinterpret_cast<const char*>(&n), sizeof n);
        for (const auto& [w, c] : count_) {
            const uint32_t l = static_cast<uint32_t>(w.size());
            out.write(reinterpret_cast<const char*>(&l), sizeof l);
            out.write(w.data(), l);
            out.write(reinterpret_cast<const char*>(c.data()), sizeof c);
        }
    }
    template <class In> bool load(In& in) {
        uint32_t n = 0;
        if (!in.read(reinterpret_cast<char*>(&n), sizeof n)) return false;
        count_.clear();
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t l = 0;
            in.read(reinterpret_cast<char*>(&l), sizeof l);
            if (l > 4096) return false;
            std::string w(l, ' ');
            in.read(w.data(), l);
            std::array<double, kRoles> c{};
            in.read(reinterpret_cast<char*>(c.data()), sizeof c);
            count_[w] = c;
        }
        return static_cast<bool>(in);
    }
private:
    std::map<std::string, std::array<double, kRoles>> count_;
};

}  // namespace eva2
