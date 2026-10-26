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
#include <condition_variable>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../src/drives.hpp"
#include "../src/neuromodulators.hpp"
#include "sdr.hpp"
#include "temporal_memory.hpp"
#include "world.hpp"
#include "semantics.hpp"
#include "vision.hpp"
#include "categories.hpp"
#include "episodes.hpp"

namespace eva2 {
using eva::Dopamine;
using eva::Drives;

struct Eva2Config {
    TMConfig tm;
    int sdr_columns = 2048, sdr_active = 40;
    // Gramática que emerge: palavras ganham um código de categoria (categories.hpp)
    // e um segundo córtex, acima, aprende sequências de categorias.
    bool categories_enabled = true;
    CategoryConfig categories;
    TMConfig category_tm;
    float category_match = 0.6f;   // fração do código de categoria que precisa estar prevista
    float grammar_override = 1.5f; // a gramática passa por cima de um chute da memória se apoia outra espécie tanto mais
    // Tema: do que se está falando. Pesa a escolha da palavra dentro da espécie.
    bool topic_enabled = true;
    float topic_gain = 8.0f;       // quanto o tema pesa na escolha
    float topic_decay = 0.5f;      // a cada frase ouvida, o tema antigo cai para tanto
    float topic_seen = 0.4f;       // o que ela vê entra no tema com este peso (o apontado, 1)
    float topic_self = 0.1f;       // o que ela mesma disse
    float topic_function_freq = 0.12f;  // palavras em mais frases que isto ("o", "é") não são tema
    float topic_intent = 0.3f;     // tema acima disto é "o que ela quer dizer": entra se a gramática deixar
    float topic_sharp = 6.0f;      // competição entre intenções: o assunto mais recente vence com folga (peso = tema^isto)
    float topic_fit = 0.8f;        // ... e se a gramática, segura, prevê tanto do código de categoria dela
    // Episódios, papéis e perguntas (episodes.hpp)
    bool episodes_enabled = true;
    float question_confidence = 0.7f;  // quão certa ela precisa estar de que ouviu uma pergunta
    float recall_chance = 0.8f;        // ao pensar alto, chance de lembrar um episódio em vez de olhar em volta
    float plan_fit = 0.5f;             // o que ela quer dizer entra se a gramática prevê tanto do código dele
    int min_overlap = 16;        // colunas previstas que uma palavra precisa ter para ser "a próxima"
    int max_reply_words = 12;
    int eye_units = 640;               // unidades de V2 nos olhos das cenas
    float label_match = 0.25f;          // a palavra ouvida decide qual é a coisa se ela se parece ao menos tanto com o conceito nomeado
    float concept_match = 0.75f;       // sem palavra: parecida tanto com um conceito, é ele; menos, é coisa nova
    // No sono, juntar conceitos que uma mesma palavra nomeia ao menos tanto. Só pelo
    // nome (0,15, sem olhar a aparência) piorou: juntou coisas diferentes em cascata.
    float merge_specificity = 0.3f;
    float merge_looks = 0.55f;   // ... e que se parecem ao menos tanto (com o nome sozinho, piorou: juntava coisas diferentes)
    // Emoção por memória: o que ela sente (-1 triste .. +1 feliz) segue o que vê
    // (contágio), fica guardado em cada lembrança, e lembrar traz de volta
    float contagion = 0.6f;            // quanto a cara de quem chora/ri passa para ela
    float reinstate = 0.5f;            // quanto lembrar um episódio traz de volta o que ela sentiu
    float feeling_tau = 60000.0f;      // ms para o sentimento passar
    float feeling_threshold = 0.25f;   // acima disto (em módulo) ela está feliz ou triste
    // Frases que terminam: depois do plano, no máximo tantas palavras sem conteúdo antes do fim
    int plan_tail = 2;
    float plan_end_fit = 0.2f;         // o fim entra assim que a gramática o aceita um pouco
    int max_actions_per_turn = 3;
    bool world_enabled = true;
    float meaning_threshold = 0.3f;   // força (≈cosseno) mínima para uma palavra "significar" uma percepção
    float meaning_min_evidence = 1.5f;
    float fast_mapping_evidence = 0.5f;  // nas cenas: uma exposição já basta para um nome novo (mapeamento rápido)  // soma de dopamina ouvida (≈ 2 vezes, ou 1 apontando) antes de uma palavra guiar ações
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
    // Tamanho grande, para uma máquina como RTX 5060 / Ryzen 7 9800X3D com 16 GB:
    // 16.384 colunas de palavras × 32 células e 4.096 colunas de categorias × 32.
    // Cabe ~1 bilhão de sinapses (8 bytes cada, contando o índice). O custo de
    // cada passo cresce com as células por coluna, não com o número de colunas.
    static Eva2Config big() {
        Eva2Config c;
        c.sdr_columns = 16384;
        c.tm.cells_per_column = 32;
        c.tm.max_segments_per_cell = 255;
        c.categories.columns = 4096;
        c.categories.active = 60;
        c.category_tm.cells_per_column = 32;
        c.category_tm.max_segments_per_cell = 255;
        c.memory_file = "eva2_memoria_grande.bin";
        c.diary_file = "eva2_diario_grande.txt";
        return c;
    }
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
    ~Eva2();
    Eva2(const Eva2&) = delete;
    Eva2& operator=(const Eva2&) = delete;

    // Você fala. Ela vive a frase como sequência, com o estado dela por dentro.
    Heard hear(const std::vector<std::string>& words, float reward = 0.0f);
    // Ela vê uma cena acontecer e ouve alguém descrevê-la ("o gato come o peixe").
    // Guarda o episódio, liga palavras ao que viu e aprende em que posição da
    // frase vem quem faz, a ação, o alvo, o lugar.
    void witness(const Scene& scene, const std::vector<std::string>& words, float dopamine = 0.6f);
    // Alguém pergunta sobre a cena e responde ("cadê o gato?" "o gato está na sala").
    // Ela aprende o que cada palavra de pergunta pede.
    // past = true: a conversa é sobre algo que já passou (a lembrança volta; "o que o gato comeu?")
    void witness_dialogue(const Scene& scene, const std::vector<std::string>& question, const std::vector<std::string>& answer, bool past = false);
    // Alguém conta depois o que aconteceu ("o gato comeu o peixe"), e a lembrança volta
    void recount(const Scene& scene, const std::vector<std::string>& words);
    // Ela descreve uma cena com as próprias palavras, na ordem que aprendeu
    // (use_order = false: as partes em ordem sorteada, para comparar)
    // (past: contando depois, como lembrança)
    std::vector<std::string> describe(const Scene& scene, bool use_order = true, bool past = false);
    // Compreensão: das cenas oferecidas, qual a frase descreve? (índice; -1 = não sabe)
    int understand(const std::vector<std::string>& words, const std::vector<Scene>& options);
    // Resposta a uma pergunta pela memória (vazio se não for pergunta ou se ela não lembra)
    std::vector<std::string> answer(const std::vector<std::string>& question);
    // ---- sim, não, não sei (o comparador: o que ouviu confere com o que lembra?)
    enum Judgment { kSame = 0, kDiff = 1, kUnknown = 2, kNoClaim = 3 };
    static const char* judgment_percept(int j) { static const char* p[] = {"[igual]", "[diferente]", "[nao_sei]", ""}; return p[j]; }
    // a frase vira uma cena (quem, ação, alvo, lugar), pela ordem aprendida
    Scene parse_claim(const std::vector<std::string>& words) const;
    // compara a cena dita com a memória (ou com o que ela sente agora, se a frase fala dela)
    int compare_claim(const std::vector<std::string>& words, Scene* truth = nullptr, const std::string& speaker = "voce", const std::string& listener = "eva") const;
    // alguém pergunta se algo é verdade e responde ("o gato comeu o peixe?" "não, o gato comeu o pão")
    void witness_check(const Scene& scene, const std::vector<std::string>& question, const std::vector<std::string>& answer, bool past = false);
    // alguém diz o que não é, vendo a cena ("o gato não está na sala")
    void witness_negation(const Scene& scene, const std::vector<std::string>& negated);
    // um livro: ela vê a figura e ouve o nome ("olha a girafa"); não é algo que aconteceu
    void look_at_book(const std::string& thing, const std::vector<std::string>& words);
    // alguém pergunta algo que ninguém sabe e a resposta é "não sei" (ela também não sabe)
    void witness_unknown(const std::vector<std::string>& question, const std::vector<std::string>& answer);
    void set_comparator(bool on) { comparator_on_ = on; }
    // ---- fatos ouvidos, nomes, meu e seu
    // alguém conta algo que ela não viu ("o papai está no mercado"): vira lembrança (contada)
    bool told(const std::vector<std::string>& words, const std::string& speaker = "voce", const std::string& listener = "eva");
    // alguém fala o nome de uma coisa de alguém ("o meu gato chama mimi"), e ela vê a coisa
    void witness_naming(const std::string& speaker, const std::string& listener, const std::string& owner, const std::string& thing,
                        const std::vector<std::string>& words, bool her_turn = false);
    // pergunta e resposta sobre um nome (entre os pais, ou com ela, a resposta no turno dela)
    void witness_name_dialogue(const std::string& asker, const std::string& answerer, const std::string& owner, const std::string& thing,
                               const std::vector<std::string>& question, const std::vector<std::string>& answer);
    const FactMemory& facts() const { return facts_; }
    void set_store_heard(bool on) { store_heard_ = on; }
    void set_perspective(bool on) { perspective_ = on; }
    // ---- sentimentos
    float feeling() const { return feeling_; }
    void set_feeling(float f) { feeling_ = f; }
    void set_valence_in_memory(bool on) { valence_in_memory_ = on; }
    void set_interoception_in_speech(bool on) { interoception_in_speech_ = on; }
    // alguém diz como alguém se sente ("você está feliz", "eu estou triste"), vendo a cara
    void witness_feeling(const std::vector<std::string>& words, bool about_listener, bool happy, bool her_turn = false);
    // pergunta sobre o sentimento dela e a resposta por ela (andaime)
    void witness_feeling_dialogue(const std::vector<std::string>& question, const std::vector<std::string>& answer);
    // frases que terminam: liga/desliga o fechamento pelo plano (para medir)
    void set_plan_closure(bool on) { plan_closure_ = on; }
    const EpisodicMemory& episodes() const { return episodes_; }
    const RoleOrder& role_order() const { return roles_; }
    const QuestionWords& question_words() const { return qwords_; }
    void set_memory_in_answers(bool on) { memory_in_answers_ = on; }
    const Scene& last_recalled() const { return last_recalled_; }  // o último episódio que ela contou sozinha
    void set_role_order_in_speech(bool on) { role_order_in_speech_ = on; }
    void set_role_order_in_understanding(bool on) { role_order_in_understanding_ = on; }
    void set_number_in_speech(bool on) { number_in_speech_ = on; }
    void set_tense_in_speech(bool on) { tense_in_speech_ = on; }
    // Olhos para as cenas: um segundo córtex visual (maior) que amadurece vendo as
    // coisas da casa, sem nomes. Depois, cada coisa vista vira um conceito formado
    // pelos olhos ("c17"), e é ele que entra na cena, no episódio e no sentido das palavras.
    void open_eyes(const std::vector<Image>& samples);
    bool eyes_open() const { return eyes_open_; }
    // `words`: o que ela ouve enquanto olha. Uma palavra que já nomeia um conceito
    // diz qual é a coisa (a palavra ajuda a separar coisas parecidas: "isso é um
    // rato, não uma lontra"); sem palavras, só os olhos decidem.
    std::string see_thing(const Image& img, bool learn = true, const std::vector<std::string>& words = {});
    // vários olhares na mesma coisa durante a cena (os olhos se mexem): uma descrição só, mais firme
    std::string see_thing(const std::vector<Image>& glances, bool learn = true, const std::vector<std::string>& words = {});
    std::string see_place(const Image& img, bool learn = true, const std::vector<std::string>& words = {});
    std::string see_place(const std::vector<Image>& glances, bool learn = true, const std::vector<std::string>& words = {});
    size_t thing_concepts() const { return things_.size(); }
    void set_concept_threshold(float t) { things_.threshold = t; places_.threshold = t; }
    size_t place_concepts() const { return places_.size(); }
    VisualCortex& eyes() { return eyes_; }

    // o sentido que uma palavra tem para ela ("[ve:gato]", "[acao:come]", "[lugar:sala]"; vazio se nenhum)
    std::string meaning_of(const std::string& word) const;

    // Ela ouve sem ser com ela (a conversa da casa, alguém falando perto): grava
    // a frase e o contexto das palavras, sem responder nem reler a conversa.
    // É a maior parte do que uma criança ouve.
    // Devolve a surpresa média (fração de colunas não previstas) das palavras.
    float listen(const std::vector<std::string>& words, float dopamine = 0.5f);
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
    const WordCategories& categories() const { return cats_; }
    const TemporalMemory& cortex() const { return tm_; }
    const TemporalMemory& category_cortex() const { return ctm_; }
    // estatística: de onde veio cada palavra gerada (memória de palavras x gramática)
    struct GenStats { long long memory = 0, grammar = 0, vetoed = 0, topic = 0; };
    const GenStats& gen_stats() const { return gen_stats_; }
    // para medir: liga/desliga o uso da gramática na fala (o aprendizado continua)
    void set_grammar_in_speech(bool on) { grammar_in_speech_ = on; }
    void set_grammar_checks_memory(bool on) { grammar_checks_memory_ = on; }
    void set_topic_in_speech(bool on) { topic_in_speech_ = on; }
    // o tema agora: as palavras mais ativas, com o peso
    std::vector<std::pair<std::string, float>> topic(size_t n = 6) const;
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
    static constexpr const char* kSad = "<triste>";
    static constexpr const char* kAsk = "<?>";  // a voz subindo no fim (pergunta de sim ou não)
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
    TemporalMemory ctm_;  // córtex de categorias: sequências de espécies de palavras
    GenStats gen_stats_;
    bool grammar_in_speech_ = true;
    bool grammar_checks_memory_ = false;  // testado: piora (veja README)
    bool topic_in_speech_ = true;
    // Tema ativo: atividade que decai sobre as palavras (o que ouviu, viu, disse).
    // E o caminho até elas: quanto cada palavra costuma vir logo antes de outra
    // (para querer dizer "gato" pré-ativar "o", não "a").
    std::vector<float> topic_;
    std::vector<std::unordered_map<int, float>> next_counts_;
    std::vector<float> next_total_, prev_total_;
    bool topic_said_ = false;  // nesta fala, ela já disse a palavra do assunto?
    void add_topic(const std::string& w, float weight);
    void decay_topic(float factor);
    float topical(int k) const;
    std::pair<std::string, float> visual_name(const VisualCode& c) const;
    // episódios, papéis, perguntas, e o plano de fala ordenado
    EpisodicMemory episodes_;
    RoleOrder roles_;
    QuestionWords qwords_;
    bool memory_in_answers_ = true, role_order_in_speech_ = true, role_order_in_understanding_ = true;
    bool number_in_speech_ = true, tense_in_speech_ = true;
    bool comparator_on_ = true, store_heard_ = true, perspective_ = true, valence_in_memory_ = true, interoception_in_speech_ = true, plan_closure_ = true;
    FactMemory facts_;
    std::vector<std::vector<int>> told_plan_;  // o que ela acabou de guardar do que você contou (para repetir do lado dela)
    float feeling_ = 0.0f;
    void feel(float delta);
    // plano de fala: o sentido de cada palavra (para o plano mandar no conteúdo)
    std::vector<std::string> plan_meaning_, item_meaning_;
    int plan_extra_ = 0;
    void begin_plan(const std::vector<std::vector<int>>& plan);
    std::vector<int> judgment_words(const std::string& percept) const;
    std::string judgment_of(const std::string& w) const;  // "não" + "sei", em ordem
    std::vector<std::vector<int>> judgment_plan(const std::string& percept) const;
    std::vector<std::vector<int>> name_plan(const std::string& owner_percept, const std::string& thing, const std::string& name) const;
    std::vector<std::vector<int>> feeling_plan(bool happy) const;
    std::vector<int> forms_in(const std::vector<int>& forms, const std::string& context) const;
    std::vector<int> agreeing(const std::vector<int>& forms, const std::vector<int>& with) const;
    // de quem é a coisa, pela palavra (meu/seu) e por quem fala: devolve a pessoa
    std::string owner_from(const std::vector<std::string>& words, const std::string& speaker, const std::string& listener) const;
    std::string name_in(const std::vector<std::string>& words) const;  // a palavra nova (o nome)
    // das formas de um sentido ("gato", "gatos"; "come", "comem", "comeu"...), as que
    // combinam com quantos são e com quando foi
    std::vector<int> choose_forms(const std::vector<int>& forms, bool many, bool past) const;
    void bind_asked(const std::vector<std::string>& question, int kind);  // palavras (e pares) da pergunta -> o que ela pede
    std::pair<int, double> asked_by(const std::vector<std::string>& question) const;
    bool question_word(const std::string& w) const;
    std::string meaning_raw(const std::string& word) const;  // o sentido, sem descontar as palavras de pergunta
    bool sounds_past(const std::vector<std::string>& words) const;
    std::vector<std::unordered_map<int, float>> prev_counts_;  // quem vem antes de cada palavra
    std::vector<std::vector<int>> plan_;  // o que ela quer dizer, em ordem (cada item: as formas de um sentido)
    size_t plan_pos_ = 0;
    Scene last_recalled_;
    bool plan_active_ = false;
    std::vector<std::pair<int, std::string>> content_slots(const std::vector<std::string>& words) const;
    std::vector<int> forms_of(const std::string& meaning) const;
    std::vector<std::vector<int>> plan_for(const Scene& s, const std::vector<int>& roles, bool use_order, bool past = false);
    std::vector<std::string> speak_plan(const std::vector<std::vector<int>>& plan, bool learn);
    std::vector<std::vector<int>> answer_plan(const std::vector<std::string>& question);
    void listen_turns(const std::vector<std::vector<std::string>>& turns, float dopamine);
    void rebuild_prev_counts();
    void step_token(int id, bool learn, float dopamine);
    void reset_cortex();
    // As duas áreas trabalham ao mesmo tempo, como no cérebro: o córtex de
    // categorias roda numa segunda linha de execução enquanto o de palavras
    // roda nesta. (Numa máquina com mais núcleos, cada área poderia ter os seus.)
    std::thread area2_;
    std::mutex area2_m_;
    std::condition_variable area2_cv_;
    bool area2_has_job_ = false, area2_stop_ = false;
    std::vector<int> area2_cols_;
    bool area2_learn_ = false;
    float area2_dop_ = 1.0f;
    void area2_loop();
    Dopamine dopa_;
    Drives drives_;
    float surprise_trace_ = 0.0f;
    std::vector<std::string> last_heard_;
    World world_;
    Semantics meaning_;  // córtex associativo: percepção <-> palavra
    WordCategories cats_;  // categorias que emergem do uso
    void observe_categories(const std::vector<std::string>& words);
    void apply_category(int id);
    VisualCortex vision_;  // olhos e córtex visual
    VisualCortex eyes_;    // olhos para as cenas (mais unidades: muitas coisas)
    ConceptMemory things_, places_;
    std::string see_with(ConceptMemory& mem, const char* prefix, const char* meaning_prefix, const std::vector<Image>& glances, bool learn, const std::vector<std::string>& words);
    // no sono: conceitos que têm o mesmo nome eram a mesma coisa vista de jeitos diferentes; viram um só
    int consolidate_concepts(ConceptMemory& mem, const char* prefix, const char* meaning_prefix);
    bool eyes_open_ = false;
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
