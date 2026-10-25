// Eva 2 - códigos esparsos (SDR): como o córtex representa uma palavra.
// Cada palavra tem colunas de IDENTIDADE (sorteadas uma vez: palavras
// diferentes quase não se sobrepõem, então o córtex nunca as confunde) e,
// quando o uso já mostrou de que espécie ela é, um código de CATEGORIA
// (compartilhado com as palavras que vivem nos mesmos contextos; veja
// categories.hpp). A identidade vai para o córtex de palavras; a categoria,
// para o córtex de categorias (uma área acima, mais abstrata).
#pragma once
#include <algorithm>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace eva2 {

struct Vocabulary {
    int columns = 2048;      // colunas de identidade
    int active = 40;         // colunas de identidade de símbolos internos (<voce>, [percepções], {ações})
    int word_active = 40;    // colunas de identidade de palavras
    int category_columns = 2048;  // tamanho do espaço de categorias
    std::vector<std::string> words;
    std::vector<std::vector<int>> ident;  // colunas de identidade
    std::vector<std::vector<int>> cat;    // código de categoria (vazio = ainda sem categoria)
    std::vector<std::vector<int>> codes;  // o que entra no córtex de palavras (= identidade)
    std::unordered_map<std::string, int> index;

    static bool internal(const std::string& w) { return !w.empty() && (w[0] == '<' || w[0] == '[' || w[0] == '{'); }

    int id(const std::string& w) const {
        auto it = index.find(w);
        return it == index.end() ? -1 : it->second;
    }
    // aprende uma palavra nova: sorteia um código esparso para ela
    int add(const std::string& w, std::mt19937& rng) {
        const int existing = id(w);
        if (existing >= 0) return existing;
        const int n = internal(w) ? active : word_active;
        std::vector<int> code;
        std::uniform_int_distribution<int> pick(0, columns - 1);
        while (static_cast<int>(code.size()) < n) {
            const int c = pick(rng);
            if (std::find(code.begin(), code.end(), c) == code.end()) code.push_back(c);
        }
        std::sort(code.begin(), code.end());
        words.push_back(w);
        ident.push_back(code);
        cat.push_back({});
        codes.push_back(code);
        index[w] = static_cast<int>(words.size()) - 1;
        return index[w];
    }
    // dá (ou troca) o código de categoria de uma palavra
    void set_category(int k, const std::vector<int>& code) { cat[k] = code; }
    // o que entra no córtex de categorias: a categoria, ou, se ainda não há,
    // a própria identidade (a palavra é "uma espécie de uma só", como no começo)
    std::vector<int> category_code(int k) const {
        if (!cat[k].empty()) return cat[k];
        std::vector<int> c;
        for (int x : ident[k]) c.push_back(x % category_columns);
        std::sort(c.begin(), c.end());
        c.erase(std::unique(c.begin(), c.end()), c.end());
        return c;
    }
    // quantas colunas de `cols` pertencem à palavra `k`
    int overlap(int k, const std::vector<int>& cols_sorted) const { return overlap_sorted(codes[k], cols_sorted); }
    static int overlap_sorted(const std::vector<int>& c, const std::vector<int>& cols_sorted) {
        int n = 0;
        size_t a = 0, b = 0;
        while (a < c.size() && b < cols_sorted.size()) {
            if (c[a] == cols_sorted[b]) { ++n; ++a; ++b; }
            else if (c[a] < cols_sorted[b]) ++a;
            else ++b;
        }
        return n;
    }
};

}  // namespace eva2
