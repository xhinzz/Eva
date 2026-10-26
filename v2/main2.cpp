// Eva 2 - conversa.
//   ./build/eva2             conversar (ela lembra de conversas anteriores)
//   ./build/eva2 --demo      demonstração automática
//   ./build/eva2 --nova      começa sem memória
#include <algorithm>
#include <set>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "childhood.hpp"
#include "corpus.hpp"
#include "eva2.hpp"

using eva2::Eva2;
using eva2::Utterance;

static const char* kind_name(Utterance::Kind k) {
    switch (k) {
        case Utterance::Kind::Reply: return "resposta";
        case Utterance::Kind::Echo: return "eco";
        case Utterance::Kind::Thought: return "pensamento";
        case Utterance::Kind::Question: return "pergunta";
        case Utterance::Kind::Point: return "aponta";
    }
    return "";
}

static bool g_quiet = false;  // dias repetidos da demo: sem imprimir

static void say(const Utterance& u) {
    if (g_quiet) return;
    if (u.kind == Utterance::Kind::Point) {
        std::printf("         Eva: (aponta para %s)   [aponta", u.words[0].c_str());
        for (auto& s : u.state) std::printf(" %s", s.c_str());
        std::printf("]\n");
        return;
    }
    std::printf("         Eva: %s   [%s", u.text().c_str(), kind_name(u.kind));
    for (auto& s : u.state) std::printf(" %s", s.c_str());
    std::printf("]\n");
}

static void show_actions(const std::vector<eva2::Action>& acts) {
    if (g_quiet) return;
    for (auto& a : acts) std::printf("         Eva %s   [%s%s]\n", a.result.c_str(), a.token.c_str(), a.reward >= 0.5f ? ", recompensa" : "");
}

static void show_world(const Eva2& e) {
    if (g_quiet) return;
    const auto& w = e.world();
    std::printf("        [%s, %s] ve:", w.here.c_str(), w.time_of_day().c_str());
    for (auto& o : w.places.at(w.here).objects) std::printf(" %s", o.c_str());
    if (!w.holding.empty()) std::printf(" | na mao: %s", w.holding.c_str());
    std::printf(" | fome %.0f%% cansaco %.0f%%\n", 100 * w.hunger, 100 * w.tiredness);
}

static void hear(Eva2& e, const std::vector<std::string>& words, float reward, bool echo_input) {
    if (echo_input && !g_quiet) {
        std::printf("Voce:");
        for (auto& w : words) std::printf(" %s", w.c_str());
        if (reward > 0) std::printf("  (com carinho)");
        std::printf("\n");
    }
    auto h = e.hear(words, reward);
    if (!g_quiet) {
        std::printf("        (surpresa %.2f, dopamina %.2f", h.surprise, h.dopamine);
        for (auto& n : h.new_words) std::printf(", nova: %s", n.c_str());
        std::printf(")\n");
    }
    for (auto& u : e.respond()) say(u);
    show_actions(e.act());
    for (auto& u : e.idle(3000)) say(u);
}

static void point_and_say(Eva2& e, const char* obj, std::vector<std::string> words, float reward = 0) {
    std::string r;
    e.point_at(obj, &r);
    if (!g_quiet) std::printf("        (voce aponta para %s)\n", obj);
    hear(e, words, reward, true);
}

static void demo() {
    eva2::Eva2Config c;
    c.memory_file = "eva2_demo.bin";
    c.diary_file = "eva2_demo_diario.txt";
    Eva2 e(c);
    std::string r;
    std::printf("=== Eva 2: sentido, com olhos ===\n\n[Dia 1] Voce anda com ela pela casa, apontando e nomeando. (Dias 2 e 3: a mesma volta, sem imprimir;\n        cada dia ela ve desenhos diferentes das mesmas coisas.)\n");
    for (int day = 1; day <= 3; ++day) {
        g_quiet = day > 1;
        e.take_to("quarto", &r); show_world(e);
        hear(e, {"estamos", "no", "quarto"}, 0, true);
        point_and_say(e, "cama", {"olha", "a", "cama"});
        point_and_say(e, "janela", {"olha", "a", "janela"});
        e.take_to("jardim", &r); if (!g_quiet) std::printf("        (%s)\n", r.c_str()); show_world(e);
        hear(e, {"estamos", "no", "jardim"}, 0, true);
        point_and_say(e, "flor", {"olha", "a", "flor"}, 1.0f);
        point_and_say(e, "arvore", {"essa", "e", "a", "arvore"});
        e.take_to("praia", &r); if (!g_quiet) std::printf("        (%s)\n", r.c_str()); show_world(e);
        point_and_say(e, "mar", {"olha", "o", "mar"}, 1.0f);
        hear(e, {"o", "mar", "e", "bonito"}, 1.0f, true);
        hear(e, {"a", "praia", "e", "linda"}, 1.0f, true);
        e.take_to("cozinha", &r); if (!g_quiet) std::printf("        (%s)\n", r.c_str()); show_world(e);
        hear(e, {"estamos", "na", "cozinha"}, 0, true);
        point_and_say(e, "pao", {"olha", "o", "pao"});
        point_and_say(e, "cafe", {"isso", "e", "cafe"});
        e.take_to("praia", &r); hear(e, {"estamos", "na", "praia"}, 1.0f, true); hear(e, {"eu", "gosto", "da", "praia"}, 0, true);
        e.take_to("quarto", &r); hear(e, {"a", "cama", "e", "boa"}, 0, true);
    }
    g_quiet = false;

    std::printf("\n[Teste de sentido] So a percepcao, sem conversa. O que ela diz vendo cada coisa?\n");
    for (auto o : {"cama", "janela", "flor", "arvore", "mar", "pao", "cafe", "passaro"}) {
        auto w = e.name_what_you_see(o);
        std::printf("  ve %-8s -> ", o);
        if (w.empty()) std::printf("(nada; nao sabe nomear)\n");
        else { for (auto& x : w) std::printf("%s ", x.c_str()); std::printf("\n"); }
    }
    std::printf("\n[O que as palavras evocam nela]\n");
    for (auto wd : {"praia", "mar", "flor", "cama"}) {
        auto ps = e.imagine(wd);
        std::printf("  '%-6s' -> ", wd);
        if (ps.empty()) std::printf("(nada ainda)");
        for (size_t i = 0; i < ps.size() && i < 3; ++i) std::printf("%s(%.2f) ", ps[i].first.c_str(), ps[i].second);
        std::printf("\n");
    }

    std::printf("\n[Imaginar] A imagem que cada palavra evoca nela (demo_imagina.ppm: mar, flor, cama, pao, e 'flor mar' misturados)\n");
    {
        std::vector<eva2::Image> row;
        for (std::vector<std::string> ws : {std::vector<std::string>{"mar"}, {"flor"}, {"cama"}, {"pao"}, {"flor", "mar"}}) {
            float conf = 0.0f;
            row.push_back(e.imagine_image(ws, &conf));
            std::printf("  '%s%s' -> forca %.2f\n", ws[0].c_str(), ws.size() > 1 ? (" " + ws[1]).c_str() : "", conf);
        }
        eva2::write_ppm("demo_imagina.ppm", row, 6);
    }

    std::printf("\n[Compreensao] Ela esta no quarto. Voce diz 'vamos para a praia'.\n");
    show_world(e);
    hear(e, {"vamos", "para", "a", "praia"}, 0, true);
    for (int i = 0; i < 3; ++i) { show_actions(e.act()); for (auto& u : e.idle(3000)) say(u); }
    show_world(e);

    std::printf("\n[Sozinha na praia] 20 s de silencio. O que ela diz, vendo o mar?\n");
    for (auto& u : e.idle(20000)) say(u);

    std::printf("\n[Fome] Voce a leva a cozinha e fica quieto.\n");
    e.take_to("cozinha", &r);
    for (int i = 0; i < 4; ++i) { show_actions(e.act()); for (auto& u : e.idle(8000)) say(u); }
    show_world(e);
    std::printf("\nsegmentos: %d | turnos: %zu | ligacoes de sentido: %zu\n", e.segments(), e.turns(), e.meaning().size());
}

static void help() {
    std::printf(
        "\nComo conversar com a Eva 2:\n"
        "  uma frase [!]           fale com ela (qualquer tamanho); '!' = com carinho\n"
        "  (linha vazia)           4 s de silencio; ela pode pensar alto\n"
        "  esperar N               N segundos de silencio\n"
        "  dormir [N]              ela dorme e repete N trechos de conversa (padrao 30)\n"
        "  completar a b c         de um comeco e o cortex dela segue\n"
        "  /leva lugar             leve-a para quarto, cozinha, jardim ou praia\n"
        "  /da objeto              ponha um objeto na mao dela (pao, fruta, flor, livro...)\n"
        "  /ver                    o que ela ve e sente agora\n"
        "  /aponta objeto          aponte para algo que ela ve; a proxima frase sua cola nele\n"
        "  /nomear objeto          o que ela diria vendo so esse objeto (teste de sentido)\n"
        "  /sentido palavra        o que a palavra evoca nela (percepcoes, com forca)\n"
        "  /ouvir N                ela vive N momentos da casa: frases, cenas, perguntas (e dorme a cada 2000)\n"
        "  /categoria palavra      de que especie ela acha que a palavra e (as mais parecidas)\n"
        "  /tema                   do que ela acha que se esta falando agora\n"
        "  /cena                   uma coisa acontece na frente dela e voce descreve (ela guarda o episodio)\n"
        "  /conta                  ela descreve, com as palavras dela, a ultima cena que viu\n"
        "  (perguntas)             'cadê o gato', 'o que o gato come', 'quem come o peixe': ela responde pela memoria\n"
        "  (sim ou nao)            'o gato comeu o peixe?' (com '?'): 'sim ...', 'nao, ...' ou 'nao sei'\n"
        "  (contar)                'o urso esta na praia', 'o meu gato chama mimi': ela guarda; depois\n"
        "                          'cadê o urso', 'como chama o meu gato' ('o seu gato chama mimi')\n"
        "  (sentimento)            'como você está', 'você está triste?'\n"
        "  /imaginar palavras...   a imagem que as palavras evocam nela (varias = mistura) -> imaginou.ppm\n"
        "  /olhar                  o que os olhos dela veem agora e como ela lembra cada coisa -> olhar.ppm\n"
        "  estado                  dopamina, humor, impulsos, estados, tamanho da memoria\n"
        "  sair                    salva a memoria e sai\n\n"
        "Tudo que ela diz e a previsao do cortex dela para a proxima palavra, dado\n"
        "tudo que veio antes. Ela aprende com voce e com ela mesma. A memoria fica\n"
        "em eva2_memoria.bin; o diario da conversa, em eva2_diario.txt.\n\n");
}

// Infância: ela ouve a fala de uma casa (corpus.hpp: ~770 palavras, frases de
// quem fala com criança) por "dias", dormindo toda noite. É assim que ela
// chega à conversa com vocabulário e gramática, em vez de só com o que você
// disser. Ninguém ensina regra: ela só ouve.
static corpus::Grammar& home_speech() { static corpus::Grammar g(7); return g; }
using eva2::scene_of;
static std::vector<std::string> g_things, g_places;
static void collect_names() {
    std::set<std::string> got;
    for (const auto& en : home_speech().lexicon())
        if (en.pos == corpus::NOUN && en.n == 's' && !got.count(en.lemma)) { got.insert(en.lemma); (en.sem & corpus::PLACE ? g_places : g_things).push_back(en.lemma); }
    eva2::set_sprite_names(g_things);  // cada coisa com uma aparência única
}
static std::mt19937& view_rng() { static std::mt19937 r(101); return r; }
// três olhares na mesma coisa durante a cena (os olhos se mexem, a coisa se mexe)
static std::vector<eva2::Image> look(const std::string& lemma) {
    std::vector<eva2::Image> g;
    for (int k = 0; k < 3; ++k) g.push_back(eva2::draw_sprite(lemma, view_rng()));
    return g;
}
// a cena como ela a percebe: pelos olhos (se abertos) ou como símbolos
static eva2::Scene perceive(Eva2& e, const corpus::Grammar::Event& ev, const std::vector<std::string>& words, std::vector<eva2::Image>* shown = nullptr) {
    if (!e.eyes_open()) return scene_of(ev);
    eva2::Scene s = scene_of(ev);
    auto a = look(ev.agent);
    s.agent = e.see_thing(a, true, words);
    if (shown) shown->push_back(a[0]);
    if (!ev.patient.empty()) { auto p = look(ev.patient); s.patient = e.see_thing(p, true, words); if (shown) shown->push_back(p[0]); }
    std::vector<eva2::Image> l;
    for (int k = 0; k < 3; ++k) l.push_back(eva2::draw_backdrop(ev.place, view_rng()));
    s.place = e.see_place(l, true, words);
    if (shown) shown->push_back(l[0]);
    return s;
}
static void open_eyes(Eva2& e) {
    std::vector<eva2::Image> samples;
    std::mt19937 rr(3);
    for (int k = 0; k < 2; ++k) {
        for (const auto& t : g_things) samples.push_back(eva2::draw_sprite(t, rr));
        for (const auto& p : g_places) samples.push_back(eva2::draw_backdrop(p, rr));
    }
    std::printf("        (infância dos olhos: ela vê %zu imagens das coisas e lugares da casa, sem nomes)\n", samples.size());
    std::fflush(stdout);
    e.open_eyes(samples);
}

// Infância: ela vive os momentos de uma casa (childhood.hpp): ouve falar, vê
// cenas e ouve a descrição, perguntas e respostas (cadê, o que, quem, sim ou não),
// livros, nomes das coisas de cada um, o que alguém conta, sentimentos. Dorme a
// cada 2000 momentos.
static void grow(Eva2& e, int moments, unsigned seed) {
    auto see = [](Eva2& ev2, const corpus::Grammar::Event& ev, const std::vector<std::string>& words, bool) { return perceive(ev2, ev, words); };
    auto see_thing = [](Eva2& ev2, const std::string& lemma, const std::vector<std::string>& words) {
        return ev2.eyes_open() ? ev2.see_thing(look(lemma), true, words) : lemma;
    };
    eva2::Childhood life(home_speech(), seed, see, see_thing);
    life.live(e, moments, [&](int day, int today, int done) {
        size_t words = 0;
        for (size_t k = 0; k < e.vocab().words.size(); ++k) words += !eva2::Vocabulary::internal(e.vocab().words[k]);
        std::printf("        dia %d: %d momentos (total %d); %zu palavras, %zu com categoria; %zu episódios, %zu fatos%s; %.1f mi de sinapses\n",
                    day, today, done, words, e.categories().words_with_code(), e.episodes().size(), e.facts().size(),
                    e.eyes_open() ? (", " + std::to_string(e.thing_concepts()) + " conceitos visuais").c_str() : "",
                    (e.cortex().n_synapses() + e.category_cortex().n_synapses()) / 1e6);
        std::fflush(stdout);
    });
}

static void show_category(const Eva2& e, const std::string& w) {
    const auto& v = e.vocab();
    const auto& c = e.categories();
    const int a = v.id(w);
    if (a < 0 || !c.has_code(a)) { std::printf("         (%s ainda nao tem categoria: ouviu %.0f vezes)\n", w.c_str(), c.count(a)); return; }
    std::vector<std::pair<float, int>> sims;
    for (int b = 0; b < static_cast<int>(v.words.size()); ++b)
        if (b != a && c.has_code(b) && !eva2::Vocabulary::internal(v.words[b])) sims.push_back({c.similarity(a, b), b});
    std::sort(sims.rbegin(), sims.rend());
    std::printf("         '%s' (ouviu %.0f vezes) e da especie de:", w.c_str(), c.count(a));
    for (size_t i = 0; i < sims.size() && i < 8; ++i) std::printf(" %s(%.2f)", v.words[sims[i].second].c_str(), sims[i].first);
    std::printf("\n");
}

static eva2::Eva2Config config_for(bool big) {
    eva2::Eva2Config c;
    if (big) c = eva2::Eva2Config::big();
    return c;
}

static void converse(bool fresh, bool big, int childhood, bool eyes) {
    collect_names();
    Eva2 e(config_for(big));
    if (!fresh && e.load()) std::printf("(Eva lembra: %zu turnos, %zu palavras, %d segmentos)\n", e.turns(), e.vocab().words.size(), e.segments());
    else std::printf("(Eva nova, sem memoria)\n");
    if (eyes && !e.eyes_open()) open_eyes(e);
    if (childhood > 0) {
        std::printf("(infancia: ela vai viver %d momentos da casa (frases, cenas, perguntas), dormindo a cada 2000)\n", childhood);
        grow(e, childhood, 7);
        e.save();
        std::printf("        (memoria salva)\n");
    }
    help();
    std::string line;
    while (true) {
        std::printf("Voce: ");
        std::fflush(stdout);
        if (!std::getline(std::cin, line)) break;
        std::istringstream in(line);
        std::vector<std::string> w;
        for (std::string s; in >> s;) w.push_back(s);
        if (w.empty()) { for (auto& u : e.idle(4000)) say(u); show_actions(e.act()); continue; }
        if (w[0] == "sair") break;
        if (w[0] == "ajuda") { help(); continue; }
        if (w[0] == "esperar") { for (auto& u : e.idle((w.size() > 1 ? std::atoi(w[1].c_str()) : 10) * 1000)) say(u); continue; }
        if (w[0] == "dormir") {
            int n = w.size() > 1 ? std::atoi(w[1].c_str()) : 30;
            e.sleep(n);
            std::printf("        (dormiu; repetiu %d trechos)\n", n);
            for (auto& u : e.idle(4000)) say(u);
            continue;
        }
        if (w[0] == "completar" && w.size() > 1) {
            std::vector<std::string> start(w.begin() + 1, w.end());
            std::printf("         Eva:");
            for (auto& x : start) std::printf(" %s", x.c_str());
            std::printf(" ...");
            for (auto& x : e.complete(start)) std::printf(" %s", x.c_str());
            std::printf("\n");
            continue;
        }
        if (w[0] == "/leva" && w.size() > 1) { std::string r; e.take_to(w[1], &r); std::printf("        (%s)\n", r.c_str()); show_world(e); continue; }
        if (w[0] == "/da" && w.size() > 1) { std::string r; e.give(w[1], &r); std::printf("        (%s)\n", r.c_str()); continue; }
        if (w[0] == "/ver") { show_world(e); continue; }
        if (w[0] == "/aponta" && w.size() > 1) { std::string r; e.point_at(w[1], &r); std::printf("        (%s)\n", r.c_str()); continue; }
        if (w[0] == "/sentido" && w.size() > 1) {
            auto ps = e.meaning().perceptions_for(w[1], 0.05f);
            std::printf("         '%s' evoca:", w[1].c_str());
            if (ps.empty()) std::printf(" nada");
            for (auto& [p, sc] : ps) std::printf(" %s(%.2f)", p.c_str(), sc);
            std::printf("\n");
            continue;
        }
        if (w[0] == "/imaginar" && w.size() > 1) {
            std::vector<std::string> words(w.begin() + 1, w.end());
            float conf = 0.0f;
            eva2::Image img = e.imagine_image(words, &conf);
            eva2::write_ppm("imaginou.ppm", {img}, 6);
            std::printf("         (imaginou com forca %.2f; imagem em imaginou.ppm)\n", conf);
            continue;
        }
        if (w[0] == "/olhar") {
            std::vector<eva2::Image> row;
            std::printf("         Eva ve:");
            for (auto& [name, img] : e.look()) {
                std::printf(" %s", name.c_str());
                row.push_back(img);
                row.push_back(e.vision().imagine(e.vision().see(img, false)));
            }
            std::printf("\n         (olhar.ppm: cada coisa como ela ve e, ao lado, como ela lembra)\n");
            eva2::write_ppm("olhar.ppm", row, 6);
            continue;
        }
        if (w[0] == "/nomear" && w.size() > 1) {
            auto n = e.name_what_you_see(w[1]);
            std::printf("         Eva ve %s e diz:", w[1].c_str());
            if (n.empty()) std::printf(" (nada)");
            for (auto& x : n) std::printf(" %s", x.c_str());
            std::printf("\n");
            continue;
        }
        if (w[0] == "/ouvir") { grow(e, w.size() > 1 ? std::atoi(w[1].c_str()) : 2000, 8); continue; }
        if (w[0] == "/cena") {
            // uma coisa acontece na frente dela, e você descreve
            auto ev = home_speech().random_event();
            auto words = home_speech().describe(ev);
            std::vector<eva2::Image> shown;
            e.witness(perceive(e, ev, words, &shown), words);
            if (!shown.empty()) eva2::write_ppm("cena.ppm", shown, 4);
            std::printf("         (ela vê: %s %s %s%s%s; você diz: \"", ev.agent.c_str(), ev.action.c_str(), ev.patient.c_str(),
                        ev.place.empty() ? "" : ", em ", ev.place.c_str());
            for (size_t i = 0; i < words.size(); ++i) std::printf("%s%s", i ? " " : "", words[i].c_str());
            std::printf("\")%s%s\n", shown.empty() ? "" : " (o que os olhos viram: cena.ppm)",
                        e.feeling() >= 0.25f ? " (ela está alegre)" : e.feeling() <= -0.25f ? " (ela está triste)" : "");
            continue;
        }
        if (w[0] == "/conta") {
            // ela descreve com as próprias palavras a última cena que viu
            if (e.episodes().size() == 0) { std::printf("         (ela ainda não viu nenhuma cena)\n"); continue; }
            auto said = e.describe(e.episodes().all().back().scene);
            std::printf("         Eva:");
            for (auto& x : said) std::printf(" %s", x.c_str());
            std::printf("\n");
            continue;
        }
        if (w[0] == "/categoria" && w.size() > 1) { show_category(e, w[1]); continue; }
        if (w[0] == "/tema") {
            std::printf("         assunto agora:");
            auto t = e.topic(8);
            if (t.empty()) std::printf(" (nenhum)");
            for (auto& [word, v] : t) std::printf(" %s(%.2f)", word.c_str(), v);
            std::printf("\n");
            continue;
        }
        if (w[0] == "estado") {
            std::printf("        dopamina %.2f | humor %.2f | sentimento %+.2f | vontade de falar %.2f | curiosidade %.2f | estados:",
                        e.dopamine(), e.mood(), e.feeling(), e.drives().talk, e.drives().curiosity);
            auto st = e.active_states();
            if (st.empty()) std::printf(" (nenhum)");
            for (auto& s : st) std::printf(" %s", s.c_str());
            std::printf("\n        palavras %zu (%zu com categoria) | segmentos %d | sinapses %.2f mi (palavras) + %.2f mi (categorias) | turnos %zu | episódios %zu | fatos %zu\n",
                        e.vocab().words.size(), e.categories().words_with_code(), e.segments(),
                        e.cortex().n_synapses() / 1e6, e.category_cortex().n_synapses() / 1e6, e.turns(), e.episodes().size(), e.facts().size());
            continue;
        }
        float reward = 0.0f;
        if (w.back() == "!") { reward = 1.0f; w.pop_back(); }
        if (w.empty()) continue;
        // "?" no fim: a voz sobe (pergunta de sim ou não, se nenhuma palavra disser outra coisa)
        bool asks = false;
        if (w.back() == "?") { asks = true; w.pop_back(); }
        else if (w.back().size() > 1 && w.back().back() == '?') { asks = true; w.back().pop_back(); }
        if (w.empty()) continue;
        if (asks) w.push_back(Eva2::kAsk);
        hear(e, w, reward, false);
    }
    if (e.save()) std::printf("        (memoria salva)\n");
}

int main(int argc, char** argv) {
    bool fresh = false, big = false, run_demo = false, eyes = false;
    int childhood = 0;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--demo") == 0) run_demo = true;
        else if (std::strcmp(argv[i], "--nova") == 0) fresh = true;
        else if (std::strcmp(argv[i], "--grande") == 0) big = true;
        else if (std::strcmp(argv[i], "--olhos") == 0) eyes = true;
        else if (std::strcmp(argv[i], "--infancia") == 0) childhood = (i + 1 < argc) ? std::atoi(argv[++i]) : 20000;
        else { std::printf("uso: eva2 [--demo] [--nova] [--grande] [--infancia N] [--olhos]\n"); return 1; }
    }
    if (run_demo) demo();
    else converse(fresh, big, childhood, eyes);
    return 0;
}
