// Eva - demonstração e modo de conversa.
//   ./build/eva            demonstração automática
//   ./build/eva --conversa conversar com a Eva no terminal
//
// Não há moldes de texto. Tudo que a Eva diz é uma cadeia de palavras que saiu
// do hipocampo dela, puxada pelo que ela está sentindo (interocepção) ou pelo
// que acabou de ouvir. Os colchetes mostram o estado interno na hora de falar.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "eva.hpp"

using eva::Eva;
using eva::Utterance;

static const char* kind_name(Utterance::Kind k) {
    switch (k) {
        case Utterance::Kind::Reply: return "resposta";
        case Utterance::Kind::Thought: return "pensamento";
        case Utterance::Kind::Question: return "pergunta";
        case Utterance::Kind::Echo: return "eco";
    }
    return "";
}

static void say(const Utterance& u, bool show_time) {
    if (show_time) std::printf("  [%5.1fs]", u.t_ms / 1000.0);
    else std::printf("        ");
    std::printf(" Eva: %s", u.text().c_str());
    std::printf("   [%s", kind_name(u.kind));
    for (auto& s : u.state) std::printf(" %s", s.c_str());
    std::printf("]\n");
}

static void live_and_talk(Eva& e, int ms) {
    for (const auto& u : e.live(ms)) say(u, true);
}

static void hear(Eva& e, const std::vector<std::string>& words, float reward = 0.0f) {
    std::printf("Voce:");
    for (auto& w : words) std::printf(" %s", w.c_str());
    if (reward > 0) std::printf("  (com carinho)");
    auto ex = e.tell(words, reward);
    std::printf("\n        (surpresa %.2f, dopamina %.2f", ex.surprise, ex.dopamine);
    for (auto& n : ex.new_words) std::printf(", nova: %s", n.c_str());
    std::printf(")\n");
    for (const auto& u : e.respond()) say(u, false);
}

// ------------------------------------------------------------------ demo

static void demo() {
    Eva eva;
    std::printf("=== Eva: dialogo que nasce do estado interno ===\n\n");
    std::printf("[Manha] Voce conversa com a Eva. Ela ainda nao sabe nada.\n");
    hear(eva, {"bom", "dia", "eva"});
    hear(eva, {"eu", "gosto", "de", "musica"}, 1.0f);
    hear(eva, {"vou", "dormir", "cedo"});
    hear(eva, {"bom", "dia", "eva"});
    hear(eva, {"eu", "gosto", "de", "musica"}, 1.0f);
    hear(eva, {"vou", "dormir", "cedo"});
    std::printf("\n[Tarde] Silencio por 30 segundos.\n");
    live_and_talk(eva, 30000);
    std::printf("\n[Fim de tarde] Voce volta com carinho: a dopamina sobe, e <feliz> acende.\n");
    std::printf("        O que ela ouviu enquanto estava feliz deve voltar.\n");
    hear(eva, {"oi", "eva"}, 1.5f);
    hear(eva, {"tudo", "bem"}, 1.0f);
    std::printf("\n[Noite] Eva dorme 20 s simulados.\n");
    eva.sleep(20000);
    std::printf("\n[Manha seguinte]\n");
    hear(eva, {"bom", "dia", "eva"});
    live_and_talk(eva, 15000);
    std::printf("\nFim.\n");
}

// ------------------------------------------------------------------ conversa

static void help() {
    std::printf(
        "\nComo conversar com a Eva:\n"
        "  uma frase curta [!]     fale com ela (ate 6 palavras); '!' = com carinho\n"
        "  (linha vazia)           fique em silencio 4 s; ela pode puxar assunto\n"
        "  esperar N               fique N segundos em silencio\n"
        "  dormir [N]              ela dorme N segundos simulados (padrao 30)\n"
        "  recitar palavra         de a primeira palavra e ela completa a frase\n"
        "  lembrar palavra         o que a palavra acende na cabeca dela (associacao)\n"
        "  estado                  dopamina, impulsos, estados e palavras conhecidas\n"
        "  sair\n\n"
        "Tudo que ela diz e uma cadeia que ela aprendeu de voce, puxada pelo que ela\n"
        "esta sentindo. No comeco ela repete pedacos do que ouviu, como uma crianca.\n"
        "Os colchetes mostram o tipo da fala e o estado interno na hora.\n\n");
}

static void converse() {
    Eva eva;
    std::printf("=== Conversa com a Eva ===\n");
    help();
    std::string line;
    while (true) {
        std::printf("Voce: ");
        std::fflush(stdout);
        if (!std::getline(std::cin, line)) break;
        std::istringstream in(line);
        std::vector<std::string> w;
        for (std::string s; in >> s;) w.push_back(s);
        if (w.empty()) { live_and_talk(eva, 4000); continue; }

        if (w[0] == "sair") break;
        if (w[0] == "ajuda") { help(); continue; }
        if (w[0] == "esperar") {
            int s = w.size() > 1 ? std::atoi(w[1].c_str()) : 10;
            live_and_talk(eva, s * 1000);
            continue;
        }
        if (w[0] == "dormir") {
            int s = w.size() > 1 ? std::atoi(w[1].c_str()) : 30;
            std::printf("        (Eva dorme %d s...)\n", s);
            eva.sleep(s * 1000);
            live_and_talk(eva, 4000);
            continue;
        }
        if (w[0] == "recitar" && w.size() > 1) {
            if (!eva.knows(w[1])) { std::printf("        (ela nunca ouviu '%s')\n", w[1].c_str()); continue; }
            auto seq = eva.recite(w[1]);
            std::printf("        Eva:");
            for (auto& x : seq) std::printf(" %s", x.c_str());
            std::printf("%s\n", seq.size() > 1 ? "" : "   (so a pista; nao lembra o que vinha depois)");
            continue;
        }
        if (w[0] == "lembrar" && w.size() > 1) {
            if (!eva.knows(w[1])) { std::printf("        (ela nunca ouviu '%s')\n", w[1].c_str()); continue; }
            auto r = eva.recall(w[1], true);
            std::printf("        '%s' acende:", w[1].c_str());
            bool any = false;
            for (size_t i = 0; i < r.spikes_per_concept.size(); ++i)
                if (eva.concepts()[i] != w[1] && !eva.is_internal(eva.concepts()[i]) && r.spikes_per_concept[i] >= 12) {
                    std::printf(" %s(%d)", eva.concepts()[i].c_str(), r.spikes_per_concept[i]);
                    any = true;
                }
            std::printf("%s\n", any ? "" : " nada alem dela mesma");
            continue;
        }
        if (w[0] == "estado") {
            std::printf("        dopamina %.2f | vontade de falar %.2f | curiosidade %.2f | estados:",
                        eva.dopamine(), eva.drives().talk, eva.drives().curiosity);
            auto st = eva.active_states();
            if (st.empty()) std::printf(" (nenhum)");
            for (auto& s : st) std::printf(" %s", s.c_str());
            std::printf("\n        palavras:");
            for (auto& c : eva.concepts())
                if (!eva.is_internal(c)) std::printf(" %s(%.2f)", c.c_str(), eva.knowledge(c));
            std::printf("\n");
            continue;
        }

        float reward = 0.0f;
        if (w.back() == "!") { reward = 1.0f; w.pop_back(); }
        if (w.size() > 6) w.resize(6);
        auto ex = eva.tell(w, reward);
        if (ex.concepts.empty()) { std::printf("        (a cabeca dela esta cheia; nao cabem palavras novas)\n"); continue; }
        std::printf("        (surpresa %.2f, dopamina %.2f", ex.surprise, ex.dopamine);
        for (auto& n : ex.new_words) std::printf(", nova: %s", n.c_str());
        std::printf(")\n");
        for (const auto& u : eva.respond()) say(u, false);
        live_and_talk(eva, 3000);
    }
    std::printf("        (fim)\n");
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--conversa") == 0) converse();
    else demo();
    return 0;
}
