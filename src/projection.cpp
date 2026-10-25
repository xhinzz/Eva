#include "projection.hpp"

#include <algorithm>
#include <cmath>

namespace eva {

Projection::Projection(Population& pre, Population& post, float connect_prob,
                       float w_init_max, PlasticityParams p, std::mt19937& rng)
    : pre_(pre), post_(post), np_(pre.size()), nq_(post.size()), p_(p),
      w_(np_ * nq_, 0.0f), elig_(np_ * nq_, 0.0f), mask_(np_ * nq_, 0),
      in_active_(np_ * nq_, 0), pre_limit_(np_), col_(nq_) {
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    const bool recurrent = (&pre == &post);
    for (int i = 0; i < np_; ++i)
        for (int j = 0; j < nq_; ++j) {
            if (recurrent && i == j) continue;  // sem autoconexão
            if (u(rng) < connect_prob) {
                mask_[i * nq_ + j] = 1;
                w_[i * nq_ + j] = u(rng) * w_init_max;
                col_[j].push_back(i);
            }
        }
}

void Projection::normalize_in(float mean_w) {
    // cada pós fica com peso médio `mean_w` por sinapse (soma = mean_w * nº de entradas)
    for (int j = 0; j < nq_; ++j) {
        float sum = 0.0f;
        for (int i : col_[j]) sum += w_[i * nq_ + j];
        if (sum <= 0.0f) continue;
        const float f = mean_w * static_cast<float>(col_[j].size()) / sum;
        for (int i : col_[j]) w_[i * nq_ + j] *= f;
    }
}

void Projection::wire_fanout(int fanout, float w, std::mt19937& rng) {
    std::uniform_int_distribution<int> pick(0, nq_ - 1);
    for (int i = 0; i < np_; ++i)
        for (int k = 0; k < fanout; ++k) {
            const int j = pick(rng);
            const int idx = i * nq_ + j;
            if (mask_[idx]) continue;
            mask_[idx] = 1;
            w_[idx] = w;
            col_[j].push_back(i);
        }
}

void Projection::set_delay(int d) {
    delay_ = std::max(1, d);
    queue_.assign(delay_, {});
    qpos_ = 0;
}

void Projection::propagate() {
    const std::vector<int>* spikes = &pre_.spikes();
    if (delay_ > 1) {
        if (queue_.size() != static_cast<size_t>(delay_)) queue_.assign(delay_, {});
        // guarda os pulsos de agora para daqui a `delay_` passos e entrega os antigos
        queue_[qpos_] = pre_.spikes();
        qpos_ = (qpos_ + 1) % delay_;
        spikes = &queue_[qpos_];
    }
    if (gain_ == 0.0f) return;
    for (int i : *spikes) {
        const float* wr = &w_[i * nq_];
        const unsigned char* mr = &mask_[i * nq_];
        if (modulatory_) {
            for (int j = 0; j < nq_; ++j)
                if (mr[j]) {
                    if (mod_slow_) post_.inject_mod_slow(j, gain_ * wr[j]);
                    else post_.inject_mod(j, gain_ * wr[j]);
                }
        } else if (slow_frac_ > 0.0f) {
            for (int j = 0; j < nq_; ++j)
                if (mr[j]) {
                    post_.inject(j, gain_ * wr[j] * (1.0f - slow_frac_));
                    post_.inject_slow(j, gain_ * wr[j] * slow_frac_);
                }
        } else {
            for (int j = 0; j < nq_; ++j)
                if (mr[j]) post_.inject(j, gain_ * wr[j]);
        }
    }
}

void Projection::learn(float dopamine, float dt) {
    if (p_.learning_rate <= 0.0f) return;

    const float decay = std::exp(-dt / p_.tau_elig);
    const auto& pre_tr = pre_.trace();
    const auto& post_tr = post_.trace();

    // pré disparou agora: pós disparou antes -> depressão
    for (int i : pre_.spikes())
        for (int j = 0; j < nq_; ++j) {
            const int idx = i * nq_ + j;
            if (!mask_[idx] || post_tr[j] == 0.0f) continue;
            elig_[idx] -= p_.a_minus * post_tr[j];
            touch(idx);
        }

    // pós disparou agora: pré disparou antes -> potenciação
    for (int j : post_.spikes())
        for (int i : col_[j]) {
            if (pre_tr[i] == 0.0f) continue;
            const int idx = i * nq_ + j;
            elig_[idx] += p_.a_plus * pre_tr[i];
            touch(idx);
        }

    // a dopamina transforma a elegibilidade em mudança de peso.
    // Só as sinapses "marcadas" são visitadas: o resto da rede não muda.
    const float k = p_.learning_rate * dopamine * dt;
    size_t keep = 0;
    for (size_t n = 0; n < active_.size(); ++n) {
        const int idx = active_[n];
        float& e = elig_[idx];
        w_[idx] = std::clamp(w_[idx] + k * e, 0.0f, p_.w_max);
        e *= decay;
        if (std::fabs(e) < 1e-4f) { e = 0.0f; in_active_[idx] = 0; }
        else active_[keep++] = idx;
    }
    active_.resize(keep);
    if (++steps_ % 10 == 0) scale();
}

// Escalonamento sináptico: se um neurônio recebe peso demais no total,
// todas as suas entradas encolhem proporcionalmente. As ligações competem
// entre si, e a rede não entra em "epilepsia".
void Projection::scale() {
    if (p_.max_in <= 0.0f) return;
    for (int j = 0; j < nq_; ++j) {
        float sum = 0.0f;
        for (int i : col_[j])
            if (i < pre_limit_) sum += w_[i * nq_ + j];
        if (sum > p_.max_in) {
            const float f = p_.max_in / sum;
            for (int i : col_[j])
                if (i < pre_limit_) w_[i * nq_ + j] *= f;
        }
    }
}

float Projection::mean_weight(int pb, int pe, int qb, int qe) const {
    double s = 0.0;
    int c = 0;
    for (int i = pb; i < pe; ++i)
        for (int j = qb; j < qe; ++j)
            if (mask_[i * nq_ + j]) { s += w_[i * nq_ + j]; ++c; }
    return c ? static_cast<float>(s / c) : 0.0f;
}

}  // namespace eva
