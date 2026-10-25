// Eva 2 - medida de gramática emergente.
//
// Ela ouve, dia após dia, a fala de uma casa (corpus.hpp: ~770 palavras,
// concordância, sentido), dorme toda noite, e é testada:
//
//  * sujeitos RETIDOS: bichos e pessoas que ela só ouviu como objeto ("eu vejo
//    a girafa"). Começamos "a girafa ..." e ela continua. A frase inteira é
//    gramatical? Concorda? Faz sentido? E é uma frase que ela nunca ouviu?
//  * sujeitos conhecidos: o mesmo, para palavras que ela ouviu como sujeito.
//  * com e sem a gramática na fala (o mesmo cérebro: só desliga o córtex de
//    categorias na hora de falar), para saber o que a gramática acrescenta.
//  * categorias: o código de categoria do vizinho mais parecido de cada palavra
//    é da mesma classe verdadeira (substantivo masculino singular, adjetivo
//    feminino plural, verbo no plural...)?
//
// Uso: grammar [dias] [frases_por_dia] [episodios_de_sono] [sementes]
//   CAT=0     sem categorias nenhuma (nem aprende)
//   V=1       mostra exemplos
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "corpus.hpp"
#include "eva2.hpp"

struct Score { int n = 0, syn = 0, agr = 0, sem = 0, novel_ok = 0, empty = 0, known = 0, known_agr = 0; };

static std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) s += (i ? " " : "") + v[i];
    return s;
}

int main(int argc, char** argv) {
    const int days = argc > 1 ? atoi(argv[1]) : 10;
    const int per_day = argc > 2 ? atoi(argv[2]) : 1000;
    const int sleep_eps = argc > 3 ? atoi(argv[3]) : 300;
    const int seeds = argc > 4 ? atoi(argv[4]) : 2;
    const bool verbose = getenv("V") != nullptr;
    std::set<int> eval_days;
    for (int d = 1; d <= days; d *= 2) eval_days.insert(d);
    eval_days.insert(days);

    std::map<int, Score> held_g, held_m, seen_g, seen_m;
    std::map<int, std::pair<int, int>> nn;  // vizinho mais próximo da mesma classe
    std::map<int, std::pair<int, int>> nn_pos;
    double secs_train = 0;
    long long syn_words = 0, syn_cats = 0;
    size_t mem = 0;

    for (int seed = 1; seed <= seeds; ++seed) {
        corpus::Grammar g(100 + seed);
        eva2::Eva2Config c;
        c.memory_file = "/dev/null"; c.diary_file = "/dev/null";
        c.world_enabled = false;
        if (getenv("CAT")) c.categories_enabled = atoi(getenv("CAT"));
        if (getenv("COLS")) c.sdr_columns = atoi(getenv("COLS"));
        if (getenv("CELLS")) c.tm.cells_per_column = atoi(getenv("CELLS"));
        if (getenv("CCOLS")) c.categories.columns = atoi(getenv("CCOLS"));
        if (getenv("CACT")) c.categories.active = atoi(getenv("CACT"));
        if (getenv("MATCH")) c.category_match = atof(getenv("MATCH"));
        if (getenv("OVERRIDE")) c.grammar_override = atof(getenv("OVERRIDE"));
        if (getenv("FRESH")) c.category_tm.fresh_context_after_burst = atoi(getenv("FRESH"));
        if (getenv("CCELLS")) c.category_tm.cells_per_column = atoi(getenv("CCELLS"));
        if (getenv("MORPH")) c.categories.morph_weight = atof(getenv("MORPH"));
        if (getenv("CTXW")) c.categories.context_words = atoi(getenv("CTXW"));
        eva2::Eva2 e(c, seed);
        if (getenv("NOCHECK")) e.set_grammar_checks_memory(false);
        std::set<std::string> heard;

        for (int d = 1; d <= days; ++d) {
            auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < per_day; ++i) {
                auto s = g.sentence();
                heard.insert(join(s));
                e.listen(s);
            }
            e.sleep(sleep_eps);
            secs_train += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (!eval_days.count(d)) continue;

            auto run = [&](bool held, bool grammar, Score& sc, bool show) {
                e.set_grammar_in_speech(grammar);
                auto starts = g.subject_starts(held);
                for (size_t k = 0; k < starts.size(); ++k) {
                    std::vector<std::string> start{eva2::Eva2::kYou};
                    start.insert(start.end(), starts[k].begin(), starts[k].end());
                    auto got = e.complete(start, 8);
                    auto full = starts[k];
                    full.insert(full.end(), got.begin(), got.end());
                    auto ck = g.check(full);
                    ++sc.n;
                    sc.empty += got.empty();
                    sc.syn += ck.syntax; sc.agr += ck.agreement; sc.sem += ck.semantics;
                    if (ck.agreement && !heard.count(join(full))) ++sc.novel_ok;
                    if (e.categories().has_code(e.vocab().id(starts[k][1]))) { ++sc.known; sc.known_agr += ck.agreement; }
                    if (show && k < 10)
                        std::printf("    %-34s %s%s\n", join(full).c_str(),
                                    ck.semantics ? "ok" : ck.agreement ? "ok (sentido estranho)" : ck.syntax ? "erro de concordância" : "agramatical",
                                    heard.count(join(full)) ? "" : (ck.agreement ? "  [nunca ouviu]" : ""));
                }
                e.set_grammar_in_speech(true);
            };
            const bool show = verbose && seed == 1 && d == days;
            if (show) std::printf("\n  [dia %d, semente 1] sujeitos retidos, com gramática:\n", d);
            run(true, true, held_g[d], show);
            run(true, false, held_m[d], false);
            if (show) std::printf("  [dia %d, semente 1] sujeitos conhecidos, com gramática:\n", d);
            run(false, true, seen_g[d], show);
            run(false, false, seen_m[d], false);

            // categorias: o vizinho mais parecido é da mesma classe?
            const auto& v = e.vocab();
            const auto& cats = e.categories();
            std::vector<int> ids;
            for (int k = 0; k < static_cast<int>(v.words.size()); ++k)
                if (!eva2::Vocabulary::internal(v.words[k]) && cats.has_code(k)) ids.push_back(k);
            for (int a : ids) {
                int best = -1; float bs = -1;
                for (int b : ids) if (b != a) { float s = cats.similarity(a, b); if (s > bs) { bs = s; best = b; } }
                if (best < 0) continue;
                nn[d].second++; nn_pos[d].second++;
                if (g.gold(v.words[a]) == g.gold(v.words[best])) nn[d].first++;
                if (g.pos_only(v.words[a]) == g.pos_only(v.words[best])) nn_pos[d].first++;
            }
            if (show) {
                std::printf("  vizinhos de categoria (semente 1):\n");
                for (const char* w : {"gato", "girafa", "bonito", "bonita", "corre", "correm", "come", "brincar", "na", "os"}) {
                    int a = v.id(w);
                    if (a < 0 || !cats.has_code(a)) continue;
                    std::vector<std::pair<float, int>> sims;
                    for (int b : ids) if (b != a) sims.push_back({cats.similarity(a, b), b});
                    std::sort(sims.rbegin(), sims.rend());
                    std::printf("    %-8s ->", w);
                    for (int i = 0; i < 6 && i < static_cast<int>(sims.size()); ++i) std::printf(" %s(%.2f)", v.words[sims[i].second].c_str(), sims[i].first);
                    std::printf("\n");
                }
            }
            if (d == days) {
                syn_words += e.cortex().n_synapses();
                syn_cats += e.category_cortex().n_synapses();
                mem += e.cortex().memory_bytes() + e.category_cortex().memory_bytes();
            }
        }
        if (verbose && seed == 1) {
            auto st = e.gen_stats();
            std::printf("  palavras faladas vindas da memória: %lld | da gramática: %lld | chutes vetados: %lld\n", st.memory, st.grammar, st.vetoed);
        }
    }

    auto pct = [](int a, int b) { return b ? 100.0 * a / b : 0.0; };
    std::printf("\n%d dias x %d frases (%d frases ouvidas), sono %d episodios/noite, %d sementes%s\n",
                days, per_day, days * per_day, sleep_eps, seeds, getenv("CAT") && !atoi(getenv("CAT")) ? " [SEM categorias]" : "");
    std::printf("%-5s | %-44s | %-44s | %s\n", "dia", "sujeitos RETIDOS: gramat. / concorda / sentido / nova", "sujeitos conhecidos: gramat. / concorda / sentido", "vizinho mesma classe (fina / classe)");
    for (int d : eval_days) {
        const Score& a = held_g[d]; const Score& b = held_m[d]; const Score& s = seen_g[d]; const Score& t = seen_m[d];
        std::printf("%-5d | com gram.: %3.0f%% / %3.0f%% / %3.0f%% / %3.0f%%      | com gram.: %3.0f%% / %3.0f%% / %3.0f%%            | %3.0f%% / %3.0f%%\n",
                    d, pct(a.syn, a.n), pct(a.agr, a.n), pct(a.sem, a.n), pct(a.novel_ok, a.n), pct(s.syn, s.n), pct(s.agr, s.n), pct(s.sem, s.n),
                    pct(nn[d].first, nn[d].second), pct(nn_pos[d].first, nn_pos[d].second));
        std::printf("%-5s | sem gram.: %3.0f%% / %3.0f%% / %3.0f%% / %3.0f%%  (vazias %3.0f%%) | sem gram.: %3.0f%% / %3.0f%% / %3.0f%%\n",
                    "", pct(b.syn, b.n), pct(b.agr, b.n), pct(b.sem, b.n), pct(b.novel_ok, b.n), pct(b.empty, b.n), pct(t.syn, t.n), pct(t.agr, t.n), pct(t.sem, t.n));
    }
    std::printf("sujeitos retidos que ela já tinha categorizado (ouviu %.0f+ vezes), com gramática: ", eva2::CategoryConfig{}.min_count);
    for (int d : eval_days) std::printf(" dia %d: %d/%d frases certas (%.0f%%) |", d, held_g[d].known_agr, held_g[d].known, pct(held_g[d].known_agr, held_g[d].known));
    std::printf("\n");
    std::printf("sinapses (média por cérebro): córtex de palavras %.1f mi, de categorias %.1f mi | memória %.0f MB | treino %.1f s por cérebro (%.0f frases/s)\n",
                syn_words / 1e6 / seeds, syn_cats / 1e6 / seeds, mem / 1e6 / seeds, secs_train / seeds, days * per_day / (secs_train / seeds));
}
