// Eva 2 - os olhos sozinhos: as 202 coisas da casa dá para reconhecer?
//
// Sem palavras e sem cenas, só o córtex visual:
//  1. RECONHECER: 6 vistas de cada coisa; cada vista é comparada com a média das
//     outras vistas de cada coisa (vizinho mais próximo). Com 1 e com 3 olhares
//     por vista, e com três descrições: só V1 (bordas e cores, máximo por zona da
//     coisa), V1 + V4 (de que cores é a coisa, onde ela ocupa a caixa, proporções),
//     e só V4.
//  2. FORMAR CONCEITOS sem nome: 2000 aparições de coisas sorteadas (3 olhares
//     cada); cada uma cai no conceito mais parecido ou abre um novo, conforme o
//     limiar. Depois, 3 vistas novas de cada coisa: caem no mesmo conceito? O
//     conceito é só daquela coisa?
// Uso: eyes_features
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

#include "corpus.hpp"
#include "vision.hpp"
using namespace eva2;

static float cosv(const std::vector<float>& a, const std::vector<float>& b) {
    double d = 0, na = 0, nb = 0;
    for (size_t i = 0; i < a.size(); ++i) { d += a[i] * b[i]; na += a[i] * a[i]; nb += b[i] * b[i]; }
    return static_cast<float>(d / (std::sqrt(na * nb) + 1e-9));
}

int main() {
    corpus::Grammar g(7);
    std::vector<std::string> things;
    std::set<std::string> got;
    for (const auto& en : g.lexicon())
        if (en.pos == corpus::NOUN && en.n == 's' && !(en.sem & corpus::PLACE) && !got.count(en.lemma)) { got.insert(en.lemma); things.push_back(en.lemma); }
    set_sprite_names(things);
    VisionConfig vc;
    vc.v2_units = 640;
    VisualCortex eyes(vc, 8);
    std::mt19937 rr(3);
    std::vector<Image> samples;
    for (int k = 0; k < 2; ++k) for (const auto& t : things) samples.push_back(draw_sprite(t, rr));
    eyes.prime(samples);
    const size_t v1_dims = 960;  // (1 + 9 zonas) x 96 traços de V1; o resto é V4
    auto part = [&](const std::vector<float>& v, int mode) {
        if (mode == 1) return v;
        if (mode == 0) return std::vector<float>(v.begin(), v.begin() + v1_dims);
        return std::vector<float>(v.begin() + v1_dims, v.end());
    };
    std::mt19937 vr(101);
    std::printf("%zu coisas, cada uma com forma, cores e marcas próprias; cada vista muda de lugar (até 2 pixels) e de tom\n\n1. reconhecer (vizinho mais próximo, 6 vistas de cada):\n", things.size());
    const char* names[] = {"só V1 (bordas e cores por zona)", "V1 + V4 (cor e forma da coisa)", "só V4"};
    for (int glances : {1, 3}) {
        std::vector<std::vector<std::vector<float>>> F(things.size());
        for (size_t t = 0; t < things.size(); ++t)
            for (int v = 0; v < 6; ++v) {
                std::vector<Image> gl;
                for (int k = 0; k < glances; ++k) gl.push_back(draw_sprite(things[t], vr));
                F[t].push_back(eyes.invariant_glances(gl));
            }
        for (int mode = 0; mode < 3; ++mode) {
            int ok = 0, n = 0;
            double same = 0, other = 0;
            for (size_t t = 0; t < things.size(); ++t)
                for (int v = 0; v < 6; ++v) {
                    const auto q = part(F[t][v], mode);
                    int best = -1; float bs = -2;
                    for (size_t u = 0; u < things.size(); ++u) {
                        std::vector<float> c(q.size(), 0.0f);
                        for (int w = 0; w < 6; ++w) { if (u == t && w == v) continue; const auto x = part(F[u][w], mode); for (size_t i = 0; i < c.size(); ++i) c[i] += x[i]; }
                        const float s = cosv(q, c);
                        if (s > bs) { bs = s; best = static_cast<int>(u); }
                    }
                    ok += best == static_cast<int>(t); ++n;
                }
            for (size_t t = 0; t < things.size(); ++t) { same += cosv(part(F[t][0], mode), part(F[t][1], mode)); other += cosv(part(F[t][0], mode), part(F[(t + 1) % things.size()][0], mode)); }
            std::printf("   %d olhar%s, %-34s reconhece %3.0f%% | parecença: mesma coisa %.2f, outra coisa %.2f\n", glances, glances > 1 ? "es" : "  ", names[mode],
                        100.0 * ok / n, same / things.size(), other / things.size());
        }
    }
    std::printf("\n2. formar conceitos sem nome (V1 + V4, 3 olhares, 2000 aparições):\n");
    std::mt19937 pick(5);
    std::uniform_int_distribution<size_t> pt(0, things.size() - 1);
    auto look = [&](size_t t) { std::vector<Image> gl; for (int k = 0; k < 3; ++k) gl.push_back(draw_sprite(things[t], vr)); return eyes.invariant_glances(gl); };
    std::vector<std::pair<size_t, std::vector<float>>> stream;
    for (int i = 0; i < 2000; ++i) { const size_t t = pt(pick); stream.push_back({t, look(t)}); }
    std::vector<std::vector<std::vector<float>>> tv(things.size());
    for (size_t t = 0; t < things.size(); ++t) for (int v = 0; v < 3; ++v) tv[t].push_back(look(t));
    for (float thr : {0.6f, 0.65f, 0.7f, 0.75f, 0.8f}) {
        ConceptMemory mem;
        mem.threshold = thr;
        for (const auto& [t, v] : stream) { auto [id, sc] = mem.best(v); if (id >= 0 && sc >= thr) mem.add_exemplar(id, v); else mem.create(v); }
        int cons = 0, views = 0, own = 0;
        std::map<int, std::map<size_t, int>> owner;
        std::map<size_t, int> maj;
        for (size_t t = 0; t < things.size(); ++t) {
            std::map<int, int> votes;
            for (const auto& v : tv[t]) votes[mem.best(v).first]++;
            int b = 0;
            for (const auto& [k, n] : votes) if (n > b) { b = n; maj[t] = k; }
            cons += b; views += 3;
            for (const auto& [k, n] : votes) owner[k][t] += n;
        }
        for (size_t t = 0; t < things.size(); ++t) { int owners = 0; for (const auto& [tt, n] : owner[maj[t]]) owners += n >= 2; own += owners <= 1; }
        std::printf("   limiar %.2f: %3zu conceitos | a mesma coisa cai no mesmo conceito %3.0f%% | coisas com um conceito só delas %3.0f%%\n", thr, mem.size(),
                    100.0 * cons / views, 100.0 * own / things.size());
    }
}
