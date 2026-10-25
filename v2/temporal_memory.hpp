// Eva 2 - memória temporal: um córtex de colunas que aprende sequências.
//
// Baseado no modelo de Hawkins (HTM). Cada coluna tem várias células. Quando
// uma palavra chega, suas colunas ativam. Se alguma célula da coluna estava
// PREVENDO (porque reconheceu o contexto), só ela dispara: a célula codifica
// "esta palavra, neste contexto". Se nenhuma previa, a coluna inteira dispara
// (surpresa) e uma célula é escolhida para aprender o contexto novo.
// Cada célula tem segmentos dendríticos: conjuntos de sinapses para células
// que estavam ativas no passo anterior. Aprendizado hebbiano local, sem erro
// propagado: sinapses que acertam ficam mais fortes, as que erram enfraquecem.
//
// Escala: o córtex é esparso (≈2% das células ativas por passo), então cada
// passo só toca as sinapses que saem de células ativas (índice pré-sináptico:
// cada célula sabe onde estão as sinapses que saem dela), nunca o córtex
// inteiro. Cada sinapse ocupa 4 bytes (24 bits de célula + 8 de permanência)
// mais 4 bytes no índice. O custo de um passo depende do que está ativo, não
// do tamanho do córtex: é isso que permite bilhões de sinapses numa máquina
// pessoal. Sinapses mortas viram "lápides" (permanência 0) e a vaga é
// reaproveitada, para os endereços do índice não mudarem.
#pragma once
#include <cstdint>
#include <iosfwd>
#include <random>
#include <vector>

namespace eva2 {

struct TMConfig {
    int columns = 2048;
    int cells_per_column = 16;
    int activation_threshold = 10;   // sinapses conectadas ativas para um segmento disparar
    int min_threshold = 6;           // para um segmento "quase" ter acertado (vale aprender nele)
    int max_new_synapses = 24;
    float connected = 0.5f;
    float initial_permanence = 0.51f;  // já conectada: uma exposição basta (como o hipocampo)
    float inc = 0.10f, dec = 0.05f;
    float predicted_decrement = 0.004f;  // previu e não aconteceu: enfraquece um pouco (0,02 apagava lembranças de uma vez só em fala variada; medido)
    int max_segments_per_cell = 64;
    // Depois de um passo de surpresa (colunas explodidas), a célula vencedora
    // seguinte normalmente é a que já casava com aquelas colunas, o que funde
    // contextos diferentes numa célula só. Ligado, a surpresa abre um contexto novo.
    bool fresh_context_after_burst = false;
    int max_synapses_per_segment = 48;  // até 64 (o índice guarda a vaga em 6 bits)
};

// 4 bytes: célula pré-sináptica (até 16,7 milhões de células) + permanência (0..255)
struct Synapse {
    uint32_t v = 0;
    Synapse() = default;
    Synapse(int cell, uint8_t perm) : v((static_cast<uint32_t>(cell) << 8) | perm) {}
    int cell() const { return static_cast<int>(v >> 8); }
    uint8_t perm() const { return static_cast<uint8_t>(v & 0xFF); }
    void set_perm(uint8_t p) { v = (v & ~0xFFu) | p; }
};
static_assert(sizeof(Synapse) == 4, "sinapse deve ter 4 bytes");

struct Segment { int32_t cell; std::vector<Synapse> syn; int active_count = 0; int matching_count = 0; int sure_count = 0; };

class TemporalMemory {
public:
    explicit TemporalMemory(TMConfig cfg = {}, unsigned seed = 7);

    // Um passo: chega uma palavra (suas colunas). `learn` = gravar; `dopamine`
    // escala a força do aprendizado (memórias emocionais ficam mais fortes).
    void step(const std::vector<int>& active_columns, bool learn, float dopamine = 1.0f);
    void reset();  // fim de sequência: o contexto é esquecido

    // Colunas que estão sendo previstas para o PRÓXIMO passo (ordenadas).
    // `sure`: só as previstas por células que dispararam porque eram esperadas
    // (contexto reconhecido), não por colunas que explodiram de surpresa. A
    // previsão "segura" é memória; a outra é a união de tudo que já veio depois.
    std::vector<int> predicted_columns(bool sure = false) const;
    // Quantas células previstas há em cada coluna dada (para desempatar palavras)
    int predicted_cells_in(const std::vector<int>& columns) const;
    // Fração das colunas de entrada que não eram previstas (0 = tudo previsto)
    float last_surprise() const { return last_surprise_; }
    int n_segments() const { return static_cast<int>(segments_.size()); }
    long long n_synapses() const;
    size_t memory_bytes() const;  // estimativa do que o córtex ocupa na memória

    void save(std::ostream& out) const;
    bool load(std::istream& in);
    const TMConfig& config() const { return cfg_; }

private:
    int cell_index(int column, int i) const { return column * cfg_.cells_per_column + i; }
    int column_of(int cell) const { return cell / cfg_.cells_per_column; }
    void compute_segment_activity();
    void adapt_segment(Segment& s, bool reinforce, float scale);
    void grow_synapses(Segment& s, int seg_index, int n, std::mt19937& rng);
    int live_synapses(const Segment& s) const;
    int least_used_cell(int column, std::mt19937& rng) const;
    int best_matching_segment(int column) const;
    uint8_t to_perm(float p);  // arredondamento estocástico: passos menores que 1/255 ainda contam, em média

    TMConfig cfg_;
    std::mt19937 rng_;
    std::uniform_real_distribution<float> unif_{0.0f, 1.0f};
    uint8_t connected_q_ = 128;
    std::vector<Segment> segments_;
    std::vector<std::vector<int>> cell_segments_;  // por célula, índices dos seus segmentos
    std::vector<std::vector<uint32_t>> presyn_;     // por célula: (segmento << 6 | vaga) de cada sinapse que sai dela
    std::vector<int> active_cells_, winner_cells_, prev_active_, prev_winner_;
    std::vector<uint8_t> is_active_, is_prev_active_;
    std::vector<int> active_segments_, matching_segments_;  // do passo anterior (previsão)
    std::vector<int> touched_segments_;   // segmentos com contagem não nula (para zerar barato)
    std::vector<uint32_t> seg_stamp_;
    uint32_t stamp_ = 0;
    std::vector<uint8_t> predictive_;  // célula prevista para o próximo passo
    std::vector<int> predictive_cells_;
    std::vector<uint8_t> sure_predictive_;
    std::vector<int> sure_cells_;
    std::vector<uint8_t> is_burst_;
    std::vector<uint8_t> col_active_;
    float last_surprise_ = 1.0f;
};

}  // namespace eva2
