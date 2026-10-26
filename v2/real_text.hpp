// Eva 2 - língua de verdade: ler frases reais do português.
//
// Dois formatos:
//  * CHAT (.cha), o formato do CHILDES (talkbank.org): fala real gravada em casa,
//    de pais com crianças. Lemos só os adultos (*MOT, *FAT, *INV...), não a
//    criança, e limpamos as marcas de transcrição ([...], <...>, &=ri, xxx, @s,
//    "(es)tá" -> "está"). A pergunta ("?") vira a subida da voz no fim (<?>).
//  * CoNLL-U, o formato das árvores do Universal Dependencies: frases escritas
//    reais, cada palavra com a classe gramatical anotada por gente (para medir
//    se as espécies que ela descobre batem com as verdadeiras).
#pragma once
#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace eva2 {

struct RealSentence {
    std::vector<std::string> words;
    std::vector<std::string> pos;  // classe anotada (UD), vazio no CHAT
};

inline std::string lower_utf8(std::string s) {
    // minúsculas: ASCII e as letras acentuadas do português (2 bytes, 0xC3 0x80-0x9E -> +0x20)
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = s[i];
        if (c < 128) s[i] = static_cast<char>(std::tolower(c));
        else if (c == 0xC3 && i + 1 < s.size()) {
            unsigned char d = s[i + 1];
            if (d >= 0x80 && d <= 0x9E && d != 0x97) s[i + 1] = static_cast<char>(d + 0x20);
            ++i;
        }
    }
    return s;
}

// Universal Dependencies: as formas como se escrevem ("do", não "de o"), sem pontuação
inline std::vector<RealSentence> read_conllu(const std::string& path, size_t max_sentences = 1000000, size_t max_words = 20) {
    std::vector<RealSentence> out;
    std::ifstream in(path);
    std::string line;
    RealSentence cur;
    int skip_until = 0;
    std::string merged_pos;
    auto flush = [&] {
        if (!cur.words.empty() && cur.words.size() <= max_words) out.push_back(cur);
        cur = {};
        skip_until = 0;
    };
    while (std::getline(in, line) && out.size() < max_sentences) {
        if (line.empty()) { flush(); continue; }
        if (line[0] == '#') continue;
        std::vector<std::string> f;
        std::istringstream ls(line);
        for (std::string x; std::getline(ls, x, '\t');) f.push_back(x);
        if (f.size() < 4) continue;
        const std::string& id = f[0];
        if (id.find('.') != std::string::npos) continue;  // nós vazios
        const auto dash = id.find('-');
        if (dash != std::string::npos) {  // palavra contraída ("do" = de + o): fica a forma escrita
            skip_until = std::stoi(id.substr(dash + 1));
            cur.words.push_back(lower_utf8(f[1]));
            cur.pos.push_back("CONTR");
            merged_pos.clear();
            continue;
        }
        const int n = std::stoi(id);
        if (n <= skip_until) {  // as partes da contraída: a classe vira "ADP+DET"
            merged_pos += (merged_pos.empty() ? "" : "+") + f[3];
            cur.pos.back() = merged_pos;
            continue;
        }
        if (f[3] == "PUNCT" || f[3] == "SYM" || f[3] == "X") continue;
        cur.words.push_back(lower_utf8(f[1]));
        cur.pos.push_back(f[3]);
    }
    flush();
    return out;
}

// CHAT (CHILDES): a fala dos adultos de todos os .cha de uma pasta (e subpastas)
inline void read_chat_file(const std::string& path, std::vector<RealSentence>& out, size_t max_words) {
    std::ifstream in(path);
    std::string line, utt;
    auto finish = [&](std::string u) {
        if (u.empty()) return;
        const std::string who = u.substr(1, u.find(':') - 1);
        if (who == "CHI") return;  // só os adultos: o que a criança ouve
        u = u.substr(u.find(':') + 1);
        // repetições e correções ("<onde está> [/] onde está a bola"): fica só a versão final
        for (size_t at = u.find("[/"); at != std::string::npos; at = u.find("[/")) {
            const size_t close = u.find(']', at);
            size_t start = at;
            while (start > 0 && u[start - 1] == ' ') --start;
            if (start > 0 && u[start - 1] == '>') {
                const size_t open = u.rfind('<', start);
                start = open == std::string::npos ? start : open;
            } else {
                while (start > 0 && u[start - 1] != ' ') --start;
            }
            u.erase(start, (close == std::string::npos ? u.size() : close + 1) - start);
        }
        // tira as marcas: [comentários], <trechos repetidos>, &=gestos, +/. etc.
        std::string clean;
        int depth = 0;
        for (char c : u) {
            if (c == '[') { ++depth; continue; }
            if (c == ']') { if (depth) --depth; continue; }
            if (depth) continue;
            if (c == '<' || c == '>' || c == '(' || c == ')') continue;
            clean += c;
        }
        RealSentence s;
        bool question = false;
        std::istringstream ws(clean);
        for (std::string w; ws >> w;) {
            if (w == "?" || w == "+?" || w == "+/?") { question = true; continue; }
            if (w[0] == '&' || w[0] == '+' || w[0] == '0' || w == "xxx" || w == "yyy" || w == "www" || w == "." || w == "!" || w == "," || w[0] == '\x15') continue;
            const auto at = w.find('@');
            if (at != std::string::npos) w = w.substr(0, at);
            w.erase(std::remove_if(w.begin(), w.end(), [](char c) { return c == ':' || c == '^' || c == '/' || c == '"' || c == ',' || c == '.' || c == '!' || c == '?'; }), w.end());
            std::replace(w.begin(), w.end(), '_', ' ');
            std::istringstream parts(w);
            for (std::string p; parts >> p;) s.words.push_back(lower_utf8(p));
        }
        if (!s.words.empty() && s.words.size() <= max_words) {
            if (question) s.words.push_back("<?>");
            out.push_back(s);
        }
    };
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line[0] == '*') { finish(utt); utt = line; }
        else if (line[0] == '\t' && !utt.empty()) utt += " " + line.substr(1);  // continuação
        else if (line[0] == '%' || line[0] == '@') { finish(utt); utt.clear(); }
    }
    finish(utt);
}

inline std::vector<RealSentence> read_chat(const std::string& dir, size_t max_words = 20) {
    std::vector<RealSentence> out;
    std::vector<std::string> stack{dir};
    std::vector<std::string> files;
    while (!stack.empty()) {
        const std::string d = stack.back();
        stack.pop_back();
        if (DIR* h = opendir(d.c_str())) {
            while (dirent* e = readdir(h)) {
                const std::string n = e->d_name;
                if (n == "." || n == "..") continue;
                const std::string p = d + "/" + n;
                if (n.size() > 4 && n.substr(n.size() - 4) == ".cha") files.push_back(p);
                else if (e->d_type == DT_DIR) stack.push_back(p);
            }
            closedir(h);
        }
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) read_chat_file(f, out, max_words);
    return out;
}

}  // namespace eva2
