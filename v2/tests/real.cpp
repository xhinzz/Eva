// Eva 2 - medida com português de verdade (e escala).
//
// Em vez da gramática pequena da casa, ela ouve frases reais. Depois, em frases
// que nunca ouviu, medimos:
//   1. a próxima palavra: dado o começo da frase, a palavra que ela prevê é a
//      certa? E é da classe certa (substantivo, verbo, artigo...)? Comparado com
//      o "mais comum" e com o "bigrama" (a palavra que mais veio depois da anterior);
//   2. as espécies que ela descobriu: o vizinho mais parecido de cada palavra
//      frequente tem a mesma classe anotada (Universal Dependencies)?
//   3. escala: vocabulário, sinapses, memória (RAM), palavras por segundo.
//
// Uso:
//   eva2_real [frases_para_ouvir] [frases_de_teste]
//   TRAIN=a.conllu,b.conllu TEST=c.conllu   frases reais anotadas (UD)
//   CHAT=pasta                              fala real do CHILDES (.cha): 95% para ouvir, 5% de teste
//   BIG=1                                   o cérebro grande (16.384 colunas, 32 células)
//   V=1                                     exemplos
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "eva2.hpp"
#include "real_text.hpp"

static std::vector<std::string> split_commas(const std::string& s) {
    std::vector<std::string> v;
    std::istringstream in(s);
    for (std::string x; std::getline(in, x, ',');) if (!x.empty()) v.push_back(x);
    return v;
}
static double rss_mb() {
    std::ifstream in("/proc/self/status");
    for (std::string line; std::getline(in, line);)
        if (line.rfind("VmRSS:", 0) == 0) return std::atof(line.c_str() + 6) / 1024.0;
    return 0.0;
}
static std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) s += (i ? " " : "") + v[i];
    return s;
}

int main(int argc, char** argv) {
    const size_t n_train = argc > 1 ? std::atoi(argv[1]) : 8000;
    const size_t n_test = argc > 2 ? std::atoi(argv[2]) : 300;
    const bool verbose = std::getenv("V") != nullptr;
    std::vector<eva2::RealSentence> train, test;
    std::string source;
    if (std::getenv("CHAT")) {
        auto all = eva2::read_chat(std::getenv("CHAT"), 15);
        const size_t cut = all.size() * 95 / 100;
        train.assign(all.begin(), all.begin() + cut);
        test.assign(all.begin() + cut, all.end());
        source = std::string("CHILDES (") + std::getenv("CHAT") + ")";
    } else {
        const char* tr = std::getenv("TRAIN");
        const char* te = std::getenv("TEST");
        if (!tr || !te) { std::fprintf(stderr, "uso: TRAIN=a.conllu,b.conllu TEST=c.conllu eva2_real [ouvir] [teste]  (ou CHAT=pasta)\n"); return 1; }
        for (const auto& f : split_commas(tr)) { auto s = eva2::read_conllu(f, 1000000, 15); train.insert(train.end(), s.begin(), s.end()); }
        for (const auto& f : split_commas(te)) { auto s = eva2::read_conllu(f, 1000000, 15); test.insert(test.end(), s.begin(), s.end()); }
        source = std::string("Universal Dependencies (") + tr + ")";
    }
    // misturar as fontes (as frases de cada arquivo vêm em blocos de assunto)
    {
        std::mt19937 rr(3);
        std::shuffle(train.begin(), train.end(), rr);
    }
    if (train.size() > n_train) train.resize(n_train);
    std::vector<eva2::RealSentence> probes;
    for (const auto& s : test) if (s.words.size() >= 3) probes.push_back(s);
    if (probes.size() > n_test) probes.resize(n_test);

    eva2::Eva2Config c = std::getenv("BIG") ? eva2::Eva2Config::big() : eva2::Eva2Config{};
    c.world_enabled = false;
    c.diary_file = "/dev/null";
    c.memory_file = "/dev/null";
    eva2::Eva2 e(c, 1);

    // o que se sabe de cada palavra (para medir): a classe mais comum, a frequência, quem vem depois
    std::map<std::string, std::map<std::string, int>> pos_count;
    std::map<std::string, int> freq;
    std::map<std::string, std::map<std::string, int>> next;
    size_t tokens = 0;
    for (const auto& s : train) {
        for (size_t i = 0; i < s.words.size(); ++i) {
            ++freq[s.words[i]];
            if (i < s.pos.size()) ++pos_count[s.words[i]][s.pos[i]];
            if (i + 1 < s.words.size()) ++next[s.words[i]][s.words[i + 1]];
        }
        tokens += s.words.size();
    }
    auto major_pos = [&](const std::string& w) -> std::string {
        auto it = pos_count.find(w);
        if (it == pos_count.end()) return "?";
        std::string b; int bn = 0;
        for (const auto& [p, n] : it->second) if (n > bn) { bn = n; b = p; }
        return b;
    };
    std::string most_common; int mc = 0;
    for (const auto& [w, n] : freq) if (n > mc) { mc = n; most_common = w; }

    std::printf("fonte: %s\nouvir: %zu frases (%zu palavras, %zu diferentes); teste: %zu frases novas\ncérebro: %s (%d colunas de palavras x %d células)\n",
                source.c_str(), train.size(), tokens, freq.size(), probes.size(), std::getenv("BIG") ? "grande" : "normal", c.sdr_columns, c.tm.cells_per_column);
    std::fflush(stdout);
    const auto t0 = std::chrono::steady_clock::now();
    size_t heard_words = 0;
    for (size_t i = 0; i < train.size(); ++i) {
        e.listen(train[i].words);
        heard_words += train[i].words.size();
        if ((i + 1) % 2000 == 0 || i + 1 == train.size()) {
            e.sleep(300);
            const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::printf("  %zu frases: %.0f palavras/s, %.1f mi de sinapses, %zu segmentos, RAM %.0f MB\n", i + 1, heard_words / sec,
                        (e.cortex().n_synapses() + e.category_cortex().n_synapses()) / 1e6, static_cast<size_t>(e.segments()), rss_mb());
            std::fflush(stdout);
        }
    }

    // ---- 1. a próxima palavra
    struct Score { int exact = 0, pos = 0, n = 0; };
    Score sc[4];  // 0: ela; 1: ela sem a gramática na fala; 2: bigrama; 3: a mais comum
    const bool has_pos = !probes.empty() && !probes[0].pos.empty();
    int shown = 0;
    for (const auto& s : probes) {
        for (size_t i = 1; i < s.words.size(); ++i) {
            if (s.words[i] == "<?>") continue;
            const std::vector<std::string> prefix(s.words.begin(), s.words.begin() + i);
            const std::string truth = s.words[i];
            const std::string truth_pos = has_pos ? s.pos[i] : major_pos(truth);
            std::string pred[4];
            for (int cond = 0; cond < 2; ++cond) {
                e.set_grammar_in_speech(cond == 0);
                auto w = e.complete(prefix, 1);
                pred[cond] = w.empty() ? "" : w[0];
            }
            e.set_grammar_in_speech(true);
            {
                auto it = next.find(prefix.back());
                if (it != next.end()) { int bn = 0; for (const auto& [w, n] : it->second) if (n > bn) { bn = n; pred[2] = w; } }
            }
            pred[3] = most_common;
            for (int k = 0; k < 4; ++k) {
                ++sc[k].n;
                sc[k].exact += pred[k] == truth;
                sc[k].pos += !pred[k].empty() && major_pos(pred[k]) == truth_pos;
            }
        }
        if (verbose && shown < 12) {
            ++shown;
            const std::vector<std::string> start(s.words.begin(), s.words.begin() + 2);
            std::printf("  \"%s ...\" -> ela continua: \"%s\"   (a frase real: \"%s\")\n", join(start).c_str(), join(e.complete(start, 10)).c_str(), join(s.words).c_str());
        }
    }

    // ---- 2. as espécies: o vizinho mais parecido tem a mesma classe?
    int nn_ok = 0, nn_n = 0;
    std::map<std::string, int> class_size;
    std::vector<int> frequent;
    for (size_t k = 0; k < e.vocab().words.size(); ++k) {
        const std::string& w = e.vocab().words[k];
        if (eva2::Vocabulary::internal(w) || !e.categories().has_code(static_cast<int>(k))) continue;
        if (freq[w] < 30 || major_pos(w) == "?") continue;
        frequent.push_back(static_cast<int>(k));
        ++class_size[major_pos(w)];
    }
    for (int a : frequent) {
        int best = -1; float bs = -1.0f;
        for (int b : frequent) if (b != a) { const float s = e.categories().similarity(a, b); if (s > bs) { bs = s; best = b; } }
        if (best < 0) continue;
        ++nn_n;
        nn_ok += major_pos(e.vocab().words[a]) == major_pos(e.vocab().words[best]);
    }
    double chance = 0.0;
    for (const auto& [p, n] : class_size) chance += (double)n / frequent.size() * (n - 1) / std::max<size_t>(1, frequent.size() - 1);

    auto pct = [](int a, int b) { return b ? 100.0 * a / b : 0.0; };
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\n1. a próxima palavra em frases novas (%d posições):\n", sc[0].n);
    const char* names[] = {"ela (córtex + gramática)", "ela sem a gramática na fala", "bigrama (a que mais veio depois)", "a palavra mais comum"};
    for (int k = 0; k < 4; ++k) std::printf("     %-34s palavra certa %5.1f%% | classe certa %5.1f%%\n", names[k], pct(sc[k].exact, sc[k].n), pct(sc[k].pos, sc[k].n));
    std::printf("2. espécies: palavras ouvidas 30+ vezes com categoria: %zu; o vizinho mais parecido tem a mesma classe: %.0f%% (acaso %.0f%%)\n",
                frequent.size(), pct(nn_ok, nn_n), 100.0 * chance);
    std::printf("3. escala: %zu palavras no vocabulário, %.1f mi de sinapses (%.1f nas palavras, %.1f nas categorias), %d segmentos, RAM %.0f MB, %.0f s ao todo\n",
                e.vocab().words.size(), (e.cortex().n_synapses() + e.category_cortex().n_synapses()) / 1e6, e.cortex().n_synapses() / 1e6,
                e.category_cortex().n_synapses() / 1e6, e.segments(), rss_mb(), sec);
}
