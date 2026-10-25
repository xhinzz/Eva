// Eva 2 - códigos esparsos (SDR): como o córtex representa uma palavra.
// Cada palavra vira um conjunto fixo de ~2% das colunas. Palavras diferentes
// quase não se sobrepõem, então o córtex nunca as confunde.
#pragma once
#include <algorithm>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace eva2 {

struct Vocabulary {
    int columns = 2048;
    int active = 40;
    std::vector<std::string> words;
    std::vector<std::vector<int>> codes;  // colunas ativas de cada palavra (ordenadas)
    std::unordered_map<std::string, int> index;

    int id(const std::string& w) const {
        auto it = index.find(w);
        return it == index.end() ? -1 : it->second;
    }
    // aprende uma palavra nova: sorteia um código esparso para ela
    int add(const std::string& w, std::mt19937& rng) {
        const int existing = id(w);
        if (existing >= 0) return existing;
        std::vector<int> all(columns);
        for (int i = 0; i < columns; ++i) all[i] = i;
        std::shuffle(all.begin(), all.end(), rng);
        std::vector<int> code(all.begin(), all.begin() + active);
        std::sort(code.begin(), code.end());
        words.push_back(w);
        codes.push_back(code);
        index[w] = static_cast<int>(words.size()) - 1;
        return index[w];
    }
    // quantas colunas de `cols` pertencem à palavra `k`
    int overlap(int k, const std::vector<int>& cols_sorted) const {
        int n = 0;
        size_t a = 0, b = 0;
        const auto& c = codes[k];
        while (a < c.size() && b < cols_sorted.size()) {
            if (c[a] == cols_sorted[b]) { ++n; ++a; ++b; }
            else if (c[a] < cols_sorted[b]) ++a;
            else ++b;
        }
        return n;
    }
};

}  // namespace eva2
