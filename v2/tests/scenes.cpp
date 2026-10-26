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

#include "corpus.hpp"
#include "eva2.hpp"

using corpus::Grammar;

static std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) s += (i ? " " : "") + v[i];
    return s;
}
static eva2::Scene to_scene(const Grammar::Event& e) {
    eva2::Scene s{e.agent, e.action, e.patient, e.place};
    s.agent_many = e.an == 'p';
    s.patient_many = e.pn == 'p';
    return s;
}

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
    auto perceive = [&](eva2::Eva2& ev2, const Grammar::Event& ev, const std::vector<std::string>& words, bool learn) {
        if (!eyes) return to_scene(ev);
        eva2::Scene s;
        s.agent = ev2.see_thing(eva2::draw_sprite(ev.agent, view_rng), learn, words);
        if (!ev.patient.empty()) s.patient = ev2.see_thing(eva2::draw_sprite(ev.patient, view_rng), learn, words);
        s.place = ev2.see_place(eva2::draw_backdrop(ev.place, view_rng), learn, words);
        s.action = ev.action;
        s.agent_many = ev.an == 'p';
        s.patient_many = ev.pn == 'p';
        return s;
    };
    std::vector<eva2::Scene> seen;  // todas as cenas que ela viu (para conferir respostas)
    std::set<std::pair<std::string, std::string>> pairs;  // quem já apareceu junto com quem
    auto run_childhood = [&](eva2::Eva2& e) {
        std::uniform_real_distribution<double> u(0, 1);
        std::mt19937 r(5);
        std::vector<std::pair<Grammar::Event, eva2::Scene>> past;  // cenas de antes (e como ela as viu), para conversar depois
        for (int done = 0; done < childhood;) {
            const int today = std::min(2000, childhood - done);
            for (int i = 0; i < today; ++i) {
                const double x = u(r);
                if (x < 0.35) { e.listen(home.sentence()); continue; }
                if (x >= 0.85 && past.size() > 30) {
                    // conversa sobre algo que já passou (a lembrança volta)
                    std::uniform_int_distribution<size_t> back(5, 30);
                    const auto& [ev, seen_as] = past[past.size() - back(r)];
                    if (x < 0.93) {
                        std::uniform_int_distribution<int> f(1, 2);
                        const int field = f(r);
                        if (home.can_ask(ev, field)) e.witness_dialogue(seen_as, home.question(ev, field, true), home.answer(ev, field, true), true);
                    } else {
                        e.recount(seen_as, home.describe(ev, true));
                    }
                    continue;
                }
                auto ev = home.random_event();
                const auto words = home.describe(ev);
                const auto sc = perceive(e, ev, words, true);
                e.witness(sc, words);
                seen.push_back(to_scene(ev));
                past.push_back({ev, sc});
                pairs.insert({ev.agent, ev.patient});
                pairs.insert({ev.patient, ev.agent});
                if (x >= 0.75) {
                    std::uniform_int_distribution<int> f(0, 2);
                    int field = f(r);
                    if (!home.can_ask(ev, field)) field = 0;
                    e.witness_dialogue(sc, home.question(ev, field), home.answer(ev, field));
                }
            }
            done += today;
            e.sleep(300);
            std::fprintf(stderr, "  infância: %d momentos\n", done);
        }
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
            const std::string ca = e.see_thing(eva2::draw_sprite(a, view_rng), false), cb = e.see_thing(eva2::draw_sprite(b, view_rng), false);
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

    if (eyes) {
        // conceitos que os olhos formaram: cada coisa vista de novo (sem palavra) cai no conceito dela?
        std::map<std::string, std::map<std::string, int>> owner;  // conceito -> coisa -> vezes
        std::map<std::string, std::string> majority;
        int consistent = 0, views = 0;
        for (const auto& t : thing_names) {
            std::map<std::string, int> votes;
            for (int v = 0; v < 3; ++v) votes[e.see_thing(eva2::draw_sprite(t, view_rng), false)]++;
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
}
