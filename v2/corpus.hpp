// Eva 2 - a fala que uma criança ouve em casa, em escala, e um verificador.
//
// Uma gramática pequena do português falado com criança ("o gato é bonito",
// "cadê a bola", "vamos brincar", "a vaca come o capim") com ~800 palavras
// (o vocabulário de uma criança de 2 a 3 anos), concordância de gênero e
// número, e restrições de sentido (quem come é bicho ou gente; o que se come
// é comida; "no/na" vai antes de lugar). As frases são sorteadas com
// frequência zipfiana (algumas palavras aparecem muito, a maioria pouco),
// como na fala real.
//
// A mesma gramática serve para GERAR o que ela ouve e para VERIFICAR o que
// ela diz. Ela nunca vê a gramática: só as frases.
//
// Para medir generalização, uma parte dos bichos e pessoas é "retida": eles
// só aparecem em posição de objeto ("eu vejo a girafa", "cadê a girafa",
// "é uma girafa"), nunca como sujeito. O teste começa uma frase com eles
// ("a girafa ...") e vê se ela continua de um jeito gramatical que nunca ouviu.
#pragma once
#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace corpus {

enum Pos { DET, INDEF, NOUN, ADJ, COP, EST, LOC, DE, VI, VINF, COME, GOSTA, QUER, VE, LIT, POSS, NAME, CHAMA };
enum Sem { ANIMAL = 1, PERSON = 2, FOOD = 4, OBJECT = 8, PLACE = 16 };
constexpr int ANIM = ANIMAL | PERSON;
constexpr int THING = ANIMAL | PERSON | FOOD | OBJECT;  // tudo menos lugar
constexpr int ANY = THING | PLACE;

struct Entry {
    std::string word;
    Pos pos;
    char g = '*';   // m, f, * (invariável)
    char n = '*';   // s, p, *
    int sem = 0;
    std::string lemma;
    bool held = false;  // retido: só como objeto
    char tense = '*';   // verbos: p (presente), a (passado)
    char person = '*';  // possessivos: f (de quem fala: meu), o (de quem ouve: seu)
};

// Uma posição de um molde de frase
struct Slot {
    Pos pos;
    int gvar = -1, nvar = -1;  // variáveis de concordância (0..3)
    int sem = ANY;
    std::string lit;           // para LIT
    bool subject = false;      // posição de sujeito (retidos não entram)
    char fixn = 0;             // número fixo ('s'), quando o molde exige
};

struct Pattern {
    std::string name;
    std::vector<Slot> slots;
    double weight;
    bool predicative = false;  // molde onde retidos não entram em nenhuma posição de substantivo
};

inline std::vector<std::string> split(const std::string& s) {
    std::istringstream in(s);
    std::vector<std::string> v;
    for (std::string x; in >> x;) v.push_back(x);
    return v;
}

class Grammar {
public:
    explicit Grammar(unsigned seed = 11, double held_fraction = 0.2, double zipf = 0.7) : rng_(seed) {
        build_lexicon();
        build_patterns();
        // retém uma fração dos bichos e pessoas (o par singular/plural junto)
        std::vector<std::string> anim;
        for (const auto& e : lex_) if (e.pos == NOUN && (e.sem & ANIM) && e.n == 's') anim.push_back(e.lemma);
        std::sort(anim.begin(), anim.end());
        anim.erase(std::unique(anim.begin(), anim.end()), anim.end());
        std::shuffle(anim.begin(), anim.end(), rng_);
        const size_t k = static_cast<size_t>(anim.size() * held_fraction);
        for (size_t i = 0; i < k; ++i) held_.insert(anim[i]);
        for (auto& e : lex_) if (e.pos == NOUN && held_.count(e.lemma)) e.held = true;
        // pesos zipfianos dentro de cada grupo (mesma classe gramatical e de sentido)
        std::map<std::string, std::vector<int>> groups;
        for (int i = 0; i < static_cast<int>(lex_.size()); ++i) {
            const auto& e = lex_[i];
            groups[std::to_string(e.pos) + "/" + std::to_string(e.sem)].push_back(i);
        }
        weight_.assign(lex_.size(), 1.0);
        for (auto& [k2, ids] : groups) {
            std::vector<std::string> lemmas;
            for (int i : ids) lemmas.push_back(lex_[i].lemma);
            std::sort(lemmas.begin(), lemmas.end());
            lemmas.erase(std::unique(lemmas.begin(), lemmas.end()), lemmas.end());
            std::shuffle(lemmas.begin(), lemmas.end(), rng_);
            std::map<std::string, double> w;
            for (size_t r = 0; r < lemmas.size(); ++r) w[lemmas[r]] = 1.0 / std::pow(r + 1.0, zipf);
            for (int i : ids) weight_[i] = w[lex_[i].lemma];
        }
        for (int i = 0; i < static_cast<int>(lex_.size()); ++i) by_word_[lex_[i].word].push_back(i);
    }

    // uma frase que ela ouve
    std::vector<std::string> sentence() {
        std::vector<double> pw;
        for (const auto& p : patterns_) pw.push_back(p.weight);
        std::discrete_distribution<size_t> pick(pw.begin(), pw.end());
        for (int attempt = 0; attempt < 50; ++attempt) {
            const Pattern& p = patterns_[pick(rng_)];
            std::vector<std::string> out;
            if (fill(p, out)) return out;
        }
        return {"cadê", "o", "gato"};
    }

    // ---------------------------------------------------------------- verificar
    struct Check { bool syntax = false, agreement = false, semantics = false; std::string pattern; };
    // A frase inteira casa com algum molde? syntax: classes certas; agreement:
    // e concordância certa; semantics: e o sentido também.
    Check check(const std::vector<std::string>& s) const {
        Check best;
        for (const auto& p : patterns_) {
            if (p.slots.size() != s.size()) continue;
            for (int level = 2; level >= 0; --level) {
                if (match(p, s, level >= 1, level >= 2)) {
                    Check c;
                    c.syntax = true; c.agreement = level >= 1; c.semantics = level >= 2; c.pattern = p.name;
                    if (score(c) > score(best)) best = c;
                    break;
                }
            }
        }
        return best;
    }

    // começos de frase para o teste: "artigo + substantivo" de sujeito
    std::vector<std::vector<std::string>> subject_starts(bool held) const {
        std::vector<std::vector<std::string>> out;
        for (const auto& e : lex_) {
            if (e.pos != NOUN || !(e.sem & ANIM) || e.held != held) continue;
            const char* det = e.g == 'm' ? (e.n == 's' ? "o" : "os") : (e.n == 's' ? "a" : "as");
            out.push_back({det, e.word});
        }
        return out;
    }

    const std::vector<Entry>& lexicon() const { return lex_; }

    // ---------------------------------------------------------------- cenas
    // Uma coisa que acontece na casa e que a criança vê: quem (agente), o quê
    // (ação), com o quê (alvo), onde (lugar). Quem cuida dela descreve a cena,
    // às vezes pergunta sobre ela e responde ("cadê o gato? o gato está na sala").
    enum Kind { AT, EAT, SEE, LIKE, INTRANS };
    struct Event {
        Kind kind = AT;
        std::string agent, action, patient, place;  // lemas (singular); ação = forma singular do verbo
        char an = 's', pn = 's';                    // número do agente e do alvo
        float valence = 0.0f;                       // como foi: chorar, cair (ruim); rir, brincar (bom)
    };
    Event random_event() {
        Event e;
        std::discrete_distribution<int> k({3, 3, 2, 2, 2});
        e.kind = static_cast<Kind>(k(rng_));
        e.agent = pick_lemma(NOUN, ANIM);
        e.place = pick_lemma(NOUN, PLACE);
        std::uniform_real_distribution<double> u(0, 1);
        e.an = u(rng_) < 0.7 ? 's' : 'p';
        e.pn = u(rng_) < 0.7 ? 's' : 'p';
        switch (e.kind) {
            case AT: e.action = "está"; break;
            case EAT: e.action = "come"; e.patient = pick_lemma(NOUN, FOOD); break;
            case SEE: e.action = "vê"; do { e.patient = pick_lemma(NOUN, THING); } while (e.patient == e.agent); break;
            case LIKE: e.action = "gosta"; do { e.patient = pick_lemma(NOUN, THING); } while (e.patient == e.agent); break;
            case INTRANS: e.action = pick_lemma(VI, 0); break;
        }
        if (!has_form(e.patient, e.pn)) e.pn = 's';
        if (!has_form(e.agent, e.an)) e.an = 's';
        e.valence = valence_of(e);
        return e;
    }
    static float valence_of(const Event& e) {
        static const std::set<std::string> good{"brinca", "ri", "dança", "canta", "pula", "sorri", "nada", "voa"};
        static const std::set<std::string> bad{"chora", "cai", "grita", "tosse", "espirra"};
        if (e.kind == INTRANS) return good.count(e.action) ? 0.8f : bad.count(e.action) ? -0.8f : 0.0f;
        if (e.kind == EAT) return 0.3f;
        if (e.kind == LIKE) return 0.3f;
        return 0.0f;
    }
    // Coisas que ela só conhece dos livros ("olha a girafa"): nunca aparecem nas
    // cenas da casa. Fixas (pelo nome), para todas as gramáticas concordarem.
    static bool book_only(const std::string& lemma) {
        unsigned h = 2166136269u;
        for (unsigned char c : lemma) h = (h ^ c) * 16777619u;
        return h % 10 == 0;
    }
    std::vector<std::string> book_things() const {
        std::vector<std::string> out;
        for (const auto& e : lex_) if (e.pos == NOUN && e.n == 's' && (e.sem & THING) && book_only(e.lemma)) out.push_back(e.lemma);
        return out;
    }
    // ---------------------------------------------------------------- sim, não, não sei
    // a mesma cena com outra coisa no lugar do alvo (ou outro lugar): o que NÃO aconteceu
    Event altered(const Event& e) {
        Event x = e;
        if (e.kind == AT) { do { x.place = pick_lemma(NOUN, PLACE); } while (x.place == e.place); }
        else if (e.kind == EAT) { do { x.patient = pick_lemma(NOUN, FOOD); } while (x.patient == e.patient); }
        else if (e.kind == SEE || e.kind == LIKE) { do { x.patient = pick_lemma(NOUN, THING); } while (x.patient == e.patient || x.patient == e.agent); }
        if (!has_form(x.patient, x.pn)) x.pn = 's';
        return x;
    }
    bool can_alter(const Event& e) const { return e.kind != INTRANS; }
    // pergunta de sim ou não: a frase com a voz subindo no fim ("o gato comeu o peixe?")
    std::vector<std::string> yes_no(const Event& claim, bool past = false) const {
        auto s = describe(claim, past && claim.kind != AT);
        s.push_back("<?>");
        return s;
    }
    // a resposta: "sim, o gato comeu o peixe" / "não, o gato comeu o pão"
    std::vector<std::string> yes_no_answer(const Event& truth, bool yes, bool past = false) const {
        auto s = describe(truth, past && truth.kind != AT);
        s.insert(s.begin(), yes ? "sim" : "não");
        return s;
    }
    // negação: "o gato não está na sala"
    std::vector<std::string> negated(const Event& claim, bool past = false) const {
        auto s = describe(claim, past && claim.kind != AT);
        s.insert(s.begin() + 2, "não");  // depois de "o gato"
        return s;
    }
    // ---------------------------------------------------------------- nomes, meu e seu
    static std::vector<std::string> pet_names() {
        return split("rex mimi totó lili bob nina fred lola tico bela max kiki dudu zeca pipo nino luna teco bibi juju "
                     "faísca pingo pituca bolinha xodó");
    }
    // possessivo que concorda com a coisa (singular): de quem fala (meu/minha) ou de quem ouve (seu/sua)
    std::string poss(const std::string& lemma, bool speaker) const {
        const char g = gender(lemma);
        return speaker ? (g == 'm' ? "meu" : "minha") : (g == 'm' ? "seu" : "sua");
    }
    // "o meu gato chama mimi"
    std::vector<std::string> named(const std::string& thing, bool speaker_owns, const std::string& name) const {
        auto s = np(thing, 's');
        s.insert(s.begin() + 1, poss(thing, speaker_owns));
        s.push_back("chama"); s.push_back(name);
        return s;
    }
    // "como chama o seu gato"
    std::vector<std::string> ask_name(const std::string& thing, bool speaker_owns) const {
        std::vector<std::string> s{"como", "chama"};
        auto o = np(thing, 's');
        o.insert(o.begin() + 1, poss(thing, speaker_owns));
        s.insert(s.end(), o.begin(), o.end());
        return s;
    }
    // coisas que alguém da casa pode ter e que têm nome (bichos e brinquedos)
    std::vector<std::string> nameable() const {
        std::vector<std::string> out;
        for (const auto& e : lex_) if (e.pos == NOUN && e.n == 's' && (e.sem & (ANIMAL | OBJECT)) && !book_only(e.lemma)) out.push_back(e.lemma);
        return out;
    }
    // ---------------------------------------------------------------- sentimentos
    // "você está feliz", "eu estou triste"
    static std::vector<std::string> feeling(bool speaker, bool happy) {
        if (speaker) return {"eu", "estou", happy ? "feliz" : "triste"};
        return {"você", "está", happy ? "feliz" : "triste"};
    }
    static std::vector<std::string> ask_feeling() { return {"como", "você", "está"}; }
    // a frase que descreve a cena
    // (past: contando depois o que aconteceu: "o gato comeu o peixe")
    std::vector<std::string> describe(const Event& e, bool past = false) const {
        std::vector<std::string> s = np(e.agent, e.an);
        switch (e.kind) {
            case AT: s.push_back(verb_form(EST, "está", e.an, past)); s.push_back(loc(e.place)); s.push_back(e.place); break;
            case EAT: s.push_back(verb_form(COME, "come", e.an, past)); { auto o = np(e.patient, e.pn); s.insert(s.end(), o.begin(), o.end()); } break;
            case SEE: s.push_back(verb_form(VE, "vê", e.an, past)); { auto o = np(e.patient, e.pn); s.insert(s.end(), o.begin(), o.end()); } break;
            case LIKE: s.push_back(verb_form(GOSTA, "gosta", e.an, past)); s.push_back(de(e.patient, e.pn)); s.push_back(noun_form(e.patient, e.pn)); break;
            case INTRANS: s.push_back(verb_form(VI, e.action, e.an, past)); break;
        }
        return s;
    }
    // pergunta sobre a cena: 0 = onde (cadê), 1 = o quê (o que ... come/vê), 2 = quem (quem come/vê ...)
    bool can_ask(const Event& e, int field) const {
        if (field == 0) return true;
        return e.kind == EAT || e.kind == SEE;
    }
    // (past: sobre algo que já passou: "o que o gato comeu?"; "cadê" é sempre agora)
    std::vector<std::string> question(const Event& e, int field, bool past = false) const {
        if (field == 0) { auto s = np(e.agent, e.an); s.insert(s.begin(), "cadê"); return s; }
        const Pos vp = e.kind == EAT ? COME : VE;
        if (field == 1) { std::vector<std::string> s{"o", "que"}; auto a = np(e.agent, e.an); s.insert(s.end(), a.begin(), a.end()); s.push_back(verb_form(vp, e.action, e.an, past)); return s; }
        std::vector<std::string> s{"quem", verb_form(vp, e.action, 's', past)};
        auto o = np(e.patient, e.pn);
        s.insert(s.end(), o.begin(), o.end());
        return s;
    }
    std::vector<std::string> answer(const Event& e, int field, bool past = false) const {
        if (field == 0) { Event at = e; at.kind = AT; return describe(at); }
        return describe(e, past);
    }
    char tense_of(const std::string& w) const {
        auto it = by_word_.find(w);
        if (it == by_word_.end()) return '*';
        for (int i : it->second) if (lex_[i].tense != '*') return lex_[i].tense;
        return '*';
    }
    char number_of(const std::string& w) const {
        auto it = by_word_.find(w);
        return it == by_word_.end() ? '*' : lex_[it->second.front()].n;
    }
    // o lema de uma palavra ("gatos" -> "gato"), para conferir respostas
    std::string lemma_of(const std::string& w) const {
        auto it = by_word_.find(w);
        if (it == by_word_.end()) return w;
        const Entry& e = lex_[it->second.front()];
        return e.lemma.empty() ? w : e.lemma;
    }
    int lex_pos(const std::string& w) const {
        auto it = by_word_.find(w);
        return it == by_word_.end() ? -1 : static_cast<int>(lex_[it->second.front()].pos);
    }
    const std::set<std::string>& held() const { return held_; }
    size_t vocabulary_size() const { return by_word_.size(); }
    // classe "verdadeira" de uma palavra (para medir se as categorias que emergem batem)
    std::string gold(const std::string& w) const {
        auto it = by_word_.find(w);
        if (it == by_word_.end()) return "?";
        const Entry& e = lex_[it->second.front()];
        static const char* names[] = {"DET", "INDEF", "N", "ADJ", "COP", "EST", "LOC", "DE", "VI", "VINF", "COME", "GOSTA", "QUER", "VE", "LIT", "POSS", "NAME", "CHAMA"};
        std::string g = names[e.pos];
        if (e.pos == LIT) return "LIT:" + w;
        if (e.pos == NOUN || e.pos == ADJ || e.pos == DET || e.pos == INDEF || e.pos == LOC || e.pos == DE) g += std::string("_") + e.g;
        if (e.pos != VINF) g += std::string("_") + e.n;
        if (e.pos == COME || e.pos == GOSTA || e.pos == QUER || e.pos == VE || e.pos == COP || e.pos == EST) g = std::string("VT_") + e.n;
        return g;
    }
    std::string pos_only(const std::string& w) const {
        auto it = by_word_.find(w);
        if (it == by_word_.end()) return "?";
        const Entry& e = lex_[it->second.front()];
        static const char* names[] = {"DET", "INDEF", "N", "ADJ", "COP", "EST", "LOC", "DE", "VI", "VINF", "COME", "GOSTA", "QUER", "VE", "LIT", "POSS", "NAME", "CHAMA"};
        return e.pos == LIT ? "LIT:" + w : names[e.pos];
    }

private:
    std::string pick_lemma(Pos pos, int sem) {
        std::vector<int> ids; std::vector<double> w;
        for (int i = 0; i < static_cast<int>(lex_.size()); ++i) {
            const Entry& e = lex_[i];
            if (e.pos != pos || e.n == 'p' || e.tense == 'a') continue;
            if (pos == NOUN && sem != PLACE && book_only(e.lemma)) continue;  // só dos livros: nunca numa cena
            if (sem && !(e.sem & sem)) continue;
            ids.push_back(i); w.push_back(weight_[i]);
        }
        std::discrete_distribution<size_t> pick(w.begin(), w.end());
        return lex_[ids[pick(rng_)]].lemma;
    }
    const Entry* noun_entry(const std::string& lemma, char n) const {
        for (const auto& e : lex_) if (e.pos == NOUN && e.lemma == lemma && e.n == n) return &e;
        return nullptr;
    }
    bool has_form(const std::string& lemma, char n) const { return lemma.empty() || noun_entry(lemma, n) != nullptr; }
    std::string noun_form(const std::string& lemma, char n) const { const Entry* e = noun_entry(lemma, n); return e ? e->word : lemma; }
    char gender(const std::string& lemma) const { const Entry* e = noun_entry(lemma, 's'); return e ? e->g : 'm'; }
    std::vector<std::string> np(const std::string& lemma, char n) const {
        const char g = gender(lemma);
        const char* det = g == 'm' ? (n == 's' ? "o" : "os") : (n == 's' ? "a" : "as");
        return {det, noun_form(lemma, n)};
    }
    std::string verb_form(Pos pos, const std::string& lemma, char n, bool past = false) const {
        const char t = past ? 'a' : 'p';
        for (const auto& e : lex_) if (e.pos == pos && e.lemma == lemma && e.n == n && e.tense == t) return e.word;
        for (const auto& e : lex_) if (e.pos == pos && e.lemma == lemma && e.n == n) return e.word;
        return lemma;
    }
    std::string loc(const std::string& place) const { return gender(place) == 'm' ? "no" : "na"; }
    std::string de(const std::string& lemma, char n) const {
        const char g = gender(lemma);
        return g == 'm' ? (n == 's' ? "do" : "dos") : (n == 's' ? "da" : "das");
    }
    static int score(const Check& c) { return c.syntax + c.agreement + c.semantics; }

    bool fits(const Entry& e, const Slot& sl, const char* g, const char* n, bool agree, bool sem, bool for_generation) const {
        if (sl.pos == LIT) return e.pos == LIT && e.word == sl.lit;
        if (e.pos != sl.pos) return false;
        if (sl.fixn && e.n != '*' && e.n != sl.fixn) return false;
        if (agree) {
            if (sl.gvar >= 0 && g[sl.gvar] && e.g != '*' && e.g != g[sl.gvar]) return false;
            if (sl.nvar >= 0 && n[sl.nvar] && e.n != '*' && e.n != n[sl.nvar]) return false;
        }
        if (sem && sl.pos == NOUN && !(e.sem & sl.sem)) return false;
        if (for_generation && e.held && sl.pos == NOUN && sl.subject) return false;
        return true;
    }
    static void bind(const Entry& e, const Slot& sl, char* g, char* n) {
        if (sl.gvar >= 0 && !g[sl.gvar] && e.g != '*') g[sl.gvar] = e.g;
        if (sl.nvar >= 0 && !n[sl.nvar] && e.n != '*') n[sl.nvar] = e.n;
    }

    bool match(const Pattern& p, const std::vector<std::string>& s, bool agree, bool sem) const {
        char g[4] = {0, 0, 0, 0}, n[4] = {0, 0, 0, 0};
        // substantivos primeiro (eles fixam gênero e número), depois o resto
        for (int pass = 0; pass < 2; ++pass)
            for (size_t i = 0; i < s.size(); ++i) {
                const Slot& sl = p.slots[i];
                if ((pass == 0) != (sl.pos == NOUN)) continue;
                auto it = by_word_.find(s[i]);
                if (it == by_word_.end()) return false;
                bool ok = false;
                for (int idx : it->second) {
                    const Entry& e = lex_[idx];
                    if (fits(e, sl, g, n, agree, sem, false)) { bind(e, sl, g, n); ok = true; break; }
                }
                if (!ok) return false;
            }
        return true;
    }

    bool fill(const Pattern& p, std::vector<std::string>& out) {
        if (p.predicative) {}  // retidos já ficam fora pelo `subject` das posições
        char g[4] = {0, 0, 0, 0}, n[4] = {0, 0, 0, 0};
        out.assign(p.slots.size(), "");
        for (int pass = 0; pass < 2; ++pass)
            for (size_t i = 0; i < p.slots.size(); ++i) {
                const Slot& sl = p.slots[i];
                if ((pass == 0) != (sl.pos == NOUN)) continue;
                if (sl.pos == LIT) { out[i] = sl.lit; continue; }
                std::vector<int> ids;
                std::vector<double> w;
                for (int k = 0; k < static_cast<int>(lex_.size()); ++k) {
                    const Entry& e = lex_[k];
                    if (!fits(e, sl, g, n, true, true, true)) continue;
                    if (p.predicative && e.held) continue;
                    if (e.tense == 'a') continue;  // a fala comum da casa é no presente; o passado vem nas lembranças
                    ids.push_back(k);
                    w.push_back(weight_[k]);
                }
                if (ids.empty()) return false;
                std::discrete_distribution<size_t> pick(w.begin(), w.end());
                const Entry& e = lex_[ids[pick(rng_)]];
                bind(e, sl, g, n);
                out[i] = e.word;
            }
        return true;
    }

    void add_noun(const std::string& spec, char g, int sem) {
        // "leão/leões" ou "gato" (plural regular)
        std::string sg = spec, pl;
        const auto slash = spec.find('/');
        if (slash != std::string::npos) { sg = spec.substr(0, slash); pl = spec.substr(slash + 1); }
        else pl = plural(sg);
        lex_.push_back({sg, NOUN, g, 's', sem, sg});
        if (sem != PLACE && !pl.empty() && pl != "-") lex_.push_back({pl, NOUN, g, 'p', sem, sg});
    }
    static bool ends(const std::string& s, const std::string& suf) { return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0; }
    static std::string plural(const std::string& s) {
        if (ends(s, "r") || ends(s, "z")) return s + "es";
        if (ends(s, "m")) return s.substr(0, s.size() - 1) + "ns";
        if (ends(s, "l")) return s.substr(0, s.size() - 1) + "is";
        return s + "s";
    }
    void add_adj(const std::string& spec) {
        // "bonito" (varia -o/-a), "grande" ou "azul/azuis" (invariável em gênero)
        std::string base = spec, pl;
        const auto slash = spec.find('/');
        if (slash != std::string::npos) { base = spec.substr(0, slash); pl = spec.substr(slash + 1); }
        if (slash == std::string::npos && ends(base, "o")) {
            const std::string stem = base.substr(0, base.size() - 1);
            lex_.push_back({stem + "o", ADJ, 'm', 's', 0, base});
            lex_.push_back({stem + "a", ADJ, 'f', 's', 0, base});
            lex_.push_back({stem + "os", ADJ, 'm', 'p', 0, base});
            lex_.push_back({stem + "as", ADJ, 'f', 'p', 0, base});
        } else {
            if (pl.empty()) pl = plural(base);
            lex_.push_back({base, ADJ, '*', 's', 0, base});
            lex_.push_back({pl, ADJ, '*', 'p', 0, base});
        }
    }
    void add_verb(Pos pos, const std::string& sg, const std::string& pl, const std::string& past_sg = "", const std::string& past_pl = "") {
        Entry a{sg, pos, '*', 's', 0, sg}; a.tense = 'p'; lex_.push_back(a);
        Entry b{pl, pos, '*', 'p', 0, sg}; b.tense = 'p'; lex_.push_back(b);
        if (!past_sg.empty()) {
            Entry c{past_sg, pos, '*', 's', 0, sg}; c.tense = 'a'; lex_.push_back(c);
            Entry d{past_pl, pos, '*', 'p', 0, sg}; d.tense = 'a'; lex_.push_back(d);
        }
    }
    void lit(const std::string& w) { lex_.push_back({w, LIT, '*', '*', 0, w}); }

    void build_lexicon() {
        for (auto s : split("cachorro gato cavalo porco pato leão/leões tigre macaco elefante urso coelho sapo peixe pássaro rato "
                            "boi galo lobo jacaré papagaio camelo burro bode pinguim golfinho tubarão/tubarões gorila hipopótamo canguru pavão/pavões"))
            add_noun(s, 'm', ANIMAL);
        for (auto s : split("vaca galinha girafa zebra ovelha cobra tartaruga borboleta abelha formiga aranha baleia coruja raposa onça "
                            "foca arara joaninha minhoca lagarta cabra pata gata cachorra égua barata lula lontra preguiça toupeira"))
            add_noun(s, 'f', ANIMAL);
        for (auto s : split("menino papai vovô bebê irmão/irmãos tio amigo moço homem médico professor palhaço"))
            add_noun(s, 'm', PERSON);
        for (auto s : split("menina mamãe vovó irmã tia amiga moça mulher professora médica princesa bruxa"))
            add_noun(s, 'f', PERSON);
        for (auto s : split("pão/pães bolo queijo leite suco biscoito ovo arroz/- feijão/feijões mamão/mamões milho sorvete chocolate iogurte "
                            "abacaxi morango tomate chá mel/- presunto pudim café doce pastel/pastéis sanduíche"))
            add_noun(s, 'm', FOOD);
        for (auto s : split("banana maçã pera uva laranja melancia cenoura batata sopa pizza torta bolacha gelatina manga salada "
                            "carne fruta pipoca mexerica goiaba ameixa cereja abóbora coxinha tapioca"))
            add_noun(s, 'f', FOOD);
        for (auto s : split("carro livro sapato chapéu copo prato brinquedo balão/balões trem avião/aviões barco caminhão/caminhões lápis/- "
                            "telefone relógio travesseiro cobertor pente boné tambor robô apito dado sino papel/papéis botão/botões garfo "
                            "cesto martelo carrinho ursinho baldinho pião/piões"))
            add_noun(s, 'm', OBJECT);
        for (auto s : split("bola boneca cadeira mesa caneca colher faca camiseta meia cama porta janela bicicleta mochila caixa chave "
                            "flor árvore pedra folha estrela lua nuvem escova toalha panela bolsa pipa peteca corda televisão/televisões "
                            "lanterna tesoura almofada xícara"))
            add_noun(s, 'f', OBJECT);
        for (auto s : split("quarto banheiro jardim parque sofá quintal mercado zoológico berço chão tapete escorregador"))
            add_noun(s, 'm', PLACE);
        for (auto s : split("cozinha sala escola praia rua casa piscina varanda garagem fazenda floresta praça"))
            add_noun(s, 'f', PLACE);

        for (auto s : split("bonito pequeno lindo gostoso cheiroso novo velho sujo limpo molhado seco cansado quieto rápido lento "
                            "gordo magro branco preto amarelo vermelho roxo redondo quentinho gelado fofo bravo feio escuro claro "
                            "alto baixo cheio vazio macio duro pesado engraçado assustado sozinho comprido curto frio bagunçado "
                            "arrumado perfumado dourado listrado pintado escondido"))
            add_adj(s);
        for (auto s : split("grande verde azul/azuis feliz/felizes triste quente forte legal/legais enorme alegre marrom/marrons "
                            "gentil/gentis veloz/velozes inteligente valente elegante"))
            add_adj(s);

        // presente singular, presente plural, passado singular, passado plural
        const char* vi[][4] = {{"corre", "correm", "correu", "correram"}, {"dorme", "dormem", "dormiu", "dormiram"},
                               {"pula", "pulam", "pulou", "pularam"}, {"brinca", "brincam", "brincou", "brincaram"},
                               {"chora", "choram", "chorou", "choraram"}, {"ri", "riem", "riu", "riram"},
                               {"canta", "cantam", "cantou", "cantaram"}, {"dança", "dançam", "dançou", "dançaram"},
                               {"nada", "nadam", "nadou", "nadaram"}, {"voa", "voam", "voou", "voaram"},
                               {"anda", "andam", "andou", "andaram"}, {"mia", "miam", "miou", "miaram"},
                               {"late", "latem", "latiu", "latiram"}, {"sorri", "sorriem", "sorriu", "sorriram"},
                               {"acorda", "acordam", "acordou", "acordaram"}, {"grita", "gritam", "gritou", "gritaram"},
                               {"cai", "caem", "caiu", "caíram"}, {"senta", "sentam", "sentou", "sentaram"},
                               {"sobe", "sobem", "subiu", "subiram"}, {"desce", "descem", "desceu", "desceram"},
                               {"chega", "chegam", "chegou", "chegaram"}, {"sai", "saem", "saiu", "saíram"},
                               {"volta", "voltam", "voltou", "voltaram"}, {"espirra", "espirram", "espirrou", "espirraram"},
                               {"tosse", "tossem", "tossiu", "tossiram"}, {"rola", "rolam", "rolou", "rolaram"}};
        for (auto& v : vi) add_verb(VI, v[0], v[1], v[2], v[3]);
        for (auto s : split("brincar comer dormir passear nadar correr desenhar pintar cantar dançar ler pular jogar cozinhar andar "
                            "sair voltar olhar contar escrever arrumar lavar beber subir descer esconder procurar assistir viajar"))
            lex_.push_back({s, VINF, '*', '*', 0, s});
        add_verb(COME, "come", "comem", "comeu", "comeram");
        add_verb(GOSTA, "gosta", "gostam", "gostou", "gostaram");
        add_verb(QUER, "quer", "querem");
        add_verb(VE, "vê", "veem", "viu", "viram");
        add_verb(COP, "é", "são");
        add_verb(EST, "está", "estão", "estava", "estavam");
        lex_.push_back({"o", DET, 'm', 's'}); lex_.push_back({"a", DET, 'f', 's'});
        lex_.push_back({"os", DET, 'm', 'p'}); lex_.push_back({"as", DET, 'f', 'p'});
        lex_.push_back({"um", INDEF, 'm', 's'}); lex_.push_back({"uma", INDEF, 'f', 's'});
        lex_.push_back({"no", LOC, 'm', 's'}); lex_.push_back({"na", LOC, 'f', 's'});
        lex_.push_back({"do", DE, 'm', 's'}); lex_.push_back({"da", DE, 'f', 's'});
        lex_.push_back({"dos", DE, 'm', 'p'}); lex_.push_back({"das", DE, 'f', 'p'});
        for (auto s : split("eu você vejo gosto quero cadê olha vamos muito de que quem sim não sei como estou")) lit(s);
        for (const char* w : {"meu", "minha", "seu", "sua"}) {
            Entry e{w, POSS, (w[0] == 'm' && w[1] == 'e') || (w[0] == 's' && w[1] == 'e') ? 'm' : 'f', 's'};
            e.person = w[0] == 'm' ? 'f' : 'o';
            lex_.push_back(e);
        }
        for (const auto& n : pet_names()) lex_.push_back({n, NAME, '*', 's', 0, n});
        add_verb(CHAMA, "chama", "chamam");
        // "quer" também é literal em "você quer"; já está como QUER singular
    }

    static Slot S(Pos p, int g = -1, int n = -1, int sem = ANY, bool subject = false) { Slot s; s.pos = p; s.gvar = g; s.nvar = n; s.sem = sem; s.subject = subject; return s; }
    static Slot L(const std::string& w) { Slot s; s.pos = LIT; s.lit = w; return s; }

    void build_patterns() {
        auto subj = [](int sem) { return S(NOUN, 0, 0, sem, true); };
        patterns_ = {
            {"N é ADJ", {S(DET, 0, 0), subj(ANY), S(COP, -1, 0), S(ADJ, 0, 0)}, 10, true},
            {"N está no LUGAR", {S(DET, 0, 0), subj(THING), S(EST, -1, 0), S(LOC, 1, 1), S(NOUN, 1, 1, PLACE)}, 8},
            {"N come COMIDA", {S(DET, 0, 0), subj(ANIM), S(COME, -1, 0), S(DET, 1, 1), S(NOUN, 1, 1, FOOD)}, 8},
            {"eu vejo N", {L("eu"), L("vejo"), S(DET, 0, 0), S(NOUN, 0, 0, THING)}, 6},
            {"eu gosto do N", {L("eu"), L("gosto"), S(DE, 0, 0), S(NOUN, 0, 0, THING)}, 5},
            {"cadê N", {L("cadê"), S(DET, 0, 0), S(NOUN, 0, 0, THING)}, 6},
            {"N VERBO", {S(DET, 0, 0), subj(ANIM), S(VI, -1, 0)}, 10},
            {"vamos VERBO", {L("vamos"), S(VINF)}, 5},
            {"olha N ADJ", {L("olha"), S(DET, 0, 0), S(NOUN, 0, 0, THING, true), S(ADJ, 0, 0)}, 6, true},
            {"N é muito ADJ", {S(DET, 0, 0), subj(ANY), S(COP, -1, 0), L("muito"), S(ADJ, 0, 0)}, 4, true},
            {"eu quero N", {L("eu"), L("quero"), S(DET, 0, 0), S(NOUN, 0, 0, FOOD | OBJECT)}, 5},
            {"você quer COMIDA", {L("você"), S(QUER, -1, 2), S(DET, 0, 0), S(NOUN, 0, 0, FOOD)}, 4},
            {"N ADJ está no LUGAR", {S(DET, 0, 0), subj(THING), S(ADJ, 0, 0), S(EST, -1, 0), S(LOC, 1, 1), S(NOUN, 1, 1, PLACE)}, 3, true},
            {"é um N", {S(COP, -1, 0), S(INDEF, 1, 0), S(NOUN, 1, 0, THING)}, 5},
            {"N gosta do N", {S(DET, 0, 0), subj(ANIM), S(GOSTA, -1, 0), S(DE, 1, 1), S(NOUN, 1, 1, THING)}, 4},
            {"eu gosto de VERBO", {L("eu"), L("gosto"), L("de"), S(VINF)}, 3},
            {"N quer VERBO", {S(DET, 0, 0), subj(ANIM), S(QUER, -1, 0), S(VINF)}, 3},
            {"N vê N", {S(DET, 0, 0), subj(ANIM), S(VE, -1, 0), S(DET, 1, 1), S(NOUN, 1, 1, THING)}, 3},
            {"olha N", {L("olha"), S(DET, 0, 0), S(NOUN, 0, 0, THING)}, 4},
            // só verificação (peso 0): estas vêm nos momentos próprios (nomes, sentimentos, não sei)
            {"o meu N chama NOME", {S(DET, 0, 0), S(POSS, 0, 0), S(NOUN, 0, 0, ANIMAL | OBJECT), S(CHAMA, -1, 0), S(NAME)}, 0},
            {"como chama o meu N", {L("como"), S(CHAMA, -1, 0), S(DET, 0, 0), S(POSS, 0, 0), S(NOUN, 0, 0, ANIMAL | OBJECT)}, 0},
            {"eu estou ADJ", {L("eu"), L("estou"), S(ADJ, -1, 0)}, 0},
            {"você está ADJ", {L("você"), S(EST, -1, 0), S(ADJ, -1, 0)}, 0},
            {"não sei", {L("não"), L("sei")}, 0},
        };
        // "você quer" e "é um N": singular
        for (auto& p : patterns_) if (p.name == "você quer COMIDA") p.slots[1].fixn = 's';
        for (auto& p : patterns_) if (p.name == "é um N") for (auto& sl : p.slots) sl.fixn = 's';
        for (auto& p : patterns_) if (p.name == "o meu N chama NOME" || p.name == "como chama o meu N" || p.name == "eu estou ADJ" || p.name == "você está ADJ")
            for (auto& sl : p.slots) sl.fixn = 's';
    }

    std::mt19937 rng_;
    std::vector<Entry> lex_;
    std::vector<Pattern> patterns_;
    std::vector<double> weight_;
    std::unordered_map<std::string, std::vector<int>> by_word_;
    std::set<std::string> held_;
};

}  // namespace corpus
