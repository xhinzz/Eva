// Eva - população de neurônios LIF (integra-e-dispara com vazamento)
#pragma once
#include <random>
#include <string>
#include <vector>

namespace eva {

struct NeuronParams {
    float tau_m = 20.0f;      // constante de tempo da membrana (ms)
    float threshold = 1.0f;   // limiar de disparo (unidades normalizadas)
    float reset = 0.0f;       // potencial após o disparo
    int refractory = 2;       // período refratário (ms)
    float trace_tau = 20.0f;  // traço de atividade usado pelo STDP (ms)
    float adapt_inc = 0.015f;  // "cansaço" acumulado a cada disparo
    float adapt_tau = 150.0f; // tempo para o cansaço passar (ms)
};

// Um grupo de neurônios. Cada neurônio acumula entradas na membrana,
// "vaza" de volta ao repouso e dispara um pulso quando passa do limiar.
class Population {
public:
    Population(std::string name, int n, NeuronParams p = {});

    void step(float dt);  // avança 1 passo de tempo
    void inject(int i, float amount) { input_[i] += amount; }
    // Entrada MODULATÓRIA: não excita por si só; multiplica o efeito das outras
    // entradas (como a inibição/neuromodulação fazem no hipocampo real).
    void inject_mod(int i, float amount) { mod_[i] += amount; }
    // Entrada LENTA (tipo NMDA): a corrente dura ~100 ms em vez de 1 ms. Assim o
    // que acabou de acontecer ainda pesa na escolha de quem dispara agora:
    // a célula codifica a CONJUNÇÃO (palavra anterior + palavra atual).
    void inject_slow(int i, float amount) { slow_[i] += amount; }
    void set_slow_tau(float tau) { slow_tau_ = tau; }
    void set_mod_gain(float g) { mod_gain_ = g; }
    // Modulação lenta: o ganho persiste ~tau ms (quem estava ativo há pouco ainda modula)
    void inject_mod_slow(int i, float amount) { mod_slow_[i] += amount; }
    void set_mod_slow_tau(float tau) { mod_slow_tau_ = tau; }
    void add_noise(float stddev, std::mt19937& rng);
    void inhibit(float amount);  // inibição global (interneurônios)
    void reset_state();
    // Inibição rápida (células em cesto): no máximo k neurônios disparam por ms.
    // 0 = sem limite. Força códigos esparsos, como no hipocampo real.
    void set_max_active(int k) { max_active_ = k; }
    // Acetilcolina reduz a corrente de adaptação: ao codificar, menos cansaço
    void set_adapt_inc(float a) { p_.adapt_inc = a; }

    int size() const { return n_; }
    const std::string& name() const { return name_; }
    const std::vector<int>& spikes() const { return spike_list_; }
    const std::vector<float>& trace() const { return trace_; }
    bool spiked(int i) const { return spiked_[i] != 0; }

private:
    std::string name_;
    int n_;
    NeuronParams p_;
    std::vector<float> v_, input_, trace_, adapt_, mod_, slow_, mod_slow_;
    float mod_slow_tau_ = 80.0f;
    float slow_tau_ = 100.0f;
    float mod_gain_ = 0.0f;
    std::vector<int> refr_left_;
    std::vector<unsigned char> spiked_;
    std::vector<int> spike_list_;
    int max_active_ = 0;
};

}  // namespace eva
