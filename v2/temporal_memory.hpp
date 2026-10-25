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
    float predicted_decrement = 0.02f;  // previu e não aconteceu: enfraquece um pouco
    int max_segments_per_cell = 64;
    int max_synapses_per_segment = 48;
};

struct Synapse { int32_t cell; float perm; };
struct Segment { int32_t cell; std::vector<Synapse> syn; int active_count = 0; int matching_count = 0; };

class TemporalMemory {
public:
    explicit TemporalMemory(TMConfig cfg = {}, unsigned seed = 7);

    // Um passo: chega uma palavra (suas colunas). `learn` = gravar; `dopamine`
    // escala a força do aprendizado (memórias emocionais ficam mais fortes).
    void step(const std::vector<int>& active_columns, bool learn, float dopamine = 1.0f);
    void reset();  // fim de sequência: o contexto é esquecido

    // Colunas que estão sendo previstas para o PRÓXIMO passo (ordenadas)
    std::vector<int> predicted_columns() const;
    // Fração das colunas de entrada que não eram previstas (0 = tudo previsto)
    float last_surprise() const { return last_surprise_; }
    int n_segments() const { return static_cast<int>(segments_.size()); }

    void save(std::ostream& out) const;
    bool load(std::istream& in);
    const TMConfig& config() const { return cfg_; }

private:
    int cell_index(int column, int i) const { return column * cfg_.cells_per_column + i; }
    int column_of(int cell) const { return cell / cfg_.cells_per_column; }
    void compute_segment_activity();
    void adapt_segment(Segment& s, bool reinforce, float scale);
    void grow_synapses(Segment& s, int n, std::mt19937& rng);
    int least_used_cell(int column, std::mt19937& rng) const;
    int best_matching_segment(int column) const;

    TMConfig cfg_;
    std::mt19937 rng_;
    std::vector<Segment> segments_;
    std::vector<std::vector<int>> cell_segments_;  // por célula, índices dos seus segmentos
    std::vector<int> active_cells_, winner_cells_, prev_active_, prev_winner_;
    std::vector<uint8_t> is_active_, is_prev_active_;
    std::vector<int> active_segments_, matching_segments_;  // do passo anterior (previsão)
    std::vector<uint8_t> predictive_;  // célula prevista para o próximo passo
    float last_surprise_ = 1.0f;
};

}  // namespace eva2
