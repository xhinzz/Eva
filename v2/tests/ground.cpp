// Eva 2 - medida de sentido COM OLHOS: você a leva pelos lugares nomeando o que
// ela vê (uma frase por objeto, por volta; cada vez o objeto é um desenho
// diferente). Depois:
//   nomear:   vendo um desenho NOVO do objeto, ela diz o nome?
//   imaginar: ouvindo só a palavra, a imagem que ela gera parece mais com esse
//             objeto do que com qualquer outro? (comparada a desenhos novos)
//   entender: no quarto, "vamos para a praia" -> ela vai? "pega a flor" no jardim -> ela pega?
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "eva2.hpp"
int main(int argc, char** argv) {
    int rounds = argc > 1 ? atoi(argv[1]) : 2;
    const int seeds = argc > 2 ? atoi(argv[2]) : 8;
    const std::vector<std::pair<std::string, std::vector<std::string>>> lessons = {
        {"quarto", {"cama", "janela"}}, {"cozinha", {"mesa", "cafe"}}, {"jardim", {"flor", "arvore"}}, {"praia", {"mar", "areia", "sol", "barco"}}};
    auto article = [](const std::string& o) { return (o == "mar" || o == "sol" || o == "barco" || o == "cafe") ? "o" : "a"; };
    int ok = 0, total = 0, named = 0, imag_ok = 0, imag_total = 0, went = 0, took = 0;
    std::mt19937 rng(7);
    for (int seed = 1; seed <= seeds; ++seed) {
        eva2::Eva2Config c; c.memory_file = "/dev/null"; c.diary_file = "/dev/null";
        eva2::Eva2 e(c, seed);
        for (int r = 0; r < rounds; ++r)
            for (auto& [place, objs] : lessons) {
                e.take_to(place);
                e.hear({"estamos", place == "praia" ? "na" : "no", place}); e.respond(); e.idle(1000);
                for (auto& o : objs) { if (getenv("APONTA")) e.point_at(o); e.hear({"olha", article(o), o}); e.respond(); e.idle(1000); }
            }
        // nomear desenhos novos
        for (auto& [place, objs] : lessons)
            for (auto& o : objs) {
                auto w = e.name_what_you_see(o);
                ++total;
                if (!w.empty()) { ++named; if (w.front() == o) ++ok; }
                if (seed == 1 && getenv("V")) { std::printf("  ve %-7s -> ", o.c_str()); for (auto& x : w) std::printf("%s ", x.c_str()); std::printf("\n"); }
            }
        // imaginar: a imagem gerada pela palavra fica mais perto de desenhos novos do próprio objeto?
        std::vector<std::string> all;
        for (auto& [place, objs] : lessons) for (auto& o : objs) all.push_back(o);
        std::vector<eva2::Image> row;
        for (auto& o : all) {
            float conf = 0;
            eva2::Image im = e.imagine_image({o}, &conf);
            row.push_back(im);
            std::string best; float be = 1e9f;
            for (auto& q : all) {
                float err = 0; for (int k = 0; k < 3; ++k) err += eva2::VisualCortex::error(im, eva2::draw_object(q, rng, 1.0f));
                if (err < be) { be = err; best = q; }
            }
            ++imag_total;
            if (best == o) ++imag_ok;
            if (seed == 1 && getenv("V")) std::printf("  imagina '%-7s' (forca %.2f) -> parece %s\n", o.c_str(), conf, best.c_str());
        }
        if (seed == 1) eva2::write_ppm("ground_imagina.ppm", row, 4);
        // entender
        e.take_to("quarto");
        e.hear({"vamos", "para", "a", "praia"}); e.respond();
        for (int i = 0; i < 4 && e.world().here != "praia"; ++i) { e.act(); e.idle(2000); }
        if (e.world().here == "praia") ++went;
        e.take_to("jardim");
        e.hear({"pega", "a", "flor"}); e.respond();
        for (int i = 0; i < 2 && e.world().holding != "flor"; ++i) { e.act(); e.idle(1000); }
        if (e.world().holding == "flor") ++took;
    }
    std::printf("%s%d voltas, %d sementes: nomeou %d/%d, nome certo %d/%d (%.0f%%) | imaginou certo %d/%d (%.0f%%) | foi a praia %d/%d | pegou a flor %d/%d\n",
                getenv("APONTA") ? "apontando, " : "sem apontar, ", rounds, seeds, named, total, ok, total, 100.0 * ok / total,
                imag_ok, imag_total, 100.0 * imag_ok / imag_total, went, seeds, took, seeds);
}
