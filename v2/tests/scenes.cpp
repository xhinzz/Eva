// Eva 2 - medida de cenas: quem faz o quê, perguntas e respostas, lembranças.
//
// Infância: ela vê cenas da casa ("o gato come o peixe" acontecendo) e ouve
// alguém descrevê-las; às vezes alguém pergunta sobre a cena e responde
// ("cadê o gato?" "o gato está na sala"); o resto é fala comum da casa.
// Dorme a cada 2000 momentos. Depois, quatro testes:
//
//  1. COMPREENSÃO (olhar preferencial): "o gato vê o cachorro". Duas cenas: o
//     gato vendo o cachorro e o cachorro vendo o gato. Qual a frase descreve?
//     Só a ordem das palavras distingue (as mesmas palavras nas duas). Acaso: 50%.
//  2. DESCRIÇÃO: ela vê uma cena nova e descreve. Certa = gramatical, quem faz
//     antes da ação, o alvo (ou o lugar) depois, todos os papéis certos.
//  3. PERGUNTAS: ela vê cenas novas; perguntamos sobre uma de 3 cenas atrás
//     ("cadê o gato?", "o que o gato come?", "quem come o peixe?").
//     Certa = gramatical e com o fato certo (o lugar, o alvo, quem fez).
//  4. LEMBRANÇAS: em silêncio, ela pensa alto. O que ela diz é verdade (descreve
//     algo que ela viu)?
// Comparações no mesmo cérebro: com e sem a ordem aprendida; com e sem memória.
//
// Uso: scenes [momentos_da_infancia] [testes]
//   CACHE=arquivo  guarda/carrega o cérebro depois da infância
//   V=1            mostra exemplos
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "childhood.hpp"
#include "corpus.hpp"
#include "eva2.hpp"

using corpus::Grammar;

static std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) s += (i ? " " : "") + v[i];
    return s;
}
static eva2::Scene to_scene(const Grammar::Event& e) { return eva2::scene_of(e); }

int main(int argc, char** argv) {
    const int childhood = argc > 1 ? atoi(argv[1]) : 10000;
    const int tests = argc > 2 ? atoi(argv[2]) : 200;
    const bool verbose = getenv("V") != nullptr;
    const std::string cache = getenv("CACHE") ? getenv("CACHE") : "";
    eva2::Eva2Config c;
    c.world_enabled = false;
    c.diary_file = "/dev/null";
    c.memory_file = cache.empty() ? "/dev/null" : cache;
    c.imitate_above = 2.0f;
    if (getenv("CONCEPT")) c.concept_match = atof(getenv("CONCEPT"));
    if (getenv("LABEL")) c.label_match = atof(getenv("LABEL"));

    Grammar home(7);
    // EYES=1: as coisas e os lugares das cenas chegam pelos olhos (desenhos), e os
    // conceitos que ela usa são os que os olhos formam; senão, chegam como símbolos
    const bool eyes = getenv("EYES") != nullptr;
    std::vector<std::string> thing_names, place_names;
    {
        std::set<std::string> got;
        for (const auto& en : home.lexicon())
            if (en.pos == corpus::NOUN && en.n == 's' && !got.count(en.lemma)) { got.insert(en.lemma); (en.sem & corpus::PLACE ? place_names : thing_names).push_back(en.lemma); }
        if (eyes) eva2::set_sprite_names(thing_names);
    }
    std::mt19937 view_rng(101);
    // GLANCES: quantos olhares ela dá em cada coisa durante uma cena (os olhos se mexem)
    const int glances = getenv("GLANCES") ? atoi(getenv("GLANCES")) : 3;
    auto look = [&](const std::string& lemma) {
        std::vector<eva2::Image> g;
        for (int k = 0; k < glances; ++k) g.push_back(eva2::draw_sprite(lemma, view_rng));
        return g;
    };
    auto perceive = [&](eva2::Eva2& ev2, const Grammar::Event& ev, const std::vector<std::string>& words, bool learn) {
        if (!eyes) return to_scene(ev);
        eva2::Scene s;
        s.agent = ev2.see_thing(look(ev.agent), learn, words);
        if (!ev.patient.empty()) s.patient = ev2.see_thing(look(ev.patient), learn, words);
        {
            std::vector<eva2::Image> g;
            for (int k = 0; k < glances; ++k) g.push_back(eva2::draw_backdrop(ev.place, view_rng));
            s.place = ev2.see_place(g, learn, words);
        }
        s.action = ev.action;
        s.agent_many = ev.an == 'p';
        s.patient_many = ev.pn == 'p';
        s.valence = ev.valence;
        return s;
    };
    std::vector<eva2::Scene> seen;  // todas as cenas que ela viu (para conferir respostas)
    std::set<std::pair<std::string, std::string>> pairs;  // quem já apareceu junto com quem
    auto see_thing = [&](eva2::Eva2& ev2, const std::string& lemma, const std::vector<std::string>& words) {
        return eyes ? ev2.see_thing(look(lemma), true, words) : lemma;
    };
    eva2::Childhood life(home, 5, perceive, see_thing);
    auto run_childhood = [&](eva2::Eva2& e) {
        life.live(e, childhood, [](int, int, int done) { std::fprintf(stderr, "  infância: %d momentos\n", done); });
        for (const auto& sc : life.seen) seen.push_back(sc);
        pairs = life.pairs;
    };
    eva2::Eva2 e(c, 1);
    if (cache.empty() || !e.load()) {
        if (eyes) {
            // infância dos olhos: ver as coisas e os lugares da casa, sem nomes
            std::vector<eva2::Image> samples;
            std::mt19937 rr(3);
            for (int k = 0; k < 2; ++k) {
                for (const auto& t : thing_names) samples.push_back(eva2::draw_sprite(t, rr));
                for (const auto& p : place_names) samples.push_back(eva2::draw_backdrop(p, rr));
            }
            e.open_eyes(samples);
            std::fprintf(stderr, "  olhos abertos (%zu imagens)\n", samples.size());
        }
        run_childhood(e);
        if (!cache.empty()) e.save();
    } else {
        for (const auto& ep : e.episodes().all()) { seen.push_back(ep.scene); pairs.insert({ep.scene.agent, ep.scene.patient}); }
    }
    auto pct = [](int a, int b) { return b ? 100.0 * a / b : 0.0; };

    // o que ela aprendeu sobre a ordem e as perguntas
    std::printf("ordem aprendida (posição média entre as palavras com sentido, 0 = primeira):");
    for (int r = 0; r < eva2::kRoles; ++r) std::printf("  %s %.2f", eva2::role_name(r), e.role_order().expected_slot(r));
    std::printf("\no que cada palavra de pergunta pede:");
    for (const char* w : {"cadê", "que", "quem", "o"}) {
        auto it = e.question_words().counts().find(w);
        if (it == e.question_words().counts().end()) continue;
        double tot = 0; for (double x : it->second) tot += x;
        int best = 0; for (int r = 1; r < eva2::kRoles; ++r) if (it->second[r] > it->second[best]) best = r;
        std::printf("  '%s' -> %s (%.0f%%)", w, eva2::role_name(best), 100.0 * it->second[best] / tot);
    }
    std::printf("\n");

    // ------------------------------------------------ 1. compreensão
    Grammar test(31);
    int und[2] = {0, 0}, und_n = 0;
    std::vector<std::string> anim;
    for (const auto& en : test.lexicon()) if (en.pos == corpus::NOUN && (en.sem & corpus::ANIM) && en.n == 's') anim.push_back(en.lemma);
    std::mt19937 r(11);
    std::uniform_int_distribution<size_t> pa(0, anim.size() - 1);
    for (int t = 0; t < tests; ++t) {
        std::string a, b;
        do { a = anim[pa(r)]; b = anim[pa(r)]; } while (a == b || pairs.count({a, b}));
        Grammar::Event ev; ev.kind = Grammar::SEE; ev.agent = a; ev.patient = b; ev.action = "vê";
        const auto words = test.describe(ev);
        std::vector<eva2::Scene> options{{a, "vê", b, ""}, {b, "vê", a, ""}};
        if (eyes) {  // as duas cenas vistas pelos olhos (sem palavras)
            const std::string ca = e.see_thing(look(a), false), cb = e.see_thing(look(b), false);
            options = {{ca, "vê", cb, ""}, {cb, "vê", ca, ""}};
        }
        for (int cond = 0; cond < 2; ++cond) {
            e.set_role_order_in_understanding(cond == 0);
            und[cond] += e.understand(words, options) == 0;
        }
        ++und_n;
        if (verbose && t < 4) std::printf("  compreensão: \"%s\" -> ela escolhe: %s\n", join(words).c_str(),
                                         (e.set_role_order_in_understanding(true), e.understand(words, options) == 0) ? "a cena certa" : "a cena trocada");
    }
    e.set_role_order_in_understanding(true);

    // ------------------------------------------------ 2. descrição
    int desc_ok[2] = {0, 0}, desc_gram[2] = {0, 0}, desc_n = 0;
    auto judge = [&](const std::vector<std::string>& w, const eva2::Scene& sc) {
        // a primeira palavra-substantivo é quem faz? a ação aparece depois? o alvo/lugar depois da ação?
        int first_noun = -1, act = -1, last = -1;
        for (size_t i = 0; i < w.size(); ++i) {
            const int pos = test.lex_pos(w[i]);
            const std::string lem = test.lemma_of(w[i]);
            if (pos == corpus::NOUN && first_noun < 0) first_noun = static_cast<int>(i);
            if (act < 0 && (lem == sc.action || (sc.action == "está" && (w[i] == "está" || w[i] == "estão")) ||
                            (sc.action == "come" && w[i] == "comem") || (sc.action == "vê" && w[i] == "veem") ||
                            (sc.action == "gosta" && w[i] == "gostam") || test.lemma_of(w[i]) == sc.action)) act = static_cast<int>(i);
            const std::string target = !sc.patient.empty() ? sc.patient : (sc.action == "está" ? sc.place : "");
            if (!target.empty() && lem == target) last = static_cast<int>(i);
        }
        const bool agent_first = first_noun >= 0 && test.lemma_of(w[first_noun]) == sc.agent;
        const bool needs_target = !sc.patient.empty() || sc.action == "está";
        return agent_first && act > first_noun && (!needs_target || last > act);
    };
    Grammar fresh(57);
    // quantos: a forma de quem faz e a do alvo combinam com um/muitos?
    auto number_ok = [&](const std::vector<std::string>& w, const eva2::Scene& sc) {
        bool agent_ok = false, patient_ok = sc.patient.empty();
        bool seen_agent = false;
        for (const auto& x : w) {
            if (test.lex_pos(x) != corpus::NOUN) continue;
            const std::string lem = test.lemma_of(x);
            const char n = test.number_of(x);
            if (!seen_agent && lem == sc.agent) { agent_ok = (n == 'p') == sc.agent_many; seen_agent = true; }
            else if (!sc.patient.empty() && lem == sc.patient) patient_ok = (n == 'p') == sc.patient_many;
        }
        return agent_ok && patient_ok;
    };
    auto verb_tense = [&](const std::vector<std::string>& w) {
        for (const auto& x : w) { const char t = test.tense_of(x); if (t != '*') return t; }
        return '*';
    };
    int num_ok[2] = {0, 0}, num_n = 0, present_ok = 0;
    for (int t = 0; t < tests; ++t) {
        auto ev = fresh.random_event();
        const auto sc = to_scene(ev);
        const auto sc_seen = perceive(e, ev, {}, false);  // como ela vê a cena (sem ninguém dizer nada)
        {   // quantidade: com e sem
            for (int cond = 0; cond < 2; ++cond) {
                e.set_number_in_speech(cond == 0);
                const auto w = e.describe(sc_seen, true);
                if (test.check(w).agreement && judge(w, sc)) { num_ok[cond] += number_ok(w, sc); if (cond == 0) { ++num_n; present_ok += verb_tense(w) == 'p'; } }
            }
            e.set_number_in_speech(true);
        }
        for (int cond = 0; cond < 2; ++cond) {
            const auto w = e.describe(sc_seen, cond == 0);
            const bool gram = test.check(w).agreement;
            desc_gram[cond] += gram;
            desc_ok[cond] += gram && judge(w, sc);
            if (verbose && cond == 0 && t < 8)
                std::printf("  descrição: cena [%s %s %s%s] -> \"%s\" %s\n", sc.agent.c_str(), sc.action.c_str(), sc.patient.c_str(),
                            sc.action == "está" ? (" em " + sc.place).c_str() : "", join(w).c_str(), gram && judge(w, sc) ? "[certa]" : "[errada]");
        }
        ++desc_n;
    }

    // ------------------------------------------------ 3. perguntas
    int qa_ok[2] = {0, 0}, qa_fact[2] = {0, 0}, qa_gram[2] = {0, 0}, qa_silent[2] = {0, 0}, qa_n = 0;
    int per_field_ok[3] = {0, 0, 0}, per_field_n[3] = {0, 0, 0};
    std::vector<Grammar::Event> recent;
    const char* field_name[] = {"onde", "o quê", "quem"};
    int tense_ok[2] = {0, 0}, tense_n = 0, shown_past = 0;
    for (int t = 0; t < tests + 3; ++t) {
        auto ev = fresh.random_event();
        const auto sc = to_scene(ev);
        const auto words = fresh.describe(ev);
        e.witness(perceive(e, ev, words, true), words);
        seen.push_back(sc);
        recent.push_back(ev);
        if (recent.size() < 4) continue;
        const auto& q_ev = recent[recent.size() - 4];  // a cena de 3 cenas atrás
        std::uniform_int_distribution<int> f(0, 2);
        int field = f(r);
        if (!fresh.can_ask(q_ev, field)) field = 0;
        const bool ask_past = field != 0 && (t % 2 == 0);  // metade das perguntas no passado ("o que o gato comeu?")
        const auto q = fresh.question(q_ev, field, ask_past);
        // a verdade: o fato mais recente que responde (se o gato apareceu de novo depois, vale o mais novo)
        std::string truth;
        for (auto it = seen.rbegin(); it != seen.rend(); ++it) {
            if (field == 0 && (it->agent == q_ev.agent || it->patient == q_ev.agent)) { truth = it->place; break; }
            if (field == 1 && it->agent == q_ev.agent && it->action == q_ev.action && !it->patient.empty()) { truth = it->patient; break; }
            if (field == 2 && it->patient == q_ev.patient && it->action == q_ev.action) { truth = it->agent; break; }
        }
        if (field != 0) {
            // tempo: a resposta vem no tempo da pergunta? (com e sem o tempo aprendido)
            for (int cond = 0; cond < 2; ++cond) {
                e.set_tense_in_speech(cond == 0);
                const auto a = e.answer(q);
                const char vt = verb_tense(a);
                if (vt != '*') { tense_ok[cond] += (vt == 'a') == ask_past; if (cond == 0) ++tense_n; }
                if (verbose && cond == 0 && ask_past && shown_past < 4) { ++shown_past; std::printf("  pergunta no passado: \"%s?\" -> \"%s\"\n", join(q).c_str(), join(a).c_str()); }
            }
            e.set_tense_in_speech(true);
        }
        for (int cond = 0; cond < 2; ++cond) {
            e.set_memory_in_answers(cond == 0);
            const auto a = e.answer(q);
            bool fact = false;
            for (const auto& w : a) if (test.lemma_of(w) == truth) fact = true;
            const bool gram = test.check(a).agreement;
            qa_silent[cond] += a.empty();
            qa_fact[cond] += fact;
            qa_gram[cond] += gram;
            qa_ok[cond] += fact && gram;
            if (cond == 0) { per_field_ok[field] += fact && gram; ++per_field_n[field]; }
            if (verbose && cond == 0 && qa_n < 10)
                std::printf("  pergunta (%s, cena de 3 atrás): \"%s?\" -> \"%s\" %s (verdade: %s)\n", field_name[field], join(q).c_str(), join(a).c_str(),
                            fact && gram ? "[certa]" : fact ? "[fato certo, frase errada]" : "[errada]", truth.c_str());
        }
        e.set_memory_in_answers(true);
        ++qa_n;
    }

    // ------------------------------------------------ 4. lembranças espontâneas
    int thoughts = 0, true_thoughts = 0, gram_thoughts = 0, past_thoughts = 0, verb_thoughts = 0;
    for (int t = 0; t < 40; ++t) {
        for (const auto& u : e.idle(6000)) {
            if (u.kind != eva2::Utterance::Kind::Thought) continue;
            ++thoughts;
            const bool gram = test.check(u.words).agreement;
            const char vt = verb_tense(u.words);
            if (vt != '*') { ++verb_thoughts; past_thoughts += vt == 'a'; }
            gram_thoughts += gram;
            bool truth = false;
            for (const auto& sc : seen) if (judge(u.words, sc)) { truth = true; break; }
            true_thoughts += gram && truth;
            if (verbose && thoughts <= 6) std::printf("  pensando alto: \"%s\" %s\n", join(u.words).c_str(), gram && truth ? "[verdade: ela viu isso]" : gram ? "[gramatical, mas não aconteceu assim]" : "[fora da gramática]");
        }
    }


    // ------------------------------------------------ 5. sim ou não (a cena de 3 cenas atrás; metade das perguntas é falsa)
    // A verdade é o fato mais recente (como nas perguntas). Certa: "sim" quando é
    // verdade; "não" + a frase com o fato certo quando não é.
    auto first_is = [](const std::vector<std::string>& a, const char* w) { return !a.empty() && a[0] == w; };
    auto rest_of = [](const std::vector<std::string>& a) { return a.size() > 1 ? std::vector<std::string>(a.begin() + 1, a.end()) : std::vector<std::string>{}; };
    int yn_judge[2] = {0, 0}, yn_full[2] = {0, 0}, yn_n = 0, yn_dontknow = 0, shown_yn = 0;
    {
        std::vector<Grammar::Event> rec;
        for (int t = 0; t < tests + 3; ++t) {
            auto ev = fresh.random_event();
            const auto words = fresh.describe(ev);
            e.witness(perceive(e, ev, words, true), words);
            seen.push_back(to_scene(ev));
            rec.push_back(ev);
            if (rec.size() < 4) continue;
            const auto& q_ev = rec[rec.size() - 4];
            // a verdade de agora sobre quem fez aquilo
            Grammar::Event truth = q_ev;
            for (auto it = seen.rbegin(); it != seen.rend(); ++it) {
                if (q_ev.kind == Grammar::AT && (it->agent == q_ev.agent || it->patient == q_ev.agent)) { truth.place = it->place; break; }
                if (q_ev.kind != Grammar::AT && it->agent == q_ev.agent && it->action == q_ev.action) { truth.patient = it->patient; break; }
            }
            const bool yes = (t % 2 == 0) || !fresh.can_alter(truth);
            const Grammar::Event claim = yes ? truth : fresh.altered(truth);
            const bool past = q_ev.kind != Grammar::AT && t % 4 == 1;
            const auto q = fresh.yes_no(claim, past);
            const std::string target = truth.kind == Grammar::AT ? truth.place : truth.patient;
            for (int cond = 0; cond < 2; ++cond) {
                e.set_comparator(cond == 0);
                const auto a = e.answer(q);
                const bool judged = first_is(a, yes ? "sim" : "não") && !(a.size() > 1 && a[1] == "sei");
                bool fact = target.empty();
                for (const auto& w : rest_of(a)) if (test.lemma_of(w) == target) fact = true;
                yn_judge[cond] += judged;
                yn_full[cond] += judged && fact && test.check(rest_of(a)).agreement;
                if (cond == 0) yn_dontknow += a.size() == 2 && a[0] == "não" && a[1] == "sei";
                if (verbose && cond == 0 && shown_yn < 6) { ++shown_yn; std::printf("  sim ou não: \"%s\" (%s) -> \"%s\"\n", join(q).c_str(), yes ? "é verdade" : ("não: era " + target).c_str(), join(a).c_str()); }
            }
            e.set_comparator(true);
            ++yn_n;
        }
    }

    // ------------------------------------------------ 6. não sei: perguntas sobre coisas que ela só conhece dos livros
    int dk_ok = 0, dk_n = 0, dk_false = 0, dk_known_n = 0;
    {
        auto only = test.book_things();
        for (size_t i = 0; i < only.size() && dk_n < 40; ++i) {
            Grammar::Event ev; ev.kind = Grammar::AT; ev.agent = only[i]; ev.place = "casa";
            const auto q = test.question(ev, 0);  // "cadê a girafa"
            const auto a = e.answer(q);
            dk_ok += a.size() == 2 && a[0] == "não" && a[1] == "sei";
            ++dk_n;
            if (verbose && i < 3) std::printf("  não sei: \"%s?\" -> \"%s\"\n", join(q).c_str(), join(a).c_str());
        }
        // e coisas que ela viu: não deve dizer "não sei"
        for (size_t i = seen.size() - 40; i < seen.size(); ++i) {
            Grammar::Event ev; ev.kind = Grammar::AT; ev.agent = seen[i].agent; ev.place = "casa"; ev.an = 's';
            const auto a = e.answer(test.question(ev, 0));
            dk_false += a.size() == 2 && a[0] == "não" && a[1] == "sei";
            ++dk_known_n;
        }
    }

    // ------------------------------------------------ 7. negação: "o leão não está na sala": qual das duas cenas?
    int neg_ok[2] = {0, 0}, neg_n = 0;
    for (int t = 0; t < tests; ++t) {
        auto ev = fresh.random_event();
        if (!fresh.can_alter(ev) || ev.kind == Grammar::LIKE) continue;
        const auto other = fresh.altered(ev);
        const auto words = fresh.negated(other);  // diz que NÃO é a outra
        std::vector<eva2::Scene> options{to_scene(other), to_scene(ev)};  // a certa é a segunda
        if (eyes) continue;
        for (int cond = 0; cond < 2; ++cond) {
            e.set_comparator(cond == 0);
            neg_ok[cond] += e.understand(words, options) == 1;
        }
        e.set_comparator(true);
        if (verbose && neg_n < 3) std::printf("  negação: \"%s\" -> ela escolhe a cena %s\n", join(words).c_str(), e.understand(words, options) == 1 ? "certa" : "errada");
        ++neg_n;
    }

    if (!cache.empty()) e.load();  // cada teste novo parte do mesmo cérebro, o da infância (com CACHE)
    // ------------------------------------------------ 8. fatos contados (ela não viu): "o urso está na praia" ... "cadê o urso?"
    int told_ok[2] = {0, 0}, told_n = 0;
    {
        Grammar tales(91);
        for (int cond = 0; cond < 2; ++cond) {
            e.set_store_heard(cond == 0);
            std::vector<Grammar::Event> rec;
            for (int t = 0; t < tests / 2 + 3; ++t) {
                auto ev = tales.random_event();
                e.hear(tales.describe(ev));  // você conta (ela não vê)
                rec.push_back(ev);
                if (rec.size() < 4) continue;
                const auto& q_ev = rec[rec.size() - 4];
                std::uniform_int_distribution<int> f(0, 2);
                int field = f(r);
                if (!tales.can_ask(q_ev, field)) field = 0;
                // a verdade: o mais recente que você contou sobre aquilo
                std::string truth;
                for (auto it = rec.rbegin(); it != rec.rend(); ++it) {
                    if (field == 0 && it->kind == Grammar::AT && it->agent == q_ev.agent) { truth = it->place; break; }
                    if (field == 1 && it->agent == q_ev.agent && it->action == q_ev.action) { truth = it->patient; break; }
                    if (field == 2 && it->patient == q_ev.patient && it->action == q_ev.action) { truth = it->agent; break; }
                }
                if (field == 0 && q_ev.kind != Grammar::AT) continue;  // contar "o urso come a uva" não diz onde o urso está
                if (truth.empty()) continue;
                const auto a = e.answer(tales.question(q_ev, field));
                bool fact = false;
                for (const auto& w : a) if (test.lemma_of(w) == truth) fact = true;
                told_ok[cond] += fact && test.check(a).agreement;
                if (cond == 0) ++told_n;
                if (verbose && cond == 0 && told_n <= 3) std::printf("  fato contado: \"%s?\" -> \"%s\" (você contou: %s)\n", join(tales.question(q_ev, field)).c_str(), join(a).c_str(), truth.c_str());
            }
        }
        e.set_store_heard(true);
    }

    if (!cache.empty()) e.load();  // cada teste novo parte do mesmo cérebro, o da infância (com CACHE)
    // ------------------------------------------------ 9. nomes que você conta, e meu/seu ("o meu gato chama tico" ... "como chama o meu gato?")
    int nm_name[2] = {0, 0}, nm_poss[2] = {0, 0}, nm_full[2] = {0, 0}, nm_n = 0;
    {
        auto names = Grammar::pet_names();
        std::vector<std::string> new_names(names.begin() + eva2::Childhood::kChildhoodNames, names.end());  // nomes que ela nunca ouviu
        auto things = test.nameable();
        std::mt19937 rn(77);
        std::shuffle(things.begin(), things.end(), rn);
        for (int cond = 0; cond < 2; ++cond) {
            e.set_perspective(cond == 0);
            for (size_t i = 0; i < 40 && i < things.size(); ++i) {
                const std::string& thing = things[i];
                const std::string& name = new_names[i % new_names.size()];
                const bool yours = i % 2 == 0;  // metade: uma coisa sua ("o meu ..."); metade: uma coisa dela ("o seu ...")
                e.hear(test.named(thing, yours, name));
                e.hear(fresh.describe(fresh.random_event()));  // outras conversas no meio
                e.hear(fresh.describe(fresh.random_event()));
                const auto a = e.answer(test.ask_name(thing, yours));
                const auto expect = test.named(thing, !yours, name);  // de quem é, visto do lado dela
                bool has_name = false;
                for (const auto& w : a) if (w == name) has_name = true;
                const bool poss = a.size() > 1 && a[1] == expect[1];
                nm_name[cond] += has_name;
                nm_poss[cond] += poss;
                nm_full[cond] += a == expect;
                if (cond == 0) ++nm_n;
                if (verbose && cond == 0 && i < 4) std::printf("  nome: você disse \"%s\"; \"%s?\" -> \"%s\"\n", join(test.named(thing, yours, name)).c_str(), join(test.ask_name(thing, yours)).c_str(), join(a).c_str());
            }
        }
        e.set_perspective(true);
    }

    if (!cache.empty()) e.load();  // cada teste novo parte do mesmo cérebro, o da infância (com CACHE)
    // ------------------------------------------------ 10. sentimentos
    // (a) ela vê alguém chorar ou rir (ela sente), e você pergunta "como você está?"
    // (b) "você está feliz?" / "você está triste?"
    // (c) lembrar colore o humor: uma cena triste ou alegre com X, outras cenas,
    //     o humor volta ao neutro, e você pergunta "cadê o X?": lembrar de X mexe com ela?
    int fe_ok[2] = {0, 0}, fe_n = 0, fyn_ok = 0, fyn_n = 0, mem_toward[2] = {0, 0}, mem_n = 0;
    double mem_shift[2] = {0, 0};
    {
        std::vector<std::string> good{"brinca", "ri", "dança", "canta", "pula", "sorri"}, bad{"chora", "cai", "grita", "tosse"};
        std::mt19937 rf(13);
        std::vector<std::string> people;
        for (const auto& en : test.lexicon()) if (en.pos == corpus::NOUN && (en.sem & corpus::ANIM) && en.n == 's' && !Grammar::book_only(en.lemma)) people.push_back(en.lemma);
        std::uniform_int_distribution<size_t> pp(0, people.size() - 1);
        for (int t = 0; t < 60; ++t) {
            const bool happy = t % 2 == 0;
            Grammar::Event ev; ev.kind = Grammar::INTRANS; ev.agent = people[pp(rf)]; ev.place = "sala";
            ev.action = happy ? good[t % good.size()] : bad[t % bad.size()];
            ev.valence = happy ? 0.8f : -0.8f;
            const auto words = test.describe(ev);
            e.set_feeling(0.0f);
            e.witness(perceive(e, ev, words, true), words);
            seen.push_back(to_scene(ev));
            const float now = e.feeling();
            for (int cond = 0; cond < 2; ++cond) {
                e.set_interoception_in_speech(cond == 0);
                e.set_feeling(now);
                if (getenv("DBG10") && t < 2 && cond == 0) setenv("EVA2_GDEBUG", "1", 1);
                const auto a = e.answer(Grammar::ask_feeling());
                if (getenv("DBG10")) unsetenv("EVA2_GDEBUG");
                bool right = false;
                for (const auto& w : a) if (w == (happy ? "feliz" : "triste")) right = true;
                fe_ok[cond] += right && test.check(a).agreement;
                if (verbose && cond == 0 && t < 2) std::printf("  sentimento: ela viu \"%s\"; \"como você está?\" -> \"%s\"\n", join(words).c_str(), join(a).c_str());
            }
            e.set_interoception_in_speech(true);
            ++fe_n;
            // (b)
            e.set_feeling(now);
            const bool claim = t % 3 == 0;
            auto q = Grammar::feeling(false, claim);
            q.push_back("<?>");
            const auto a = e.answer(q);
            fyn_ok += first_is(a, claim == happy ? "sim" : "não") && !(a.size() > 1 && a[1] == "sei");
            ++fyn_n;
            if (verbose && t < 2) std::printf("  sentimento: \"%s\" -> \"%s\"\n", join(q).c_str(), join(a).c_str());
        }
        // (c)
        for (int t = 0; t < 60; ++t) {
            const bool happy = t % 2 == 0;
            Grammar::Event ev; ev.kind = Grammar::INTRANS; ev.agent = people[pp(rf)]; ev.place = "quarto";
            ev.action = happy ? good[t % good.size()] : bad[t % bad.size()];
            ev.valence = happy ? 0.8f : -0.8f;
            const auto words = test.describe(ev);
            e.witness(perceive(e, ev, words, true), words);
            seen.push_back(to_scene(ev));
            for (int k = 0; k < 4; ++k) {  // outras cenas, sem X
                auto other = fresh.random_event();
                if (other.agent == ev.agent || other.patient == ev.agent) continue;
                other.valence = 0.0f;
                const auto ow = fresh.describe(other);
                e.witness(perceive(e, other, ow, true), ow);
                seen.push_back(to_scene(other));
            }
            Grammar::Event at; at.kind = Grammar::AT; at.agent = ev.agent; at.place = "casa";
            for (int cond = 0; cond < 2; ++cond) {
                e.set_valence_in_memory(cond == 0);
                e.set_feeling(0.0f);
                e.answer(test.question(at, 0));  // "cadê o X?"
                const float d = e.feeling();
                mem_toward[cond] += happy ? d > 0.05f : d < -0.05f;
                mem_shift[cond] += happy ? d : -d;
            }
            e.set_valence_in_memory(true);
            ++mem_n;
        }
    }

    // ------------------------------------------------ 11. frases que terminam: com e sem o fechamento pelo plano
    // "emendou": um começo da frase já era uma frase completa e certa, e ela continuou
    int end_ok[2] = {0, 0}, end_runon[2] = {0, 0}, end_n = 0;
    auto runon = [&](const std::vector<std::string>& a) {
        if (test.check(a).agreement) return false;
        for (size_t k = 2; k < a.size(); ++k) if (test.check(std::vector<std::string>(a.begin(), a.begin() + k)).agreement) return true;
        return false;
    };
    {
        Grammar ends(23);
        std::vector<Grammar::Event> rec;
        for (int t = 0; t < tests + 1; ++t) {
            auto ev = ends.random_event();
            const auto words = ends.describe(ev);
            const auto sc = to_scene(ev);
            e.witness(perceive(e, ev, words, true), words);
            seen.push_back(sc);
            rec.push_back(ev);
            if (rec.size() < 2) continue;
            const auto& q_ev = rec[rec.size() - 2];
            std::uniform_int_distribution<int> f(0, 2);
            int field = f(r);
            if (!ends.can_ask(q_ev, field)) field = 0;
            for (int cond = 0; cond < 2; ++cond) {
                e.set_plan_closure(cond == 0);
                const auto d = e.describe(perceive(e, ev, {}, false), true);
                const auto a = e.answer(ends.question(q_ev, field, field != 0 && t % 2 == 0));  // metade no passado
                end_ok[cond] += (test.check(d).agreement && judge(d, sc)) + test.check(a).agreement;
                end_runon[cond] += runon(d) + runon(a);
            }
            e.set_plan_closure(true);
            end_n += 2;
        }
    }

    if (eyes) {
        // conceitos que os olhos formaram: cada coisa vista de novo (sem palavra) cai no conceito dela?
        std::map<std::string, std::map<std::string, int>> owner;  // conceito -> coisa -> vezes
        std::map<std::string, std::string> majority;
        int consistent = 0, views = 0;
        for (const auto& t : thing_names) {
            std::map<std::string, int> votes;
            for (int v = 0; v < 3; ++v) votes[e.see_thing(look(t), false)]++;
            int best = 0; for (const auto& [k, n] : votes) if (n > best) { best = n; majority[t] = k; }
            consistent += best; views += 3;
            for (const auto& [k, n] : votes) owner[k][t] += n;
        }
        int own = 0;
        for (const auto& t : thing_names) { int owners = 0; for (const auto& [tt, n] : owner[majority[t]]) owners += n >= 2; own += owners <= 1; }
        std::printf("olhos: %zu conceitos de coisas e %zu de lugares formados (para %zu coisas e %zu lugares) | a mesma coisa vista 3 vezes cai no mesmo conceito: %.0f%% | coisas com um conceito só delas: %.0f%%\n",
                    e.thing_concepts(), e.place_concepts(), thing_names.size(), place_names.size(), pct(consistent, views), pct(own, static_cast<int>(thing_names.size())));
    }
    std::printf("\ninfância de %d momentos (%zu cenas vistas, %zu episódios na memória)%s\n", childhood, seen.size(), e.episodes().size(), eyes ? " — cenas vistas pelos olhos" : "");
    std::printf("1. compreensão (\"o A vê o B\": qual das duas cenas?), %d testes com pares que ela nunca viu juntos:\n", und_n);
    std::printf("     com a ordem aprendida: %.0f%%   | sem (papéis sorteados): %.0f%%   | acaso: 50%%\n", pct(und[0], und_n), pct(und[1], und_n));
    std::printf("2. descrição de cenas novas, %d cenas: certa (gramatical, papéis na ordem certa)\n", desc_n);
    std::printf("     com a ordem aprendida: %.0f%% (gramatical %.0f%%) | ordem sorteada: %.0f%% (gramatical %.0f%%)\n",
                pct(desc_ok[0], desc_n), pct(desc_gram[0], desc_n), pct(desc_ok[1], desc_n), pct(desc_gram[1], desc_n));
    std::printf("   quantidade (entre as descrições certas): um/muitos certo para quem faz e para o alvo: %.0f%% | sem a quantidade aprendida: %.0f%%   (%d descrições; verbo no presente: %.0f%%)\n",
                pct(num_ok[0], num_n), pct(num_ok[1], num_n), num_n, pct(present_ok, num_n));
    std::printf("3. perguntas sobre a cena de 3 cenas atrás, %d perguntas: certa (gramatical e com o fato certo)\n", qa_n);
    std::printf("     com memória: %.0f%% (fato certo %.0f%%, gramatical %.0f%%, silêncio %.0f%%) | sem memória: %.0f%% (fato %.0f%%, gramatical %.0f%%)\n",
                pct(qa_ok[0], qa_n), pct(qa_fact[0], qa_n), pct(qa_gram[0], qa_n), pct(qa_silent[0], qa_n), pct(qa_ok[1], qa_n), pct(qa_fact[1], qa_n), pct(qa_gram[1], qa_n));
    std::printf("     por tipo, com memória: onde %.0f%% | o quê %.0f%% | quem %.0f%%\n",
                pct(per_field_ok[0], per_field_n[0]), pct(per_field_ok[1], per_field_n[1]), pct(per_field_ok[2], per_field_n[2]));
    std::printf("   tempo: a resposta vem no tempo da pergunta (\"o que o gato come/comeu?\"): %.0f%% | sem o tempo aprendido: %.0f%%   (%d respostas com verbo)\n",
                pct(tense_ok[0], tense_n), pct(tense_ok[1], tense_n), tense_n);
    std::printf("4. pensando alto (4 min de silêncio): %d falas, %.0f%% gramaticais, %.0f%% verdadeiras (descrevem algo que ela viu), %.0f%% no passado (das que têm verbo)\n",
                thoughts, pct(gram_thoughts, thoughts), pct(true_thoughts, thoughts), pct(past_thoughts, verb_thoughts));
    std::printf("5. sim ou não sobre a cena de 3 cenas atrás, %d perguntas (metade falsas):\n", yn_n);
    std::printf("     com o comparador: sim/não certo %.0f%%, e com a correção certa e gramatical %.0f%% (disse \"não sei\" %.0f%%) | sem o comparador: %.0f%% / %.0f%%\n",
                pct(yn_judge[0], yn_n), pct(yn_full[0], yn_n), pct(yn_dontknow, yn_n), pct(yn_judge[1], yn_n), pct(yn_full[1], yn_n));
    std::printf("6. não sei: \"cadê a X?\" para coisas que ela só viu em livro: \"não sei\" em %.0f%% (%d) | para coisas que ela viu, disse \"não sei\" em %.0f%% (%d)\n",
                pct(dk_ok, dk_n), dk_n, pct(dk_false, dk_known_n), dk_known_n);
    std::printf("7. negação (\"o leão não está na sala\": qual das duas cenas?), %d testes: entendendo o \"não\" %.0f%% | sem o comparador %.0f%%\n",
                neg_n, pct(neg_ok[0], neg_n), pct(neg_ok[1], neg_n));
    std::printf("8. fatos contados (ela não viu), pergunta 3 fatos depois, %d perguntas: certa %.0f%% | sem guardar o que ouve: %.0f%%\n",
                told_n, pct(told_ok[0], told_n), pct(told_ok[1], told_n));
    std::printf("9. nomes contados por você (nomes que ela nunca ouviu), %d: o nome certo %.0f%%, meu/seu do lado certo %.0f%%, a frase exata %.0f%% | sem trocar a perspectiva: %.0f%% / %.0f%% / %.0f%%\n",
                nm_n, pct(nm_name[0], nm_n), pct(nm_poss[0], nm_n), pct(nm_full[0], nm_n), pct(nm_name[1], nm_n), pct(nm_poss[1], nm_n), pct(nm_full[1], nm_n));
    std::printf("10. sentimentos: depois de ver alguém rir ou chorar, \"como você está?\" certo %.0f%% (sem interocepção %.0f%%), %d; \"você está feliz/triste?\" sim/não certo %.0f%%\n",
                pct(fe_ok[0], fe_n), pct(fe_ok[1], fe_n), fe_n, pct(fyn_ok, fyn_n));
    std::printf("    lembrar colore o humor (\"cadê o X?\" depois de X rir ou chorar e de 4 outras cenas): o humor vai para o lado da lembrança em %.0f%% (média %.2f) | sem a valência na memória: %.0f%% (%.2f), %d\n",
                pct(mem_toward[0], mem_n), mem_n ? mem_shift[0] / mem_n : 0.0, pct(mem_toward[1], mem_n), mem_n ? mem_shift[1] / mem_n : 0.0, mem_n);
    std::printf("11. frases que terminam (%d falas: descrições e respostas): certas %.0f%%, emendaram %.0f%% | sem o fechamento pelo plano: certas %.0f%%, emendaram %.0f%%\n",
                end_n, pct(end_ok[0], end_n), pct(end_runon[0], end_n), pct(end_ok[1], end_n), pct(end_runon[1], end_n));
}
