// Eva - conjunto de sinapses entre duas populações, com STDP modulado por dopamina
#pragma once
#include <random>
#include <vector>

#include "population.hpp"

namespace eva {

struct PlasticityParams {
    float a_plus = 1.0f;    // potenciação: pré dispara antes do pós
    float a_minus = 0.9f;   // depressão: pré dispara depois do pós
    float tau_elig = 100.0f;  // quanto tempo a "marca" de elegibilidade dura (ms)
    float w_max = 0.2f;     // peso máximo de uma sinapse
    float learning_rate = 0.0f;
    float max_in = 0.0f;    // homeostase: soma máxima de pesos que chegam a um neurônio (0 = sem limite)
};

// Sinapses de `pre` para `post`.
// Aprendizado em 3 fatores (como no cérebro):
//   1) STDP marca as sinapses que participaram (traço de elegibilidade);
//   2) a dopamina decide se essa marca vira mudança real de peso;
//   3) a taxa de aprendizado depende da região (hipocampo rápido, córtex lento).
class Projection {
public:
    Projection(Population& pre, Population& post, float connect_prob,
               float w_init_max, PlasticityParams p, std::mt19937& rng);

    void propagate();                         // entrega os pulsos do pré ao pós
    void learn(float dopamine, float dt);     // atualiza pesos
    void scale();                             // homeostase sináptica

    void set_learning_rate(float lr) { p_.learning_rate = lr; }
    float learning_rate() const { return p_.learning_rate; }
    void set_gain(float g) { gain_ = g; }      // 0 = "lesão" da via
    void set_modulatory(bool m, bool slow = false) { modulatory_ = m; mod_slow_ = slow; }  // entrega como ganho, não como corrente
    // Atraso de transmissão (ms). Um pulso do pré só chega ao pós `d` passos depois.
    // É o que dá ritmo a uma cadeia: sem atraso, uma sequência inteira "explode" num só ms.
    void set_delay(int d);
    // Normaliza: cada pós recebe exatamente `total` de peso somado (sem "hubs")
    void normalize_in(float total);
    // Liga cada pré a exatamente `fanout` pós sorteados, com peso `w` (fibras musgosas)
    void wire_fanout(int fanout, float w, std::mt19937& rng);
    // Fração da corrente entregue pelo canal lento (0 = tudo rápido)
    void set_slow_fraction(float f) { slow_frac_ = f; }
    // Só os primeiros `n` neurônios pré estão "em uso" (conceitos já aprendidos).
    // Os outros ainda não existem para a homeostase, como sinapses não formadas.
    void set_pre_limit(int n) { pre_limit_ = n; }
    float gain() const { return gain_; }

    float weight(int i, int j) const { return w_[i * nq_ + j]; }
    bool connected(int i, int j) const { return mask_[i * nq_ + j] != 0; }

    // força média das sinapses entre dois subconjuntos de neurônios
    float mean_weight(int pre_begin, int pre_end, int post_begin, int post_end) const;

private:
    Population& pre_;
    Population& post_;
    int np_, nq_;
    PlasticityParams p_;
    float gain_ = 1.0f;
    bool modulatory_ = false;
    bool mod_slow_ = false;
    float slow_frac_ = 0.0f;
    int steps_ = 0;
    int delay_ = 1;
    std::vector<std::vector<int>> queue_;  // fila circular de pulsos a entregar
    size_t qpos_ = 0;
    std::vector<float> w_, elig_;
    std::vector<unsigned char> mask_, in_active_;
    int pre_limit_;
    std::vector<int> active_;  // sinapses com elegibilidade != 0 (só essas são atualizadas)
    std::vector<std::vector<int>> col_;  // para cada pós, os prés conectados
    void touch(int idx) {
        if (!in_active_[idx]) { in_active_[idx] = 1; active_.push_back(idx); }
    }
};

}  // namespace eva
