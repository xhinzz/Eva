// Eva 2 - o mundo dela.
//
// Sentido não vem de palavras ligadas a palavras; vem de palavras ligadas ao
// que se percebe e ao que se faz. Este é um mundo pequeno onde a Eva está em
// algum lugar, vê coisas, sente fome e cansaço, e pode agir. Tudo isso entra
// no córtex dela como "palavras internas" no mesmo fluxo da conversa:
//   percepções  [ve:mar] [esta:praia] [tempo:noite] [sente:fome]
//   ações       {ir:cozinha} {pegar:pao} {comer} {olhar}
// Se você diz "olha o mar" enquanto ela vê o mar, "mar" e [ve:mar] ficam
// ligados. Depois, ver o mar prevê a palavra "mar": ela nomeia o que vê.
#pragma once
#include <algorithm>
#include <deque>
#include <map>
#include <string>
#include <vector>
#include <random>

#include "vision.hpp"

namespace eva2 {

struct Place {
    std::string name;
    std::vector<std::string> objects;
    std::vector<std::string> exits;
};

struct World {
    std::map<std::string, Place> places;
    std::string here = "quarto";
    std::string holding;          // objeto na mão (ou vazio)
    int tick = 0;                 // avança a cada turno
    float hunger = 0.2f;          // 0..1
    float tiredness = 0.1f;       // 0..1
    std::vector<std::string> edible{"pao", "fruta", "bolo", "cafe"};

    World() {
        places["quarto"] = {"quarto", {"cama", "janela", "livro"}, {"cozinha", "jardim"}};
        places["cozinha"] = {"cozinha", {"mesa", "pao", "cafe", "fruta"}, {"quarto", "jardim"}};
        places["jardim"] = {"jardim", {"flor", "arvore", "passaro"}, {"quarto", "cozinha", "praia"}};
        places["praia"] = {"praia", {"mar", "areia", "sol", "barco"}, {"jardim"}};
    }

    std::string time_of_day() const {
        const int t = (tick / 6) % 3;
        return t == 0 ? "manha" : t == 1 ? "tarde" : "noite";
    }

    // O que ela percebe agora SEM os olhos (corpo e lugar): palavras internas
    std::vector<std::string> perceive() const {
        std::vector<std::string> p{"[esta:" + here + "]", "[tempo:" + time_of_day() + "]"};
        if (!holding.empty()) p.push_back("[tem:" + holding + "]");
        if (hunger > 0.6f) p.push_back("[sente:fome]");
        if (tiredness > 0.7f) p.push_back("[sente:cansaco]");
        return p;
    }
    // O que os olhos dela veem: cada coisa visível, desenhada (nunca igual duas vezes)
    std::vector<std::pair<std::string, Image>> look(std::mt19937& rng) const {
        std::vector<std::pair<std::string, Image>> out;
        for (const auto& o : places.at(here).objects) out.push_back({o, draw_object(o, rng, 1.0f)});
        if (!holding.empty()) out.push_back({holding, draw_object(holding, rng, 1.0f)});
        return out;
    }
    std::vector<std::string> all_objects() const {
        std::vector<std::string> out;
        for (const auto& [n, pl] : places) for (const auto& o : pl.objects) out.push_back(o);
        return out;
    }

    // O tempo passa: fome e cansaço crescem, e algumas coisas mudam de lugar.
    // Coisas que se movem são o que permite aprender o que uma palavra NÃO é:
    // "passaro" aparece no jardim e na praia, mas "areia" só na praia.
    std::vector<std::string> mobile{"passaro", "livro", "pao", "fruta"};
    void pass_turn() {
        ++tick;
        hunger = std::min(1.0f, hunger + 0.08f);
        tiredness = std::min(1.0f, tiredness + 0.05f);
        if (tick % 3 == 0) {
            const std::string obj = mobile[(tick / 3) % mobile.size()];
            // tira de onde está e põe num lugar vizinho de onde costuma ficar
            std::string from;
            for (auto& [name, pl] : places) {
                auto it = std::find(pl.objects.begin(), pl.objects.end(), obj);
                if (it != pl.objects.end()) { pl.objects.erase(it); from = name; break; }
            }
            if (from.empty()) from = "cozinha";
            const auto& ex = places[from].exits;
            const std::string to = ex[(tick / 3) % ex.size()];
            places[to].objects.push_back(obj);
        }
    }

    // Executa uma ação. Devolve a recompensa (dopamina) que o corpo dela sente.
    // Ações possíveis: {ir:lugar} {pegar:objeto} {comer} {dormir} {olhar}
    float act(const std::string& action, std::string* result) {
        auto fail = [&](const std::string& why) { if (result) *result = why; return 0.0f; };
        if (action.rfind("{ir:", 0) == 0) {
            const std::string dest = action.substr(4, action.size() - 5);
            const auto& ex = places[here].exits;
            for (const auto& e : ex)
                if (e == dest) { here = dest; if (result) *result = "foi para " + dest; return 0.1f; }
            return fail("nao da para ir de " + here + " para " + dest);
        }
        if (action.rfind("{pegar:", 0) == 0) {
            const std::string obj = action.substr(7, action.size() - 8);
            for (const auto& o : places[here].objects)
                if (o == obj && (o == "pao" || o == "fruta" || o == "livro" || o == "flor" || o == "bolo" || o == "cafe")) {
                    holding = obj;
                    if (result) *result = "pegou " + obj;
                    return 0.1f;
                }
            return fail("nao tem " + obj + " aqui para pegar");
        }
        if (action == "{comer}") {
            for (const auto& e : edible)
                if (holding == e) {
                    const float reward = hunger > 0.6f ? 1.0f : 0.2f;  // comer com fome e muito bom
                    hunger = std::max(0.0f, hunger - 0.6f);
                    if (result) *result = "comeu " + holding;
                    holding.clear();
                    return reward;
                }
            return fail("nao tem o que comer");
        }
        if (action == "{dormir}") {
            if (here != "quarto") return fail("so dorme no quarto");
            const float reward = tiredness > 0.7f ? 0.8f : 0.1f;
            tiredness = 0.0f;
            if (result) *result = "dormiu";
            return reward;
        }
        if (action == "{olhar}") { if (result) *result = "olhou em volta"; return 0.05f; }
        return fail("nao sabe fazer " + action);
    }

    // Primeiro passo no caminho até `dest` (busca em largura), ou vazio
    std::string next_step_towards(const std::string& dest) const {
        if (dest == here || !places.count(dest)) return "";
        std::map<std::string, std::string> parent;
        std::deque<std::string> q{here};
        parent[here] = here;
        while (!q.empty()) {
            const std::string cur = q.front(); q.pop_front();
            for (const auto& e : places.at(cur).exits)
                if (!parent.count(e)) { parent[e] = cur; q.push_back(e); }
        }
        if (!parent.count(dest)) return "";
        std::string step = dest;
        while (parent[step] != here) step = parent[step];
        return step;
    }

    // Todas as ações que fazem sentido aqui e agora (o repertório motor dela)
    std::vector<std::string> possible_actions() const {
        std::vector<std::string> a{"{olhar}"};
        for (const auto& e : places.at(here).exits) a.push_back("{ir:" + e + "}");
        for (const auto& o : places.at(here).objects)
            if (o != holding && (o == "pao" || o == "fruta" || o == "livro" || o == "flor" || o == "bolo" || o == "cafe")) a.push_back("{pegar:" + o + "}");
        if (!holding.empty()) a.push_back("{comer}");
        if (here == "quarto") a.push_back("{dormir}");
        return a;
    }

    static bool is_perception(const std::string& t) { return !t.empty() && t[0] == '['; }
    static bool is_action(const std::string& t) { return !t.empty() && t[0] == '{'; }
    static std::string label(const std::string& t) {  // "[ve:mar]" -> "mar"
        const auto c = t.find(':');
        if (c == std::string::npos) return t;
        return t.substr(c + 1, t.size() - c - 2);
    }
};

}  // namespace eva2
