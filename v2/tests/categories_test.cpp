// Eva 2 - só as categorias (rápido): ouve N frases do corpus e mede se o código
// de categoria de cada palavra se parece com o de palavras da mesma classe.
// Uso: categories_test [frases]   (env: FAN, ACT, CTXW, MORPH, MINC)
#include <cstdio>
#include <cstdlib>
#include <map>
#include "categories.hpp"
#include "corpus.hpp"
int main(int argc, char** argv) {
    const int n = argc > 1 ? atoi(argv[1]) : 20000;
    eva2::CategoryConfig cc;
    if (getenv("FAN")) cc.fanout = atoi(getenv("FAN"));
    if (getenv("ACT")) cc.active = atoi(getenv("ACT"));
    if (getenv("CTXW")) cc.context_words = atoi(getenv("CTXW"));
    if (getenv("MORPH")) cc.morph_weight = atof(getenv("MORPH"));
    if (getenv("MINC")) cc.min_count = atof(getenv("MINC"));
    corpus::Grammar g(101);
    eva2::WordCategories cats(cc);
    std::map<std::string, int> id;
    std::vector<std::string> words;
    for (int i = 0; i < n; ++i) {
        auto s = g.sentence();
        std::vector<int> ids;
        for (auto& w : s) { auto it = id.find(w); if (it == id.end()) { id[w] = words.size(); words.push_back(w); } ids.push_back(id[w]); }
        cats.observe(ids, s);
    }
    cats.update_all();
    std::vector<int> have;
    for (int k = 0; k < (int)words.size(); ++k) if (cats.has_code(k)) have.push_back(k);
    int nn_fine = 0, nn_pos = 0;
    std::map<std::string, std::pair<double, int>> same, diff;
    double s_same = 0, s_diff = 0; long n_same = 0, n_diff = 0;
    for (int a : have) {
        int best = -1; float bs = -1;
        for (int b : have) if (b != a) {
            float s = cats.similarity(a, b);
            if (s > bs) { bs = s; best = b; }
            if (g.gold(words[a]) == g.gold(words[b])) { s_same += s; ++n_same; } else { s_diff += s; ++n_diff; }
        }
        nn_fine += g.gold(words[a]) == g.gold(words[best]);
        nn_pos += g.pos_only(words[a]) == g.pos_only(words[best]);
    }
    std::printf("%d frases: %zu/%zu palavras com categoria | vizinho mesma classe fina %.0f%%, classe %.0f%% | parecença mesma classe %.2f, outra %.2f\n",
                n, have.size(), words.size(), 100.0 * nn_fine / have.size(), 100.0 * nn_pos / have.size(), s_same / n_same, s_diff / n_diff);
    if (getenv("V"))
        for (const char* w : {"gato", "vaca", "girafa", "bonito", "bonita", "corre", "correm", "come", "brincar", "na", "os", "cozinha"}) {
            auto it = id.find(w); if (it == id.end() || !cats.has_code(it->second)) continue;
            int a = it->second;
            std::vector<std::pair<float, int>> sims;
            for (int b : have) if (b != a) sims.push_back({cats.similarity(a, b), b});
            std::sort(sims.rbegin(), sims.rend());
            std::printf("  %-8s (%s, %.0fx) ->", w, g.gold(w).c_str(), cats.count(a));
            for (int i = 0; i < 6; ++i) std::printf(" %s(%.2f)", words[sims[i].second].c_str(), sims[i].first);
            std::printf("\n");
        }
}
