// Eva 2 - um cérebro de colunas corticais que conversa.
//
// O que ela é: um córtex de colunas (memória temporal, aprendizado hebbiano
// local), interocepção (dopamina, humor, curiosidade, surpresa viram palavras
// internas que entram no fluxo), impulsos que decidem quando falar, sono que
// consolida, e memória que persiste em disco entre conversas.
//
// O que ela NÃO tem: modelo de linguagem, moldes de texto, retropropagação.
// Toda palavra que ela diz é a previsão do córtex dela para o próximo passo,
// dado tudo que veio antes na conversa.
#pragma once
#include <random>
#include <string>
#include <vector>

#include "../src/drives.hpp"
#include "../src/neuromodulators.hpp"
#include "sdr.hpp"
#include "temporal_memory.hpp"
#include "world.hpp"
#include "semantics.hpp"
#include "vision.hpp"

namespace eva2 {
using eva::Dopamine;
using eva::Drives;

struct Eva2Config {
    TMConfig tm;
    int sdr_columns = 2048, sdr_active = 40;
    int min_overlap = 16;        // colunas previstas que uma palavra precisa ter para ser "a próxima"
    int max_reply_words = 12;
    int max_actions_per_turn = 3;
    bool world_enabled = true;
    float meaning_threshold = 0.3f;   // força (≈cosseno) mínima para uma palavra "significar" uma percepção
    float meaning_min_evidence = 1.5f;  // soma de dopamina ouvida (≈ 2 vezes, ou 1 apontando) antes de uma palavra guiar ações
    float function_word_freq = 0.4f;  // palavra em mais de 40% das frases não guia a imaginação
    float attention_gain = 3.0f;      // quanto o objeto apontado pesa a mais na ligação
    float unattended_gain = 0.3f;     // e quanto o resto pesa a menos  // soma de dopamina ouvida (≈ 2-3 vezes) antes de uma palavra guiar ações  // força mínima para uma palavra "significar" uma percepção
    float reply_talk = 0.15f;    // vontade mínima para responder
    float self_learning = 0.25f; // quanto ela aprende com a própria fala (escala de dopamina)
    float imitate_above = 0.6f;  // surpresa acima disto: ela imita primeiro (novidade -> imitação)
    float attention_bonus = 0.2f, novelty_bonus = 0.3f;
    float state_threshold = 0.35f;
    float surprise_tau = 2000.0f;
    int turn_pause_ms = 3000;    // silêncio simulado depois de cada fala sua
    int context_turns = 6;       // quantos turnos anteriores o córtex relê antes de ouvir você (contexto longo)
    std::string memory_file = "eva2_memoria.bin";
    std::string diary_file = "eva2_diario.txt";
};

struct Utterance {
    enum class Kind { Reply, Echo, Thought, Question, Point };  // Point: aponta para algo que vê e não sabe nomear
    Kind kind;
    std::vector<std::string> words;
    std::vector<std::string> state;
    float surprise = 0.0f;  // quão pouco ela previa o que acabou de dizer (0 = sabia de cor)
    std::string text() const;
};

struct Heard {
    std::vector<std::string> new_words;
    float surprise = 0.0f;   // fração de colunas não previstas (erro de previsão real)
    float dopamine = 0.0f;
};

// Algo que ela fez no mundo
struct Action {
    std::string token;   // {ir:jardim}
    std::string result;  // "foi para jardim"
    float reward;
};

class Eva2 {
public:
    explicit Eva2(Eva2Config cfg = {}, unsigned seed = 42);

    // Você fala. Ela vive a frase como sequência, com o estado dela por dentro.
    Heard hear(const std::vector<std::string>& words, float reward = 0.0f);
    // Ela responde (ou não). Vem depois de `hear`.
    std::vector<Utterance> respond();
    // Ela age no mundo se o córtex previr uma ação (fome -> ir à cozinha -> pegar pão -> comer).
    std::vector<Action> act();
    // Você muda o mundo dela (como pegar uma criança pela mão): leva-a a um lugar, dá um objeto
    bool take_to(const std::string& place, std::string* result = nullptr);
    bool give(const std::string& object, std::string* result = nullptr);
    // Atenção conjunta: você aponta para algo que ela vê; o que você disser a seguir cola nele
    bool point_at(const std::string& object, std::string* result = nullptr);
    const World& world() const { return world_; }
    const Semantics& meaning() const { return meaning_; }
    // O que ela imagina ao ouvir uma palavra (percepções evocadas)
    std::vector<std::pair<std::string, float>> imagine(const std::string& word) const { return meaning_.perceptions_for(word, cfg_.meaning_threshold, cfg_.meaning_min_evidence); }
    // O que ela diria vendo isto, sem contexto de conversa (teste de sentido).
    // Com `img`, nomeia uma imagem qualquer (inclusive uma que nunca viu).
    std::vector<std::string> name_what_you_see(const std::string& object);
    std::vector<std::string> name_image(const Image& img);
    // Imaginar: a imagem que as palavras evocam (várias palavras = mistura)
    Image imagine_image(const std::vector<std::string>& words, float* confidence = nullptr);
    // O que os olhos dela veem agora e o que ela imagina de cada coisa
    std::vector<std::pair<std::string, Image>> look() { return world_.look(rng_); }
    VisualCortex& vision() { return vision_; }
    // Tempo passa em silêncio: impulsos crescem; ela pode pensar alto.
    std::vector<Utterance> idle(int ms);
    // Dorme: repete as conversas guardadas e consolida.
    void sleep(int episodes);

    // Memória longa: ela lembra entre conversas.
    bool load();
    bool save() const;

    // Completar: dê o começo e ela segue (teste direto do córtex)
    std::vector<std::string> complete(const std::vector<std::string>& start, int max_words = 12);

    const Vocabulary& vocab() const { return vocab_; }
    const Drives& drives() const { return drives_; }
    float dopamine() const { return dopa_.level; }
    float mood() const { return dopa_.mood; }
    std::vector<std::string> active_states() const;
    int segments() const { return tm_.n_segments(); }
    size_t turns() const { return history_.size(); }

    static constexpr const char* kYou = "<voce>";
    static constexpr const char* kMe = "<eva>";
    static constexpr const char* kEnd = "<fim>";
    static constexpr const char* kHappy = "<feliz>";
    static constexpr const char* kCurious = "<curiosa>";
    static constexpr const char* kSurprised = "<surpresa>";
    static constexpr const char* kWorld = "<mundo>";
    static bool is_internal(const std::string& w) { return !w.empty() && (w[0] == '<' || w[0] == '[' || w[0] == '{'); }

private:
    struct Turn { bool mine; std::vector<std::string> tokens; float dopamine; };

    void feed(const std::string& token, bool learn, float dopamine);
    void perceive(bool learn);  // o mundo entra no fluxo: <mundo> [percepções]
    void feed_tokens(const std::vector<std::string>& tokens, bool learn, float dopamine);
    void pass_time(int ms);
    float state_level(const std::string& name) const;
    std::vector<std::string> generate(int max_words, bool learn);
    int predict_next(std::vector<int>* overlaps_out = nullptr);
    void diary(const std::string& who, const std::vector<std::string>& words, const std::string& note);

    Eva2Config cfg_;
    std::mt19937 rng_;
    Vocabulary vocab_;
    TemporalMemory tm_;
    Dopamine dopa_;
    Drives drives_;
    float surprise_trace_ = 0.0f;
    std::vector<std::string> last_heard_;
    World world_;
    Semantics meaning_;  // córtex associativo: percepção <-> palavra
    VisualCortex vision_;  // olhos e córtex visual
    std::vector<std::vector<int>> unit_columns_;  // cada unidade de V2 -> colunas do córtex de sequências
    std::vector<int> visual_sdr(const VisualCode& c) const;
    std::vector<std::pair<std::string, float>> visual_tokens(const VisualCode& c) const;  // [v12] com ativação
    std::vector<std::pair<std::string, VisualCode>> seen_now_;  // o que ela viu no último olhar
    std::vector<std::string> last_perception_;
    std::vector<std::pair<std::string, float>> imagined_;  // o que a última frase evocou (antes de ligar o novo)
    int actions_this_turn_ = 0;
    std::string pending_action_;
    std::string goal_place_;
    std::string attended_;  // objeto apontado (vale para a próxima frase)  // intenção: lugar que ela entendeu que deve ir
    bool allow_actions_ = false;
    float last_heard_surprise_ = 0.0f;
    std::vector<std::vector<std::string>> recent_replies_;  // habituação: não repetir a mesma fala
    std::vector<Turn> history_;
    std::vector<std::string> recent_said_;  // para não repetir a mesma palavra em loop
    double clock_ms_ = 0;
};

}  // namespace eva2
