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
static eva2::Scene to_scene(const Grammar::Event& e) { return {e.agent, e.action, e.patient, e.place}; }

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

    Grammar home(7);
    std::vector<eva2::Scene> seen;  // todas as cenas que ela viu (para conferir respostas)
    std::set<std::pair<std::string, std::string>> pairs;  // quem já apareceu junto com quem
    auto run_childhood = [&](eva2::Eva2& e) {
        std::uniform_real_distribution<double> u(0, 1);
        std::mt19937 r(5);
        for (int done = 0; done < childhood;) {
            const int today = std::min(2000, childhood - done);
            for (int i = 0; i < today; ++i) {
                const double x = u(r);
                if (x < 0.35) { e.listen(home.sentence()); continue; }
                auto ev = home.random_event();
                const auto sc = to_scene(ev);
                e.witness(sc, home.describe(ev));
                seen.push_back(sc);
                pairs.insert({ev.agent, ev.patient});
                pairs.insert({ev.patient, ev.agent});
                if (x > 0.80) {
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
        const std::vector<eva2::Scene> options{{a, "vê", b, ""}, {b, "vê", a, ""}};
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
    for (int t = 0; t < tests; ++t) {
        auto ev = fresh.random_event();
        const auto sc = to_scene(ev);
        for (int cond = 0; cond < 2; ++cond) {
            const auto w = e.describe(sc, cond == 0);
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
    for (int t = 0; t < tests + 3; ++t) {
        auto ev = fresh.random_event();
        const auto sc = to_scene(ev);
        e.witness(sc, fresh.describe(ev));
        seen.push_back(sc);
        recent.push_back(ev);
        if (recent.size() < 4) continue;
        const auto& q_ev = recent[recent.size() - 4];  // a cena de 3 cenas atrás
        std::uniform_int_distribution<int> f(0, 2);
        int field = f(r);
        if (!fresh.can_ask(q_ev, field)) field = 0;
        const auto q = fresh.question(q_ev, field);
        // a verdade: o fato mais recente que responde (se o gato apareceu de novo depois, vale o mais novo)
        std::string truth;
        for (auto it = seen.rbegin(); it != seen.rend(); ++it) {
            if (field == 0 && (it->agent == q_ev.agent || it->patient == q_ev.agent)) { truth = it->place; break; }
            if (field == 1 && it->agent == q_ev.agent && it->action == q_ev.action && !it->patient.empty()) { truth = it->patient; break; }
            if (field == 2 && it->patient == q_ev.patient && it->action == q_ev.action) { truth = it->agent; break; }
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
    int thoughts = 0, true_thoughts = 0, gram_thoughts = 0;
    for (int t = 0; t < 40; ++t) {
        for (const auto& u : e.idle(6000)) {
            if (u.kind != eva2::Utterance::Kind::Thought) continue;
            ++thoughts;
            const bool gram = test.check(u.words).agreement;
            gram_thoughts += gram;
            bool truth = false;
            for (const auto& sc : seen) if (judge(u.words, sc)) { truth = true; break; }
            true_thoughts += gram && truth;
            if (verbose && thoughts <= 6) std::printf("  pensando alto: \"%s\" %s\n", join(u.words).c_str(), gram && truth ? "[verdade: ela viu isso]" : gram ? "[gramatical, mas não aconteceu assim]" : "[fora da gramática]");
        }
    }

    std::printf("\ninfância de %d momentos (%zu cenas vistas, %zu episódios na memória)\n", childhood, seen.size(), e.episodes().size());
    std::printf("1. compreensão (\"o A vê o B\": qual das duas cenas?), %d testes com pares que ela nunca viu juntos:\n", und_n);
    std::printf("     com a ordem aprendida: %.0f%%   | sem (papéis sorteados): %.0f%%   | acaso: 50%%\n", pct(und[0], und_n), pct(und[1], und_n));
    std::printf("2. descrição de cenas novas, %d cenas: certa (gramatical, papéis na ordem certa)\n", desc_n);
    std::printf("     com a ordem aprendida: %.0f%% (gramatical %.0f%%) | ordem sorteada: %.0f%% (gramatical %.0f%%)\n",
                pct(desc_ok[0], desc_n), pct(desc_gram[0], desc_n), pct(desc_ok[1], desc_n), pct(desc_gram[1], desc_n));
    std::printf("3. perguntas sobre a cena de 3 cenas atrás, %d perguntas: certa (gramatical e com o fato certo)\n", qa_n);
    std::printf("     com memória: %.0f%% (fato certo %.0f%%, gramatical %.0f%%, silêncio %.0f%%) | sem memória: %.0f%% (fato %.0f%%, gramatical %.0f%%)\n",
                pct(qa_ok[0], qa_n), pct(qa_fact[0], qa_n), pct(qa_gram[0], qa_n), pct(qa_silent[0], qa_n), pct(qa_ok[1], qa_n), pct(qa_fact[1], qa_n), pct(qa_gram[1], qa_n));
    std::printf("     por tipo, com memória: onde %.0f%% | o quê %.0f%% | quem %.0f%%\n",
                pct(per_field_ok[0], per_field_n[0]), pct(per_field_ok[1], per_field_n[1]), pct(per_field_ok[2], per_field_n[2]));
    std::printf("4. pensando alto (4 min de silêncio): %d falas, %.0f%% gramaticais, %.0f%% verdadeiras (descrevem algo que ela viu)\n",
                thoughts, pct(gram_thoughts, thoughts), pct(true_thoughts, thoughts));
}
