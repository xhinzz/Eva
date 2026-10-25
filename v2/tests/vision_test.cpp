// Eva 2 - olhos: desenha as coisas, o córtex visual aprende vendo, e imagina.
// Grava painéis PPM: linha 1 o que ela vê, linha 2 o que ela imagina a partir do código.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include "vision.hpp"
using namespace eva2;
int main(int argc, char** argv) {
    int epochs = argc > 1 ? atoi(argv[1]) : 30;
    const std::vector<std::string> objs{"mar", "sol", "areia", "barco", "flor", "arvore", "passaro", "cama", "janela", "livro", "mesa", "pao", "cafe", "fruta", "bolo"};
    std::mt19937 rng(1);
    VisualCortex vc;
    { std::vector<Image> samples; for (int r = 0; r < 4; ++r) for (auto& o : objs) samples.push_back(draw_object(o, rng, 1.0f)); vc.prime(samples); }
    // ela vê cada coisa muitas vezes, com variação (como no mundo real)
    for (int e = 0; e < epochs; ++e)
        for (auto& o : objs) vc.see(draw_object(o, rng, 1.0f), true);
    // teste: imagem nova de cada objeto (variação nova), código, reconstrução
    std::vector<Image> seen, imagined, v1rec;
    std::vector<VisualCode> codes;
    double err_v1 = 0, err_v2 = 0;
    for (auto& o : objs) {
        Image img = draw_object(o, rng, 1.0f);
        VisualCode c = vc.see(img, false);
        Image im = vc.imagine(c), r1 = vc.reconstruct_v1(c);
        err_v1 += VisualCortex::error(img, r1); err_v2 += VisualCortex::error(img, im);
        seen.push_back(img); imagined.push_back(im); v1rec.push_back(r1); codes.push_back(c);
    }
    std::printf("epocas %d: erro V1 %.3f, erro imaginacao (V2->V1->pixels) %.3f  (0 = perfeito, ~0.3 = ruido)\n", epochs, err_v1 / objs.size(), err_v2 / objs.size());
    // estabilidade: a mesma coisa, desenhada de novo, dá o mesmo código? e coisas diferentes, códigos diferentes?
    double same = 0, diff = 0; int nd = 0;
    for (size_t i = 0; i < objs.size(); ++i) {
        VisualCode again = vc.see(draw_object(objs[i], rng, 1.0f), false);
        same += VisualCortex::similarity(codes[i], again);
        for (size_t j = i + 1; j < objs.size(); ++j) { diff += VisualCortex::similarity(codes[i], codes[j]); ++nd; }
    }
    std::printf("codigo da mesma coisa (variada): %.2f de sobreposicao | coisas diferentes: %.2f\n", same / objs.size(), diff / nd);
    std::printf("pares mais parecidos:");
    std::vector<std::pair<float, std::string>> pairs;
    for (size_t i = 0; i < objs.size(); ++i) for (size_t j = i + 1; j < objs.size(); ++j) pairs.push_back({VisualCortex::similarity(codes[i], codes[j]), objs[i] + "-" + objs[j]});
    std::sort(pairs.rbegin(), pairs.rend());
    for (int k = 0; k < 5; ++k) std::printf(" %s(%.2f)", pairs[k].second.c_str(), pairs[k].first);
    std::printf("\n");
    write_ppm("ve.ppm", seen); write_ppm("imagina.ppm", imagined); write_ppm("v1.ppm", v1rec);
    // imaginar o desconhecido: juntar códigos de duas coisas
    std::vector<Image> mix;
    auto blend = [&](const char* a, const char* b) {
        size_t ia = std::find(objs.begin(), objs.end(), a) - objs.begin(), ib = std::find(objs.begin(), objs.end(), b) - objs.begin();
        std::vector<int> units = codes[ia].v2_active; units.insert(units.end(), codes[ib].v2_active.begin(), codes[ib].v2_active.end());
        std::vector<float> w(vc.config().v2_units, 0.0f);
        for (int u : codes[ia].v2_active) w[u] += codes[ia].v2_activity[u];
        for (int u : codes[ib].v2_active) w[u] += codes[ib].v2_activity[u];
        mix.push_back(vc.imagine(units, &w));
    };
    blend("flor", "mar"); blend("passaro", "sol"); blend("cama", "arvore"); blend("fruta", "barco");
    write_ppm("mistura.ppm", mix);
    return 0;
}
