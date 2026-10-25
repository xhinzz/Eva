#include "population.hpp"

#include <algorithm>
#include <cmath>

namespace eva {

Population::Population(std::string name, int n, NeuronParams p)
    : name_(std::move(name)), n_(n), p_(p),
      v_(n, 0.0f), input_(n, 0.0f), trace_(n, 0.0f), adapt_(n, 0.0f), mod_(n, 0.0f), slow_(n, 0.0f), mod_slow_(n, 0.0f),
      refr_left_(n, 0), spiked_(n, 0) {}

void Population::add_noise(float stddev, std::mt19937& rng) {
    std::normal_distribution<float> d(0.0f, stddev);
    for (int i = 0; i < n_; ++i) input_[i] += d(rng);
}

void Population::inhibit(float amount) {
    for (int i = 0; i < n_; ++i) input_[i] -= amount;
}

void Population::step(float dt) {
    const float leak = dt / p_.tau_m;
    const float decay = std::exp(-dt / p_.trace_tau);
    const float adecay = std::exp(-dt / p_.adapt_tau);
    const float sdecay = std::exp(-dt / slow_tau_);
    const float srate = 3.0f * dt / slow_tau_;  // corrente lenta entregue por ms
    const float mdecay = std::exp(-dt / mod_slow_tau_);
    spike_list_.clear();

    for (int i = 0; i < n_; ++i) {
        trace_[i] *= decay;
        if (trace_[i] < 1e-3f) trace_[i] = 0.0f;  // traço apagado: sinapse não é marcada
        adapt_[i] *= adecay;
        spiked_[i] = 0;

        if (refr_left_[i] > 0) {  // ainda se recuperando do último pulso
            --refr_left_[i];
            v_[i] = p_.reset;
            input_[i] = 0.0f;
            mod_[i] = 0.0f;
            slow_[i] *= sdecay;
            mod_slow_[i] *= mdecay;
            continue;
        }

        // corrente lenta: libera um pouco por ms e decai devagar
        slow_[i] *= sdecay;
        input_[i] += slow_[i] * srate;
        // ganho: a entrada modulatória amplifica só entradas excitatórias
        float in = input_[i];
        mod_slow_[i] *= mdecay;
        if (in > 0.0f && mod_gain_ > 0.0f) in *= 1.0f + mod_gain_ * (mod_[i] + mod_slow_[i]);
        // adaptação: um neurônio que disparou muito fica mais difícil de excitar
        v_[i] += leak * (0.0f - v_[i]) + in - adapt_[i] * dt;
        if (v_[i] < -1.0f) v_[i] = -1.0f;  // evita hiperpolarização absurda
        input_[i] = 0.0f;
        mod_[i] = 0.0f;

        if (v_[i] >= p_.threshold) spike_list_.push_back(i);  // candidato a disparar
    }

    // competição: só os k mais excitados disparam, o resto é inibido
    if (max_active_ > 0 && static_cast<int>(spike_list_.size()) > max_active_) {
        std::partial_sort(spike_list_.begin(), spike_list_.begin() + max_active_, spike_list_.end(),
                          [&](int a, int b) { return v_[a] > v_[b]; });
        for (size_t k = max_active_; k < spike_list_.size(); ++k) v_[spike_list_[k]] = p_.reset;
        spike_list_.resize(max_active_);
    }

    for (int i : spike_list_) {  // disparo!
        v_[i] = p_.reset;
        refr_left_[i] = p_.refractory;
        spiked_[i] = 1;
        trace_[i] += 1.0f;
        adapt_[i] += p_.adapt_inc;
    }
}

void Population::reset_state() {
    std::fill(v_.begin(), v_.end(), 0.0f);
    std::fill(input_.begin(), input_.end(), 0.0f);
    std::fill(trace_.begin(), trace_.end(), 0.0f);
    std::fill(adapt_.begin(), adapt_.end(), 0.0f);
    std::fill(mod_.begin(), mod_.end(), 0.0f);
    std::fill(slow_.begin(), slow_.end(), 0.0f);
    std::fill(mod_slow_.begin(), mod_slow_.end(), 0.0f);
    std::fill(refr_left_.begin(), refr_left_.end(), 0);
    std::fill(spiked_.begin(), spiked_.end(), 0);
    spike_list_.clear();
}

}  // namespace eva
