// Eva 2 - medida de "falar sobre o assunto".
//
// Ela passa por uma infância (ouve N frases da casa, dormindo a cada 2000).
// Depois você fala com ela: frases novas da mesma casa ("o gato é bonito").
// Para cada uma, ela responde. Contamos:
//   * no assunto: a resposta usa um substantivo da sua frase (o gato -> "o gato está na sala")
//   * gramatical: a resposta inteira casa com a gramática e concorda
//   * eco: a resposta é cópia da sua frase
// O mesmo cérebro (carregado do mesmo arquivo) responde com o tema ligado e desligado.
//
// Uso: topic [frases_da_infancia] [frases_de_conversa]
//   CACHE=arquivo   guarda/carrega o cérebro depois da infância (para repetir rápido)
//   ECO=1           deixa a imitação de frases novas ligada (padrão: desligada,
//                   para medir o que ela produz, não o que repete)
//   GAIN=x          peso do tema
//   V=1             mostra a conversa
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

#include "corpus.hpp"
#include "eva2.hpp"

static std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) s += (i ? " " : "") + v[i];
    return s;
}

struct Result { int n = 0, replied = 0, on_topic = 0, on_topic_not_echo = 0, grammatical = 0, grammatical_on_topic = 0, echo = 0; };

int main(int argc, char** argv) {
    const int childhood = argc > 1 ? atoi(argv[1]) : 10000;
    const int probes = argc > 2 ? atoi(argv[2]) : 200;
    const bool verbose = getenv("V") != nullptr;
    const std::string cache = getenv("CACHE") ? getenv("CACHE") : "";

    auto make_config = [&]() {
        eva2::Eva2Config c;
        c.world_enabled = false;
        c.diary_file = "/dev/null";
        c.memory_file = cache.empty() ? "/dev/null" : cache;
        if (!getenv("ECO")) c.imitate_above = 2.0f;  // nunca imita: mede o que ela produz
        if (getenv("GAIN")) c.topic_gain = atof(getenv("GAIN"));
        return c;
    };
    corpus::Grammar home(7);
    // infância (ou carrega)
    {
        eva2::Eva2 e(make_config(), 1);
        if (cache.empty() || !e.load()) {
            for (int done = 0; done < childhood;) {
                const int today = std::min(2000, childhood - done);
                for (int i = 0; i < today; ++i) e.listen(home.sentence());
                done += today;
                e.sleep(300);
                std::fprintf(stderr, "  infância: %d frases\n", done);
            }
            if (!cache.empty()) e.save();
        }
        if (cache.empty()) { std::fprintf(stderr, "(sem CACHE: a infância é refeita para cada condição)\n"); }
    }
    // frases de conversa: as mesmas para as duas condições, só com sujeito (artigo + substantivo)
    corpus::Grammar talk(99);
    std::vector<std::vector<std::string>> said;
    while (static_cast<int>(said.size()) < probes) {
        auto s = talk.sentence();
        if (s.size() >= 3 && (s[0] == "o" || s[0] == "a" || s[0] == "os" || s[0] == "as")) said.push_back(s);
    }

    Result res[2];
    for (int cond = 0; cond < 2; ++cond) {
        const bool topic_on = cond == 0;
        eva2::Eva2Config c = make_config();
        eva2::Eva2 e(c, 1);
        if (!cache.empty()) e.load();
        else {
            corpus::Grammar again(7);
            for (int done = 0; done < childhood;) {
                const int today = std::min(2000, childhood - done);
                for (int i = 0; i < today; ++i) e.listen(again.sentence());
                done += today;
                e.sleep(300);
            }
        }
        e.set_topic_in_speech(topic_on);
        if (verbose) std::printf("\n=== tema %s ===\n", topic_on ? "LIGADO" : "DESLIGADO");
        Result& r = res[cond];
        for (const auto& s : said) {
            e.idle(2500);  // um pouco de silêncio: a vontade de falar volta
            e.hear(s);
            std::vector<std::string> reply;
            bool echo = false;
            for (const auto& u : e.respond()) { reply = u.words; echo = (u.kind == eva2::Utterance::Kind::Echo) || u.words == s; }
            ++r.n;
            if (reply.empty()) { if (verbose && r.n <= 25) std::printf("Voce: %-34s Eva: (silencio)\n", join(s).c_str()); continue; }
            ++r.replied;
            std::set<std::string> nouns;
            for (const auto& w : s) if (talk.pos_only(w) == "N") nouns.insert(w);
            bool on = false;
            for (const auto& w : reply) if (nouns.count(w)) on = true;
            const auto ck = talk.check(reply);
            r.on_topic += on;
            r.on_topic_not_echo += on && !echo;
            r.grammatical += ck.agreement;
            r.grammatical_on_topic += ck.agreement && on;
            r.echo += echo;
            if (verbose && r.n <= 25)
                std::printf("Voce: %-34s Eva: %-38s %s%s%s\n", join(s).c_str(), join(reply).c_str(), on ? "[no assunto]" : "[outro assunto]",
                            ck.agreement ? " [gramatical]" : (ck.syntax ? " [erro de concordância]" : " [fora da gramática]"), echo ? " [eco]" : "");
        }
    }
    auto pct = [](int a, int b) { return b ? 100.0 * a / b : 0.0; };
    std::printf("\ninfância de %d frases; %d frases suas (novas para ela), imitação %s\n", childhood, probes, getenv("ECO") ? "ligada" : "desligada");
    std::printf("%-18s | %-9s | %-26s | %-11s | %-26s | %s\n", "", "respondeu", "no assunto (seu substantivo)", "gramatical", "no assunto E gramatical", "eco");
    for (int cond = 0; cond < 2; ++cond) {
        const Result& r = res[cond];
        std::printf("%-18s | %8.0f%% | %25.0f%% | %10.0f%% | %25.0f%% | %3.0f%%\n", cond == 0 ? "tema ligado" : "tema desligado",
                    pct(r.replied, r.n), pct(r.on_topic, r.replied), pct(r.grammatical, r.replied), pct(r.grammatical_on_topic, r.replied), pct(r.echo, r.replied));
    }
}
