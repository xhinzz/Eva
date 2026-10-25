// Eva - o cérebro: córtex + hipocampo + dopamina + sono + impulsos
#pragma once
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "drives.hpp"
#include "neuromodulators.hpp"
#include "population.hpp"
#include "projection.hpp"

namespace eva {

// Todos os "botões" da Eva em um só lugar.
struct EvaConfig {
    int max_concepts = 48;     // quantas palavras ela pode aprender
    int group_size = 20;       // neurônios por conceito no córtex
    int hippo_size = 300;      // neurônios no hipocampo      // neurônios no hipocampo
    int hippo_max_active = 3;  // inibição rápida: máx. de disparos por ms no hipocampo
    float hp_trace_tau = 20.0f;
    float hp_adapt_inc = 0.015f, hp_adapt_tau = 150.0f;
    // Cansaço do córtex. A acetilcolina (alta ao ouvir) suprime a corrente de
    // adaptação: ao codificar, a palavra precisa ficar firme para recrutar o CA3;
    // ao lembrar, o cansaço é o que faz a cadeia avançar.
    float cx_adapt_inc = 0.015f;         // ao codificar (ACh alta). 0 aprende todas as transições, mas as palavras não se apagam: medido, pior
    float cx_adapt_inc_retrieval = 0.015f;  // ao lembrar / dormir (ACh baixa)  // cansaço das células do CA3 (passagem de bastão por fadiga)  // janela do STDP no hipocampo (ms): maior = liga palavras não adjacentes
    int cortex_max_active = 16;  // idem no córtex (evita que uma memória forte "trave" tudo)

    // córtex: aprende devagar acordada, mais no sono (consolidação)
    float cx_w_max = 0.25f, cx_max_in = 2.5f, cx_a_minus = 0.6f;  // a_minus: depressão quando a ordem é inversa
    float lr_cx_awake = 0.00001f, lr_cx_sleep = 0.002f;

    // hipocampo: aprende rápido
    float hp_w_max = 0.35f, hp_max_in = 0.8f, hp_a_minus = 0.6f;
    float hpcx_w_max = 0.3f, hpcx_max_in = 2.0f;
    float cxhp_w_init = 0.3f, cxhp_w_max = 0.4f, cxhp_max_in = 6.0f;
    float lr_hp_awake = 0.4f;

    // entrada sensorial e controle de atividade
    float sensory_rate = 0.25f, sensory_weight = 0.35f;
    float noise = 0.02f;
    float cx_inhibition = 0.012f, hp_inhibition = 0.04f;

    // sono
    float sleep_recurrent_gain = 6.0f;  // ACh baixa: conexões internas do hipocampo mais fortes
    float sleep_tonic = 0.045f;         // depolarização no estado UP
    float sleep_noise = 0.12f;
    int sleep_period = 250, sleep_up = 60;  // ondas lentas (ms)

    // divagação acordada (mind-wandering): como o sono, mas mais rara e mais fraca
    float wander_gain = 4.0f, wander_tonic = 0.045f, wander_noise = 0.12f;
    int wander_period = 1000, wander_up = 60;
    int thought_threshold = 12;  // pulsos no córtex para um conceito "entrar no pensamento"

    // sequências (frases): cada palavra é mostrada por sua vez, com sobreposição
    int seq_word_ms = 100, seq_overlap_ms = 20;
    // Passagem de bastão (teta na codificação): na fronteira entre palavras, um
    // pulso de inibição zera a competição no CA3. As células da palavra velha
    // perdem as vagas; as da nova entram enquanto o traço da velha ainda está
    // vivo, e a transição velha->nova é aprendida. 0 = desligado.
    int encode_reset_ms = 0;
    float encode_reset_inhibition = 1.0f;
    int encode_theta_ms = 0;  // alternativa: ritmo livre (período em ms), em vez de alinhado às palavras
    float recite_gain = 6.0f;    // ao recitar, o hipocampo encadeia sozinho (como na divagação)
    float retrieval_cx_gain = 0.3f;  // ACh baixa: entrada córtex->hipocampo enfraquece ao lembrar (1 = igual)
    int retrieval_max_active = 8;    // k vencedores ao lembrar: assembleias maiores encadeiam melhor (0 = igual ao normal)
    float cxhp_slow_fraction = 0.0f;  // parte lenta (NMDA) da entrada córtex->hipocampo (código conjuntivo)
    float cxhp_slow_tau = 100.0f;
    float recite_drive = 1.0f;       // força da pista ao recitar (1 = igual ao sensorial)
    int hp_hp_delay = 1;             // atraso (ms) das sinapses internas do hipocampo
    int hp_cx_delay = 1;             // atraso (ms) hipocampo -> córtex    // k vencedores no hipocampo ao lembrar (0 = o mesmo de sempre)    // ao recitar, o hipocampo encadeia sozinho (como na divagação)
    int recite_max_ms = 900;
    int onset_spikes = 3;        // um conceito "entra" na sequência no seu 3o pulso
    int readout_min_spikes = 6;  // e só conta se, no total, disparou pelo menos isto
    int readout_bin_ms = 5;      // (divagação) janela para ler a ordem de um pensamento
    // ritmo teta ao recitar: a cada ciclo a inibição "zera" o hipocampo e só a
    // próxima palavra da cadeia consegue disparar (uma palavra por ciclo)
    int theta_ms = 0, theta_inhib_ms = 10;  // 0 = desligado
    float theta_inhibition = 0.5f;

    // Giro denteado (DG): camada de separação de padrões entre córtex e CA3.
    // Cada célula do DG recebe a palavra atual (córtex) E o estado anterior do
    // CA3, por pesos fixos e aleatórios; poucas vencem; cada vencedora ativa
    // umas poucas células do CA3 por fibras fortes (musgosas). Assim, "musica"
    // depois de "de" e "musica" depois de "ouve" recrutam células diferentes.
    int dg_size = 0;            // 0 = desligado
    int dg_max_active = 6;
    float dg_cx_w = 0.3f;       // peso médio por sinapse córtex->DG (fixo)
    float dg_ca3_w = 0.5f;      // peso médio por sinapse CA3->DG (estado anterior)
    float dg_ca3_tau = 80.0f;   // quanto tempo o estado anterior ainda pesa (ms)
    int dg_fanout = 3;          // células do CA3 que cada célula do DG ativa
    float dg_mossy_w = 0.6f;    // força das fibras musgosas
    float dg_cx_direct_gain = 0.3f;  // via direta córtex->CA3 fica fraca (o DG manda)
    bool dg_modulatory = true;       // fibras musgosas como GANHO sobre a entrada do córtex (evita o laço fugir)
    float dg_mod_gain = 3.0f;

    // contexto por episódio (células de tempo/lugar do córtex entorrinal):
    // um padrão esparso novo a cada episódio, que o hipocampo liga às palavras.
    // A mesma palavra em dois episódios recruta células diferentes.
    int context_size = 0, context_active = 8;   // 0 = desligado (padrão; veja README). 60 liga
    int context_max_active = 6;
    float ctx_hp_w_init = 0.3f, ctx_hp_w_max = 0.4f, ctx_hp_max_in = 3.0f, ctx_hp_connect = 0.25f;
    float hp_ctx_w_max = 1.0f, hp_ctx_max_in = 6.0f;  // fortes: o hipocampo precisa conseguir reinstalar o contexto
    float ctx_inhibition = 0.05f;
    float ctx_mod_gain = 0.0f;       // >0: contexto age como GANHO multiplicativo no hipocampo (0 = soma)
    float boundary_surprise = 0.5f;  // acima disto, começa um episódio novo
    int context_drift = 0;        // unidades trocadas por episódio (0 = contexto todo novo)
    float ctx_lr_scale = 1.0f;    // taxa de aprendizado ctx->hp em relação ao hipocampo

    // interocepção
    float intero_rate = 0.08f;       // taxa máxima de disparo dos conceitos internos (0.25 destruía as sequências: medido)
    float state_threshold = 0.35f;   // acima disto o estado conta como "ativo"
    float surprise_tau = 2000.0f;    // ms
    float reply_talk = 0.15f;        // vontade mínima para responder quando falam com ela
    bool echo_when_silent = true;    // ecoa o que ouviu se o estado não puxar nada

    // surpresa e dopamina
    float novelty_bonus = 1.0f;  // o novo libera dopamina (como no cérebro)
    float attention_bonus = 0.2f;  // ser ouvida/falada também libera um pouco
    float predict_full_spikes = 20.0f;  // pulsos na palavra seguinte para contar como "previsto"
    int probe_ms = 50;           // duração da pista ao prever
    int recite_cue_ms = 50;      // duração da pista ao recitar
    int recite_max_gap_ms = 60;  // silêncio maior que isto encerra a frase
};

struct RecallResult {
    std::string cue;
    std::vector<int> spikes_per_concept;  // atividade de cada conceito no córtex
};

struct Experience {
    std::vector<std::string> concepts;
    std::vector<std::string> new_words;  // palavras que ela nunca tinha visto
    float surprise = 0.0f;  // 0 = já previa; 1 = totalmente inesperado
    float dopamine = 0.0f;  // nível de dopamina durante a experiência
    std::vector<int> context;  // unidades de contexto ativas no episódio (se houver)
};

// Algo que a Eva disse. Não há moldes de texto: `words` é a cadeia que saiu
// do hipocampo dela, na ordem; `state` são os estados internos ativos na hora.
struct Utterance {
    enum class Kind { Reply, Thought, Question, Echo };
    Kind kind;
    double t_ms;                        // tempo simulado
    std::vector<std::string> words;     // o que ela disse (só palavras aprendidas)
    std::vector<std::string> state;     // interocepção ativa: <feliz>, <curiosa>, <surpresa>
    std::string text() const;           // as palavras juntas ("?" se for pergunta)
};

class Eva {
public:
    explicit Eva(std::vector<std::string> concepts = {}, EvaConfig cfg = {}, unsigned seed = 42);

    // Viver uma experiência: um ou mais conceitos aparecem juntos.
    // `reward` > 0 libera dopamina extra (a experiência foi boa/importante).
    // Palavras desconhecidas são aprendidas na hora (se houver espaço).
    Experience experience(const std::vector<std::string>& concepts, float reward = 0.0f,
                          int duration_ms = 200);
    Experience experience(const std::string& a, const std::string& b, float reward = 0.0f,
                          int duration_ms = 200) {
        return experience(std::vector<std::string>{a, b}, reward, duration_ms);
    }

    // Contar uma frase: as palavras vêm em ordem, uma depois da outra.
    // O STDP é assimétrico no tempo, então ela aprende a ORDEM (A -> B -> C).
    Experience tell(const std::vector<std::string>& words, float reward = 0.0f,
                    std::vector<std::vector<int>>* word_cells = nullptr,  // depuração: células do CA3 por palavra
                    std::vector<std::vector<int>>* raster = nullptr,      // depuração: pulsos do CA3 por ms
                    std::vector<std::vector<int>>* cx_raster = nullptr);  // depuração: pulsos do córtex por ms

    // Marcador de fim de frase: um "conceito" interno que ela vive ao final de
    // cada frase. É o que a deixa saber onde a frase termina (segmentação).
    static constexpr const char* kEnd = "<fim>";
    // Interocepção: estados internos que o córtex "sente" como conceitos.
    // Disparam sozinhos conforme a química dela; assim, o que ela ouve enquanto
    // está feliz fica ligado a <feliz>, e sentir-se feliz de novo puxa aquilo.
    static constexpr const char* kHappy = "<feliz>";
    static constexpr const char* kCurious = "<curiosa>";
    static constexpr const char* kSurprised = "<surpresa>";
    bool is_internal(const std::string& c) const { return !c.empty() && c[0] == '<'; }
    std::vector<std::string> active_states() const;  // quais estados estão acima do limiar

    // Responder ao que acabou de ouvir. A resposta nasce do estado interno
    // (os conceitos interoceptivos ativos servem de pista) e, se nada vier,
    // ela ecoa o que ouviu (como uma criança). Pode voltar vazio = silêncio.
    std::vector<Utterance> respond();

    // Recitar: dá a primeira palavra e deixa a rede encadear sozinha.
    std::vector<std::string> recite(const std::string& cue, int max_words = 6);

    // Viver acordada sem estímulo. Ela divaga, e pode decidir falar.
    std::vector<Utterance> live(int duration_ms);

    // Dormir: o hipocampo repete as memórias e o córtex aprende com elas.
    void sleep(int duration_ms);

    // Lembrar: mostra só uma pista e mede o que acende no córtex.
    // with_hippocampus=false simula uma lesão no hipocampo.
    RecallResult recall(const std::string& cue, bool with_hippocampus, int duration_ms = 150);

    // Silêncio puro (sem divagação), útil para testes
    void rest(int duration_ms);

    const std::vector<std::string>& concepts() const { return concepts_; }
    bool knows(const std::string& name) const { return find_concept(name) >= 0; }
    int concept_index(const std::string& name) const;  // lança se desconhecido
    float cortical_link(const std::string& a, const std::string& b) const;
    float knowledge(const std::string& c) const;  // quanto ela sabe sobre um conceito
    float dopamine() const { return dopa_.level; }
    float mood() const { return dopa_.mood; }
    const Drives& drives() const { return drives_; }
    double clock_ms() const { return clock_ms_; }
    const EvaConfig& config() const { return cfg_; }

    // estatísticas de atividade (pulsos por neurônio por segundo) desde o último reset
    void reset_stats() { cx_spikes_ = hp_spikes_ = 0; stat_ms_ = 0; }
    float cortex_rate() const;
    float hippo_rate() const;

    // gravação de atividade do hipocampo (depuração): contagem por célula
    void start_recording() { rec_hp_.assign(hippo_->size(), 0); rec_ctx_.assign(context_->size(), 0); recording_ = true; }
    std::vector<int> stop_recording() { recording_ = false; return rec_hp_; }
    const std::vector<int>& recorded_context() const { return rec_ctx_; }

    // acesso de leitura para depuração/visualização
    const Projection& cx_cx() const { return *cx_cx_; }
    const Projection& cx_hp() const { return *cx_hp_; }
    const Projection& hp_hp() const { return *hp_hp_; }
    const Projection& hp_cx() const { return *hp_cx_; }
    const Population& cortex() const { return *cortex_; }
    const Population& hippo() const { return *hippo_; }
    const Population& dg() const { return *dg_pop_; }
    const Population& context() const { return *context_; }
    const Projection& ctx_hp() const { return *ctx_hp_; }

private:
    void step(const std::vector<int>& active_concepts, float drive);
    void set_mode(Mode m);
    void retrieval_mode(bool on, float recurrent_gain);  // liga/desliga o modo "interno"
    void cue_gate(bool cue_on);
    void freeze_learning(bool frozen);
    int find_concept(const std::string& name) const;
    int add_concept(const std::string& name);
    float predict_surprise(const std::vector<int>& idx);
    std::vector<int> resolve(const std::vector<std::string>& names, Experience& ex);
    void begin_experience(const std::vector<int>& idx, float reward, Experience& ex);
    void end_experience(Experience& ex);
    std::vector<int> read_sequence(const std::vector<std::vector<int>>& bins, int min_spikes) const;
    std::vector<std::string> chain_from(const std::vector<int>& cues, int max_words);  // recita a partir de pistas
    float state_level(const std::string& name) const;
    std::vector<std::string> last_heard_;
    float surprise_trace_ = 0.0f;  // surpresa recente, decai em ~2 s

    std::vector<std::string> concepts_;
    EvaConfig cfg_;
    float dt_ = 1.0f;  // 1 ms por passo
    std::mt19937 rng_;
    Mode mode_ = Mode::Awake;
    bool frozen_ = false;
    bool wandering_ = false;

    std::unique_ptr<Population> cortex_, hippo_, context_, dg_pop_;
    std::unique_ptr<Projection> cx_cx_, cx_hp_, hp_hp_, hp_cx_, ctx_hp_, hp_ctx_;
    std::unique_ptr<Projection> cx_dg_, ca3_dg_, dg_ca3_;  // giro denteado
    std::vector<int> episode_ctx_, last_ctx_;
    std::vector<int> rec_hp_, rec_ctx_;
    bool recording_ = false;  // unidades de contexto ativas no episódio atual
    void new_episode();             // sorteia um contexto novo
    Dopamine dopa_;
    Drives drives_;

    double clock_ms_ = 0;
    long long sleep_clock_ = 0;   // fase das ondas lentas
    long long wander_clock_ = 0;  // fase da divagação
    std::string last_question_;
    long long cx_spikes_ = 0, hp_spikes_ = 0, stat_ms_ = 0;
};

}  // namespace eva
