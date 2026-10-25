// Eva 2 - medida: ensina N frases (2 vezes) e pede para completar a partir das
// 2 primeiras palavras. Conta palavras certas em ordem e frases inteiras.
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>
#include "eva2.hpp"
int main(int argc, char** argv) {
    int reps = argc > 1 ? atoi(argv[1]) : 2;
    const char* corpus[] = {
        "eu gosto muito de musica", "hoje eu vou trabalhar cedo", "amanha eu vou viajar para o mar",
        "o mar e bonito de manha", "eu me chamo pedro e moro aqui", "minha mae faz bolo de cenoura",
        "o cachorro dorme no sofa da sala", "eu gosto de cafe com leite", "vou ao mercado comprar pao",
        "a lua brilha na noite escura", "meu irmao joga bola no parque", "eu gosto de ler livros antigos",
        "o gato subiu no telhado", "hoje choveu muito na cidade", "eu vou dormir cedo hoje",
        "ela canta bem e toca violao", "o onibus passa as oito", "nos vamos ao cinema sabado",
        "eu quero aprender a cozinhar", "o cafe da manha esta pronto"};
    const int N = sizeof(corpus) / sizeof(corpus[0]);
    int seeds = 8, total_words = 0, ok_words = 0, full = 0, total = 0;
    for (int seed = 1; seed <= seeds; ++seed) {
        eva2::Eva2Config c; c.memory_file = "/dev/null"; c.diary_file = "/dev/null";
        if (getenv("CAT")) c.categories_enabled = atoi(getenv("CAT"));
        if (getenv("MINC")) c.categories.min_count = atof(getenv("MINC"));
        if (getenv("DEC")) c.tm.predicted_decrement = atof(getenv("DEC"));
        if (getenv("INIT")) c.tm.initial_permanence = atof(getenv("INIT"));
        eva2::Eva2 e(c, seed);
        std::vector<std::vector<std::string>> sents;
        for (auto s : corpus) { std::istringstream in(s); std::vector<std::string> w; for (std::string x; in >> x;) w.push_back(x); sents.push_back(w); }
        for (int r = 0; r < reps; ++r) for (auto& s : sents) { e.hear(s); e.idle(500); }
        for (auto& s : sents) {
            std::vector<std::string> start(s.begin(), s.begin() + 2);
            auto got = e.complete(start, 12);
            std::vector<std::string> want(s.begin() + 2, s.end());
            int k = 0; while (k < (int)got.size() && k < (int)want.size() && got[k] == want[k]) ++k;
            ok_words += k; total_words += want.size(); full += (k == (int)want.size() && got.size() == want.size()); ++total;
            if (seed == 1 && getenv("V")) { std::printf("  %s %s ->", start[0].c_str(), start[1].c_str()); for (auto& w : got) std::printf(" %s", w.c_str()); std::printf("\n"); }
        }
    }
    std::printf("%d frases x %d repeticoes, %d sementes: palavras certas %d/%d (%.0f%%), frases inteiras %d/%d\n",
                N, reps, seeds, ok_words, total_words, 100.0 * ok_words / total_words, full, total);
}
