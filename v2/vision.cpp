#include "vision.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <unordered_map>
#include <istream>
#include <ostream>

namespace eva2 {

static constexpr int kZones = 9;  // setores da caixa da coisa na descrição invariante
static constexpr int kV4 = 64 + 9 + 4;  // V4: cores da coisa (4x4x4), onde ela ocupa a caixa (3x3), proporções
static constexpr float kV4Weight = 3.0f;  // peso da cor e da forma (medido: v2/tests e logs/olhos)

// ---------------------------------------------------------------- utilidades

static void normalize(std::vector<float>& v) {
    double s = 0;
    for (float x : v) s += x * x;
    const float n = static_cast<float>(std::sqrt(s));
    if (n > 1e-6f) for (float& x : v) x /= n;
}

static float dot(const std::vector<float>& a, const std::vector<float>& b) {
    float s = 0;
    for (size_t i = 0; i < a.size(); ++i) s += a[i] * b[i];
    return s;
}

VisualCortex::VisualCortex(VisionConfig cfg, unsigned seed) : cfg_(cfg), rng_(seed) {
    std::normal_distribution<float> g(0.0f, 1.0f);
    v1_.assign(cfg_.v1_units, std::vector<float>(kPatchDim));
    for (auto& u : v1_) { for (float& x : u) x = std::fabs(g(rng_)); normalize(u); }
    v2_.assign(cfg_.v2_units, std::vector<float>(v2_in_dim()));
    for (auto& u : v2_) { for (float& x : u) x = std::fabs(g(rng_)); normalize(u); }
    v2_gen_.assign(cfg_.v2_units, std::vector<float>(kImg * kImg * 3, 0.5f));
    in_mean_.assign(v2_in_dim(), 0.0f);
    v1_use_.assign(cfg_.v1_units, 0.0f);
    v2_use_.assign(cfg_.v2_units, 0.0f);
    v2_duty_.assign(cfg_.v2_units, 0.0f);
    duty_target_ = static_cast<float>(cfg_.v2_k) / cfg_.v2_units;
}

void VisualCortex::patch_of(const Image& img, int gx, int gy, std::vector<float>& out) const {
    out.resize(kPatchDim);
    int i = 0;
    for (int y = 0; y < kPatch; ++y)
        for (int x = 0; x < kPatch; ++x)
            for (int c = 0; c < 3; ++c) out[i++] = img.at(gx * kStride + x, gy * kStride + y, c);
}

void VisualCortex::add_patch(Image& img, int gx, int gy, const std::vector<float>& p) const {
    int i = 0;
    for (int y = 0; y < kPatch; ++y)
        for (int x = 0; x < kPatch; ++x)
            for (int c = 0; c < 3; ++c) img.at(gx * kStride + x, gy * kStride + y, c) += p[i++];
}

void VisualCortex::prime(const std::vector<Image>& samples) {
    if (samples.empty()) return;
    std::uniform_int_distribution<size_t> pick(0, samples.size() - 1);
    std::uniform_int_distribution<int> g(0, kGrid - 1);
    std::vector<float> p;
    for (auto& u : v1_) {
        patch_of(samples[pick(rng_)], g(rng_), g(rng_), p);
        // tira o fundo médio para o traço descrever a variação, e um pouco de ruído para diferenciar
        std::normal_distribution<float> n(0.0f, 0.02f);
        for (size_t i = 0; i < p.size(); ++i) p[i] = std::max(0.0f, p[i] + n(rng_));
        u = p;
        normalize(u);
    }
    // V1 amadurece primeiro (algumas passadas só nele), para que os códigos que V2
    // vai aprender não mudem debaixo dele. Depois V2 nasce dos códigos maduros.
    for (int pass = 0; pass < 20; ++pass)
        for (const auto& img : samples) see(img, true);
    for (auto& u : v2_) std::fill(u.begin(), u.end(), 0.0f);  // V2 ainda não aprendeu nada de verdade
    std::fill(v2_use_.begin(), v2_use_.end(), 0.0f);
    // fecha o período crítico de V1: medido, se V1 continua mudando depois que V2
    // se formou, V2 fica "lendo" um alfabeto que não existe mais (85% -> 8% de acerto)
    v1_frozen_ = true;

    // média dos traços (para a adaptação) a partir das amostras
    std::fill(in_mean_.begin(), in_mean_.end(), 0.0f);
    for (const auto& img : samples) { auto v = pooled(see(img, false)); for (size_t i = 0; i < v.size(); ++i) in_mean_[i] += v[i] / samples.size(); }
    // V2: cada unidade nasce como o código agrupado (adaptado) de uma imagem vista,
    // e sua sinapse de descida nasce como a própria imagem
    prime_origin.assign(v2_.size(), -1);
    for (size_t k = 0; k < v2_.size(); ++k) {
        const size_t si = pick(rng_);
        prime_origin[k] = static_cast<int>(si);
        const Image& img = samples[si];
        VisualCode c = see(img, false);
        std::vector<float> vec = pooled(c);
        for (size_t i = 0; i < vec.size(); ++i) vec[i] = vec[i] / (0.05f + cfg_.adapt * in_mean_[i]);
        std::normal_distribution<float> n(0.0f, 0.01f);
        for (float& x : vec) x = std::max(0.0f, x + n(rng_));
        v2_[k] = vec;
        normalize(v2_[k]);
        for (size_t i = 0; i < v2_gen_[k].size(); ++i) v2_gen_[k][i] = img.px[i];
    }
    // estatística das respostas densas nas coisas da infância dos olhos
    const int D = (1 + kZones) * cfg_.v1_units + kV4;
    dense_mu_.assign(D, 0.0f); dense_sd_.assign(D, 0.0f);
    for (const auto& img : samples) { auto d = dense_raw(img); for (int u = 0; u < D; ++u) { dense_mu_[u] += d[u]; dense_sd_[u] += d[u] * d[u]; } }
    for (int u = 0; u < D; ++u) {
        dense_mu_[u] /= samples.size();
        dense_sd_[u] = std::sqrt(std::max(1e-6f, dense_sd_[u] / samples.size() - dense_mu_[u] * dense_mu_[u]));
    }
}

// Traços agrupados por quadrante: "há um traço de borda azul em cima à esquerda",
// sem dizer em qual pedaço exato. É o que dá invariância a pequenos deslocamentos
// (como as células complexas de Hubel e Wiesel).
std::vector<float> VisualCortex::pooled(const VisualCode& c) const {
    std::vector<float> vec(v2_in_dim(), 0.0f);
    int idx = 0;
    for (int gy = 0; gy < kGrid; ++gy)
        for (int gx = 0; gx < kGrid; ++gx, ++idx) {
            const int q = (gy >= kGrid / 2 ? 2 : 0) + (gx >= kGrid / 2 ? 1 : 0);
            // presença (não energia): um traço de objeto vale tanto quanto um traço de fundo
            for (auto& [unit, coef] : c.v1_codes[idx]) if (coef > 0.05f) vec[q * cfg_.v1_units + unit] = 1.0f;
        }
    return vec;
}

std::vector<float> VisualCortex::invariant_dense(const Image& img) const {
    std::vector<float> v = dense_raw(img);
    if (dense_mu_.size() == v.size())
        for (size_t u = 0; u < v.size(); ++u) v[u] = (v[u] - dense_mu_[u]) / dense_sd_[u];
    for (size_t u = v.size() - kV4; u < v.size(); ++u) v[u] *= kV4Weight;
    return v;
}

// vários olhares na mesma coisa (os olhos se mexem, a coisa se mexe): a média das descrições
std::vector<float> VisualCortex::invariant_glances(const std::vector<Image>& imgs) const {
    std::vector<float> acc;
    for (const auto& img : imgs) {
        auto v = invariant_dense(img);
        if (acc.empty()) acc = v; else for (size_t i = 0; i < v.size(); ++i) acc[i] += v[i];
    }
    if (!imgs.empty()) for (float& x : acc) x /= imgs.size();
    return acc;
}

// Descrição invariante a posição e a tamanho: para cada traço de V1, a resposta
// máxima dele na imagem inteira e em cada um de 3x3 setores da caixa que contém
// a coisa (o que difere do fundo). O máximo dá invariância a deslocamento; os
// setores da caixa guardam a forma (o canto vazio do losango, a cabeça em cima,
// a base embaixo) sem depender de onde a coisa está nem do tamanho dela.
std::vector<float> VisualCortex::dense_raw(const Image& img) const {
    const int U = cfg_.v1_units;
    std::vector<float> v((1 + kZones) * U, 0.0f), p(kPatchDim);
    int x0b = kImg, y0b = kImg, x1b = -1, y1b = -1;
    const float r0 = img.at(0, 0, 0), g0 = img.at(0, 0, 1), b0 = img.at(0, 0, 2);
    for (int y = 0; y < kImg; ++y)
        for (int x = 0; x < kImg; ++x) {
            const float d = std::fabs(img.at(x, y, 0) - r0) + std::fabs(img.at(x, y, 1) - g0) + std::fabs(img.at(x, y, 2) - b0);
            if (d > 0.15f) { x0b = std::min(x0b, x); y0b = std::min(y0b, y); x1b = std::max(x1b, x); y1b = std::max(y1b, y); }
        }
    if (x1b < 0) { x0b = y0b = 0; x1b = y1b = kImg - 1; }
    const float w = static_cast<float>(x1b - x0b + 1), h = static_cast<float>(y1b - y0b + 1);
    for (int y0 = 0; y0 + kPatch <= kImg; ++y0)
        for (int x0 = 0; x0 + kPatch <= kImg; ++x0) {
            int i = 0;
            for (int y = 0; y < kPatch; ++y)
                for (int x = 0; x < kPatch; ++x)
                    for (int c = 0; c < 3; ++c) p[i++] = img.at(x0 + x, y0 + y, c);
            float n = 0.0f; for (float x : p) n += x * x;
            n = std::sqrt(n) + 1e-6f;
            const float px = x0 + kPatch / 2.0f, py = y0 + kPatch / 2.0f;
            const int zx = std::clamp(static_cast<int>(3.0f * (px - x0b) / w), 0, 2);
            const int zy = std::clamp(static_cast<int>(3.0f * (py - y0b) / h), 0, 2);
            const int q = 1 + zy * 3 + zx;
            for (int u = 0; u < U; ++u) {
                float d = 0.0f;
                const auto& wv = v1_[u];
                for (int k = 0; k < kPatchDim; ++k) d += wv[k] * p[k];
                d /= n;
                v[u] = std::max(v[u], d);
                v[q * U + u] = std::max(v[q * U + u], d);
            }
        }
    // V4: de que cores é a coisa (o que difere do fundo), onde ela ocupa a caixa dela,
    // e as proporções (larga, alta, cheia). Nada disso depende de onde ela está.
    std::vector<float> hist(64, 0.0f), occ(kZones, 0.0f);
    int n = 0;
    for (int y = 0; y < kImg; ++y)
        for (int x = 0; x < kImg; ++x) {
            const float d = std::fabs(img.at(x, y, 0) - r0) + std::fabs(img.at(x, y, 1) - g0) + std::fabs(img.at(x, y, 2) - b0);
            if (d <= 0.15f) continue;
            ++n;
            auto bin = [&](int c) { return std::min(3, static_cast<int>(img.at(x, y, c) * 4.0f)); };
            hist[bin(0) * 16 + bin(1) * 4 + bin(2)] += 1.0f;
            const int zx = std::clamp(static_cast<int>(3.0f * (x - x0b) / w), 0, 2), zy = std::clamp(static_cast<int>(3.0f * (y - y0b) / h), 0, 2);
            occ[zy * 3 + zx] += 1.0f;
        }
    for (float x : hist) v.push_back(n ? std::sqrt(x / n) : 0.0f);
    for (float x : occ) v.push_back(n ? x / n : 0.0f);
    v.push_back(w / h); v.push_back(n / (w * h)); v.push_back(w / kImg); v.push_back(h / kImg);
    return v;
}

std::vector<float> VisualCortex::invariant(const VisualCode& c) const {
    std::vector<float> v(cfg_.v1_units, 0.0f);
    for (const auto& patch : c.v1_codes)
        for (const auto& [unit, coef] : patch) if (coef > 0.05f) v[unit] += 1.0f;
    float norm = 0.0f;
    for (int u = 0; u < cfg_.v1_units; ++u) {
        float common = 0.0f;
        for (int q = 0; q < 4; ++q) common += in_mean_[q * cfg_.v1_units + u] / 4.0f;  // quanto este traço está em toda parte
        v[u] = std::sqrt(v[u]) / (0.1f + common);
        norm += v[u] * v[u];
    }
    norm = std::sqrt(norm);
    if (norm > 0) for (float& x : v) x /= norm;
    return v;
}

// ---------------------------------------------------------------- ver (subir)

VisualCode VisualCortex::see(const Image& img, bool learn) {
    VisualCode code;
    const int v2dim = kGrid * kGrid * cfg_.v1_units;
    std::vector<float> v1_vec(v2dim, 0.0f);  // entrada de V2: coeficientes de V1 por pedaço
    std::vector<float> p, resid;

    // V1: cada pedaço é explicado por k traços, por "matching pursuit" hebbiano:
    // o traço mais parecido explica o que pode, o resto fica para o próximo.
    for (int gy = 0; gy < kGrid; ++gy)
        for (int gx = 0; gx < kGrid; ++gx) {
            patch_of(img, gx, gy, p);
            resid = p;
            std::vector<std::pair<int, float>> chosen;
            for (int k = 0; k < cfg_.v1_k; ++k) {
                int best = -1; float bs = 0.0f;
                for (int u = 0; u < cfg_.v1_units; ++u) {
                    // penaliza levemente unidades muito usadas: todas aprendem algo
                    const float s = dot(v1_[u], resid);
                    if (s > bs) { bs = s; best = u; }
                }
                if (best < 0 || bs < 0.05f) break;
                const float coef = dot(v1_[best], resid);
                chosen.push_back({best, coef});
                for (int i = 0; i < kPatchDim; ++i) resid[i] -= coef * v1_[best][i];
                if (learn && !v1_frozen_) {  // regra de Oja: o traço se move na direção do que explicou.
                    // Período crítico: quanto mais um traço já viu, menos ele muda
                    // (senão o dicionário deriva e os códigos de cima ficam velhos).
                    auto& w = v1_[best];
                    const float lr = cfg_.lr_v1 / (1.0f + v1_use_[best] / 200.0f);
                    for (int i = 0; i < kPatchDim; ++i) w[i] += lr * coef * (p[i] - coef * w[i]);
                    for (float& x : w) if (x < 0.0f) x = 0.0f;
                    normalize(w);
                    v1_use_[best] += 1.0f;
                }
            }
            code.v1_codes.push_back(chosen);
            const int base = (gy * kGrid + gx) * cfg_.v1_units;
            for (auto& [u, c] : chosen) v1_vec[base + u] = c;
        }

    // V2: o conjunto de traços (agrupado por quadrante) é resumido por k unidades.
    // Adaptação: traços presentes em quase tudo (o fundo) deixam de contar; só o
    // que é distintivo desta imagem chega a V2 (como neurônios que se adaptam).
    std::vector<float> in = pooled(code);
    if (learn) for (size_t i = 0; i < in.size(); ++i) in_mean_[i] += 0.01f * (in[i] - in_mean_[i]);
    // adaptação por divisão: traço que está sempre presente (fundo) vale pouco;
    // traço raro vale muito (como a resposta de um neurônio que se adapta ao comum)
    for (size_t i = 0; i < in.size(); ++i) in[i] = in[i] / (0.05f + cfg_.adapt * in_mean_[i]);
    if (std::getenv("VDEBUG")) { double n = 0; int nz = 0; for (float x : in) { n += x * x; nz += x > 0; } std::fprintf(stderr, "in: norma %.3f, nao-zero %d/%zu\n", std::sqrt(n), nz, in.size()); }
    normalize(in);
    code.v2_activity.assign(cfg_.v2_units, 0.0f);
    for (int u = 0; u < cfg_.v2_units; ++u) code.v2_activity[u] = std::max(0.0f, dot(v2_[u], in));
    // o código de saída é pura ativação (quem reconhece melhor); o boosting só
    // entra na hora de escolher QUEM APRENDE, para nenhuma unidade ficar sem uso
    std::vector<int> order(cfg_.v2_units);
    for (int u = 0; u < cfg_.v2_units; ++u) order[u] = u;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return code.v2_activity[a] > code.v2_activity[b]; });
    for (int k = 0; k < cfg_.v2_k; ++k) code.v2_active.push_back(order[k]);
    std::sort(code.v2_active.begin(), code.v2_active.end());
    if (std::getenv("VDEBUG") && !learn) {
        std::fprintf(stderr, "top:");
        for (int k = 0; k < 6; ++k) { int u = order[k]; int nz = 0; for (float x : v2_[u]) nz += x > 0.01f; std::fprintf(stderr, " u%d(a=%.2f,nz=%d,use=%.0f)", u, code.v2_activity[u], nz, v2_use_[u]); }
        std::fprintf(stderr, "\n");
    }
    if (learn) {
        std::vector<int> lorder = order;
        std::sort(lorder.begin(), lorder.end(), [&](int a, int b) {
            const float ba = std::exp(-cfg_.boost * (v2_duty_[a] - duty_target_));
            const float bb = std::exp(-cfg_.boost * (v2_duty_[b] - duty_target_));
            return code.v2_activity[a] * ba > code.v2_activity[b] * bb;
        });
        std::vector<uint8_t> won(cfg_.v2_units, 0);
        for (int rank = 0; rank < cfg_.v2_k; ++rank) {
            const int u = lorder[rank];
            won[u] = 1;
            float lr = rank == 0 ? cfg_.lr_v2 : cfg_.lr_v2 * 0.05f;  // só a vencedora vira protótipo de verdade
            lr /= 1.0f + v2_use_[u] / 50.0f;  // período crítico também aqui
            auto& w = v2_[u];
            for (size_t i = 0; i < w.size(); ++i) w[i] += lr * (in[i] - w[i]);
            for (float& x : w) if (x < 0.0f) x = 0.0f;
            normalize(w);
            v2_use_[u] += 1.0f;
            auto& g = v2_gen_[u];
            for (size_t i = 0; i < g.size(); ++i) g[i] += lr * (img.px[i] - g[i]);
        }
        for (int u = 0; u < cfg_.v2_units; ++u) v2_duty_[u] += 0.05f * (won[u] - v2_duty_[u]);
    }
    return code;
}

// ---------------------------------------------------------------- imaginar (descer)

Image VisualCortex::imagine(const std::vector<int>& units, const std::vector<float>* weights) const {
    // Descida: as unidades ativas reconstroem, pelas sinapses de volta, a imagem
    // que costumam ver. As mais ativas pesam muito mais (ativação ao quadrado).
    // Várias unidades de coisas diferentes juntas dão uma imagem mista: é assim
    // que se imagina o que nunca se viu.
    Image img;
    for (float& x : img.px) x = 0.0f;
    float total = 0.0f;
    // código de população: se vierem as ativações de todas as unidades, todas
    // contribuem, com peso muito concentrado nas mais ativas (ativação^4)
    std::vector<int> all;
    if (weights && static_cast<int>(weights->size()) == cfg_.v2_units) { all.resize(cfg_.v2_units); for (int u = 0; u < cfg_.v2_units; ++u) all[u] = u; }
    for (int u : (all.empty() ? units : all)) {
        float w = weights ? std::max(0.0f, (*weights)[u]) : 1.0f;
        w = w * w * w * w;
        if (w <= 0.0f) continue;
        for (size_t i = 0; i < img.px.size(); ++i) img.px[i] += w * v2_gen_[u][i];
        total += w;
    }
    if (total > 0.0f) for (float& x : img.px) x = std::clamp(x / total, 0.0f, 1.0f);
    return img;
}

Image VisualCortex::reconstruct_v1(const VisualCode& code) const {
    Image img;
    Image cover;
    std::vector<float> patch(kPatchDim), ones(kPatchDim, 1.0f);
    int idx = 0;
    for (int gy = 0; gy < kGrid; ++gy)
        for (int gx = 0; gx < kGrid; ++gx, ++idx) {
            std::fill(patch.begin(), patch.end(), 0.0f);
            for (auto& [u, c] : code.v1_codes[idx])
                for (int i = 0; i < kPatchDim; ++i) patch[i] += c * v1_[u][i];
            add_patch(img, gx, gy, patch);
            add_patch(cover, gx, gy, ones);
        }
    for (size_t i = 0; i < img.px.size(); ++i) img.px[i] = std::clamp(cover.px[i] > 0 ? img.px[i] / cover.px[i] : 0.0f, 0.0f, 1.0f);
    return img;
}

float VisualCortex::error(const Image& a, const Image& b) {
    double s = 0;
    for (size_t i = 0; i < a.px.size(); ++i) s += std::fabs(a.px[i] - b.px[i]);
    return static_cast<float>(s / a.px.size());
}

float VisualCortex::similarity(const VisualCode& a, const VisualCode& b) {
    int common = 0;
    for (int u : a.v2_active) for (int v : b.v2_active) if (u == v) ++common;
    return static_cast<float>(common) / std::max<size_t>(1, a.v2_active.size());
}

// ---------------------------------------------------------------- persistência

void VisualCortex::save(std::ostream& out) const {
    auto wv = [&](const std::vector<std::vector<float>>& m) {
        const uint32_t n = static_cast<uint32_t>(m.size()), d = static_cast<uint32_t>(m.empty() ? 0 : m[0].size());
        out.write(reinterpret_cast<const char*>(&n), sizeof n);
        out.write(reinterpret_cast<const char*>(&d), sizeof d);
        for (const auto& row : m) out.write(reinterpret_cast<const char*>(row.data()), d * sizeof(float));
    };
    wv(v1_); wv(v2_); wv(v2_gen_);
    out.write(reinterpret_cast<const char*>(in_mean_.data()), in_mean_.size() * sizeof(float));
    out.write(reinterpret_cast<const char*>(v1_use_.data()), v1_use_.size() * sizeof(float));
    out.write(reinterpret_cast<const char*>(v2_use_.data()), v2_use_.size() * sizeof(float));
    const uint32_t nd = static_cast<uint32_t>(dense_mu_.size());
    out.write(reinterpret_cast<const char*>(&nd), sizeof nd);
    out.write(reinterpret_cast<const char*>(dense_mu_.data()), nd * sizeof(float));
    out.write(reinterpret_cast<const char*>(dense_sd_.data()), nd * sizeof(float));
}

bool VisualCortex::load(std::istream& in) {
    auto rv = [&](std::vector<std::vector<float>>& m) {
        uint32_t n = 0, d = 0;
        in.read(reinterpret_cast<char*>(&n), sizeof n);
        in.read(reinterpret_cast<char*>(&d), sizeof d);
        m.assign(n, std::vector<float>(d));
        for (auto& row : m) in.read(reinterpret_cast<char*>(row.data()), d * sizeof(float));
    };
    rv(v1_); rv(v2_); rv(v2_gen_);
    in_mean_.resize(v2_.empty() ? 0 : v2_[0].size());
    in.read(reinterpret_cast<char*>(in_mean_.data()), in_mean_.size() * sizeof(float));
    v1_use_.resize(v1_.size()); v2_use_.resize(v2_.size()); v2_duty_.assign(v2_.size(), duty_target_);
    in.read(reinterpret_cast<char*>(v1_use_.data()), v1_use_.size() * sizeof(float));
    in.read(reinterpret_cast<char*>(v2_use_.data()), v2_use_.size() * sizeof(float));
    uint32_t nd = 0;
    in.read(reinterpret_cast<char*>(&nd), sizeof nd);
    if (nd > 100000) return false;
    dense_mu_.resize(nd); dense_sd_.resize(nd);
    in.read(reinterpret_cast<char*>(dense_mu_.data()), nd * sizeof(float));
    in.read(reinterpret_cast<char*>(dense_sd_.data()), nd * sizeof(float));
    cfg_.v1_units = static_cast<int>(v1_.size());
    cfg_.v2_units = static_cast<int>(v2_.size());
    return static_cast<bool>(in);
}

// ---------------------------------------------------------------- desenhos

namespace {
struct Pen {
    Image& img; std::mt19937& rng; float var;
    float jit() { std::uniform_real_distribution<float> u(-var, var); return u(rng); }
    void fill(float r, float g, float b) { for (int y = 0; y < kImg; ++y) for (int x = 0; x < kImg; ++x) set(x, y, r, g, b); }
    void set(int x, int y, float r, float g, float b) {
        if (x < 0 || y < 0 || x >= kImg || y >= kImg) return;
        img.at(x, y, 0) = std::clamp(r, 0.0f, 1.0f); img.at(x, y, 1) = std::clamp(g, 0.0f, 1.0f); img.at(x, y, 2) = std::clamp(b, 0.0f, 1.0f);
    }
    void rect(int x0, int y0, int x1, int y1, float r, float g, float b) { for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) set(x, y, r, g, b); }
    void disc(int cx, int cy, int rad, float r, float g, float b) {
        for (int y = cy - rad; y <= cy + rad; ++y) for (int x = cx - rad; x <= cx + rad; ++x)
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= rad * rad) set(x, y, r, g, b);
    }
    void tri(int cx, int base_y, int w, int h, float r, float g, float b) {  // triângulo apontando para cima
        for (int dy = 0; dy < h; ++dy) { const int half = w * (h - dy) / (2 * h); for (int x = cx - half; x <= cx + half; ++x) set(x, base_y - dy, r, g, b); }
    }
};
}  // namespace

Image draw_object(const std::string& name, std::mt19937& rng, float variation) {
    Image img;
    Pen p{img, rng, variation};
    std::uniform_int_distribution<int> off(-2, 2);
    const int dx = variation > 0 ? off(rng) : 0, dy = variation > 0 ? off(rng) : 0;
    const float t = p.jit() * 0.3f;  // variação de tom
    p.fill(0.92f + t * 0.1f, 0.92f + t * 0.1f, 0.90f);  // fundo claro neutro
    if (name == "mar") {
        p.rect(0, 12 + dy, 31, 31, 0.1f + t, 0.35f + t, 0.8f);
        for (int y = 14 + dy; y < 32; y += 5) for (int x = 0; x < 32; x += 6) p.rect(x, y, x + 3, y, 0.6f, 0.8f, 1.0f);
        p.rect(0, 0, 31, 11 + dy, 0.55f, 0.75f, 0.95f);  // céu
    } else if (name == "sol") {
        p.fill(0.55f, 0.75f, 0.95f);
        p.disc(16 + dx, 14 + dy, 7, 1.0f, 0.85f + t, 0.1f);
        for (int a = 0; a < 8; ++a) { const float ang = a * 3.14159f / 4; p.disc(16 + dx + int(11 * std::cos(ang)), 14 + dy + int(11 * std::sin(ang)), 1, 1.0f, 0.8f, 0.2f); }
    } else if (name == "areia") {
        p.fill(0.9f + t * 0.1f, 0.8f, 0.5f);
        std::uniform_int_distribution<int> px(0, 31);
        for (int i = 0; i < 60; ++i) { const int x = px(rng), y = px(rng); p.set(x, y, 0.75f, 0.6f, 0.35f); }
    } else if (name == "barco") {
        p.rect(0, 18 + dy, 31, 31, 0.1f, 0.35f, 0.8f);
        p.rect(8 + dx, 18 + dy, 24 + dx, 23 + dy, 0.55f, 0.3f, 0.1f);
        p.rect(15 + dx, 4 + dy, 16 + dx, 17 + dy, 0.4f, 0.25f, 0.1f);
        p.tri(20 + dx, 17 + dy, 10, 12, 1.0f, 1.0f, 1.0f);
    } else if (name == "flor") {
        p.rect(15 + dx, 16 + dy, 16 + dx, 30, 0.2f, 0.6f, 0.2f);
        p.disc(10 + dx, 24 + dy, 2, 0.2f, 0.6f, 0.2f);
        const float r = 0.9f, g = 0.2f + std::fabs(t), b = 0.5f + t;
        for (int a = 0; a < 6; ++a) { const float ang = a * 3.14159f / 3; p.disc(15 + dx + int(5 * std::cos(ang)), 11 + dy + int(5 * std::sin(ang)), 3, r, g, b); }
        p.disc(15 + dx, 11 + dy, 3, 1.0f, 0.9f, 0.2f);
    } else if (name == "arvore") {
        p.rect(14 + dx, 16 + dy, 17 + dx, 31, 0.45f, 0.28f, 0.1f);
        p.disc(15 + dx, 11 + dy, 9, 0.15f, 0.55f + t, 0.15f);
    } else if (name == "passaro") {
        p.fill(0.55f, 0.75f, 0.95f);
        p.disc(16 + dx, 16 + dy, 5, 0.3f + t, 0.3f, 0.3f);
        p.disc(21 + dx, 12 + dy, 3, 0.3f, 0.3f, 0.3f);
        p.rect(24 + dx, 12 + dy, 27 + dx, 12 + dy, 1.0f, 0.7f, 0.1f);
        p.tri(12 + dx, 15 + dy, 10, 5, 0.25f, 0.25f, 0.25f);
    } else if (name == "cama") {
        p.rect(3 + dx, 14 + dy, 28 + dx, 24 + dy, 0.8f, 0.3f + t, 0.3f);
        p.rect(3 + dx, 10 + dy, 28 + dx, 14 + dy, 1.0f, 1.0f, 1.0f);
        p.rect(3 + dx, 6 + dy, 5 + dx, 26 + dy, 0.45f, 0.28f, 0.1f);
        p.rect(26 + dx, 10 + dy, 28 + dx, 26 + dy, 0.45f, 0.28f, 0.1f);
    } else if (name == "janela") {
        p.rect(6 + dx, 4 + dy, 25 + dx, 23 + dy, 0.55f, 0.75f, 0.95f);
        p.rect(6 + dx, 4 + dy, 25 + dx, 5 + dy, 0.5f, 0.3f, 0.1f); p.rect(6 + dx, 22 + dy, 25 + dx, 23 + dy, 0.5f, 0.3f, 0.1f);
        p.rect(6 + dx, 4 + dy, 7 + dx, 23 + dy, 0.5f, 0.3f, 0.1f); p.rect(24 + dx, 4 + dy, 25 + dx, 23 + dy, 0.5f, 0.3f, 0.1f);
        p.rect(15 + dx, 4 + dy, 16 + dx, 23 + dy, 0.5f, 0.3f, 0.1f); p.rect(6 + dx, 13 + dy, 25 + dx, 14 + dy, 0.5f, 0.3f, 0.1f);
    } else if (name == "livro") {
        p.rect(8 + dx, 6 + dy, 23 + dx, 26 + dy, 0.2f, 0.3f + t, 0.7f);
        p.rect(8 + dx, 6 + dy, 10 + dx, 26 + dy, 0.1f, 0.15f, 0.4f);
        p.rect(13 + dx, 10 + dy, 20 + dx, 11 + dy, 0.95f, 0.9f, 0.7f);
    } else if (name == "mesa") {
        p.rect(3 + dx, 12 + dy, 28 + dx, 15 + dy, 0.6f, 0.4f + t, 0.2f);
        p.rect(5 + dx, 16 + dy, 7 + dx, 28 + dy, 0.5f, 0.32f, 0.15f);
        p.rect(24 + dx, 16 + dy, 26 + dx, 28 + dy, 0.5f, 0.32f, 0.15f);
    } else if (name == "pao") {
        p.disc(16 + dx, 17 + dy, 8, 0.85f, 0.6f + t, 0.3f);
        p.rect(8 + dx, 17 + dy, 24 + dx, 24 + dy, 0.85f, 0.6f, 0.3f);
        p.rect(12 + dx, 14 + dy, 20 + dx, 14 + dy, 0.95f, 0.8f, 0.5f);
    } else if (name == "cafe") {
        p.rect(9 + dx, 10 + dy, 21 + dx, 24 + dy, 1.0f, 1.0f, 1.0f);
        p.rect(10 + dx, 11 + dy, 20 + dx, 15 + dy, 0.35f, 0.2f + t, 0.1f);
        p.rect(22 + dx, 13 + dy, 25 + dx, 20 + dy, 1.0f, 1.0f, 1.0f);
        p.rect(23 + dx, 15 + dy, 24 + dx, 18 + dy, 0.92f, 0.92f, 0.9f);
    } else if (name == "fruta") {
        p.disc(16 + dx, 18 + dy, 8, 0.9f, 0.15f + std::fabs(t), 0.15f);
        p.rect(16 + dx, 7 + dy, 16 + dx, 10 + dy, 0.3f, 0.5f, 0.2f);
        p.disc(19 + dx, 9 + dy, 2, 0.2f, 0.6f, 0.2f);
    } else if (name == "bolo") {
        p.rect(6 + dx, 14 + dy, 25 + dx, 26 + dy, 0.8f, 0.55f + t, 0.3f);
        p.rect(6 + dx, 12 + dy, 25 + dx, 15 + dy, 0.95f, 0.5f, 0.6f);
        p.rect(15 + dx, 6 + dy, 16 + dx, 11 + dy, 1.0f, 0.9f, 0.3f);
    } else {
        p.disc(16 + dx, 16 + dy, 6, 0.5f, 0.5f, 0.5f);
    }
    return img;
}

Image draw_place(const std::string& place, std::mt19937& rng, float variation) {
    Image img;
    Pen p{img, rng, variation};
    const float t = p.jit() * 0.3f;
    if (place == "praia") { p.rect(0, 0, 31, 10, 0.55f, 0.75f, 0.95f); p.rect(0, 11, 31, 20, 0.1f, 0.35f + t, 0.8f); p.rect(0, 21, 31, 31, 0.9f, 0.8f, 0.5f); }
    else if (place == "jardim") { p.rect(0, 0, 31, 14, 0.55f, 0.75f, 0.95f); p.rect(0, 15, 31, 31, 0.2f, 0.6f + t, 0.2f); }
    else if (place == "cozinha") { p.fill(0.95f, 0.9f + t * 0.1f, 0.75f); p.rect(0, 22, 31, 31, 0.6f, 0.5f, 0.4f); }
    else { p.fill(0.85f, 0.8f + t * 0.1f, 0.9f); p.rect(0, 24, 31, 31, 0.5f, 0.4f, 0.3f); }
    return img;
}

// ---------------------------------------------------------------- desenhos de tudo
// Cada bicho, pessoa, comida e coisa do vocabulário ganha uma aparência própria,
// sorteada uma vez a partir do nome (forma, cor, um detalhe, tamanho), e vista
// cada vez um pouco diferente (posição, tom, tamanho). Não são desenhos
// realistas: são aparências distintas e estáveis, que é o que os olhos precisam
// para formar um conceito ("aquela coisa redonda laranja de pintas").

static uint64_t name_hash(const std::string& s) {
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    h ^= h >> 33; h *= 0xff51afd7ed558ccdull; h ^= h >> 33;
    return h;
}
static const float kPalette[12][3] = {
    {0.85f, 0.15f, 0.15f}, {0.95f, 0.55f, 0.10f}, {0.95f, 0.85f, 0.15f}, {0.55f, 0.80f, 0.15f},
    {0.10f, 0.60f, 0.25f}, {0.10f, 0.70f, 0.70f}, {0.15f, 0.45f, 0.90f}, {0.20f, 0.20f, 0.65f},
    {0.55f, 0.25f, 0.75f}, {0.90f, 0.40f, 0.70f}, {0.50f, 0.30f, 0.15f}, {0.25f, 0.25f, 0.25f}};

static std::unordered_map<std::string, int>& sprite_index() { static std::unordered_map<std::string, int> m; return m; }
void set_sprite_names(const std::vector<std::string>& names) {
    auto& m = sprite_index();
    m.clear();
    for (size_t i = 0; i < names.size(); ++i) m[names[i]] = static_cast<int>(i);
}

Image draw_sprite(const std::string& name, std::mt19937& rng, float variation) {
    Image img;
    Pen p{img, rng, variation};
    // Aparência: se a lista de nomes foi dada, cada um ganha uma combinação única
    // (as mais diferentes primeiro: muda a cor, depois a forma, depois a parte);
    // senão, sorteada do nome.
    uint64_t h = name_hash(name);
    auto take = [&](int n) { const int v = static_cast<int>(h % n); h /= n; return v; };
    int shape = take(8), color = take(12), big = take(2), part = take(8), pcolor = 0, detail = take(4);
    pcolor = (color + 1 + take(11)) % 12;
    auto it = sprite_index().find(name);
    if (it != sprite_index().end()) {
        const int k = it->second;
        color = k % 12;
        shape = (k / 12 + k) % 8;
        part = (k / 96 + k / 12) % 8;
        pcolor = (color + 3 + (k / 12) % 7) % 12;
        big = (k / 24) % 2;
        detail = (k / 48) % 4;
    }
    // corpo (forma, cor, tamanho) e uma segunda parte grande (cabeça, faixa, orelhas,
    // base, aro, metade de outra cor, cauda), de outra cor, e um detalhe pequeno
    std::uniform_int_distribution<int> off(-2, 2);
    const int dx = variation > 0 ? off(rng) : 0, dy = variation > 0 ? off(rng) : 0;
    const float t = p.jit() * 0.15f;
    const int rad = big ? 8 : 6;
    p.fill(0.93f + t * 0.05f, 0.93f + t * 0.05f, 0.91f);
    const float r = kPalette[color][0] + t, g = kPalette[color][1] + t, b = kPalette[color][2] + t;
    const float fr = kPalette[pcolor][0], fg = kPalette[pcolor][1], fb = kPalette[pcolor][2];
    const int cx = 16 + dx, cy = 17 + dy;
    if (part == 5) p.disc(cx, cy, rad + 3, fr, fg, fb);  // aro atrás do corpo
    switch (shape) {
        case 0: p.disc(cx, cy, rad, r, g, b); break;
        case 1: p.rect(cx - rad, cy - rad, cx + rad, cy + rad, r, g, b); break;
        case 2: p.tri(cx, cy + rad, 2 * rad + 2, 2 * rad, r, g, b); break;
        case 3: for (int y = -rad / 2; y <= rad / 2; ++y) for (int x = -rad - 3; x <= rad + 3; ++x) if (x * x * 4 + y * y * 4 * 6 <= (rad + 3) * (rad + 3) * 4) p.set(cx + x, cy + y, r, g, b); break;
        case 4: for (int y = -rad - 3; y <= rad + 3; ++y) for (int x = -rad / 2; x <= rad / 2; ++x) if (y * y * 4 + x * x * 4 * 6 <= (rad + 3) * (rad + 3) * 4) p.set(cx + x, cy + y, r, g, b); break;
        case 5: p.disc(cx, cy, rad, r, g, b); p.disc(cx, cy, rad / 2, 0.93f, 0.93f, 0.91f); break;
        case 6: p.rect(cx - rad, cy - 2, cx + rad, cy + 2, r, g, b); p.rect(cx - 2, cy - rad, cx + 2, cy + rad, r, g, b); break;
        default: for (int y = -rad; y <= rad; ++y) for (int x = -rad; x <= rad; ++x) if (std::abs(x) + std::abs(y) <= rad) p.set(cx + x, cy + y, r, g, b); break;
    }
    switch (part) {
        case 1: p.disc(cx, cy - rad - 3, 4, fr, fg, fb); break;                                            // cabeça
        case 2: p.rect(cx - rad - 1, cy - 2, cx + rad + 1, cy + 2, fr, fg, fb); break;                     // faixa larga
        case 3: p.tri(cx - rad + 2, cy - rad + 1, 6, 6, fr, fg, fb); p.tri(cx + rad - 2, cy - rad + 1, 6, 6, fr, fg, fb); break;  // orelhas
        case 4: p.rect(cx - rad - 3, cy + rad, cx + rad + 3, cy + rad + 3, fr, fg, fb); break;             // base
        case 6: for (int y = cy - rad; y <= cy + rad; ++y) for (int x = cx; x <= cx + rad; ++x)            // metade de outra cor
                    if (img.at(std::clamp(x, 0, kImg - 1), std::clamp(y, 0, kImg - 1), 0) == std::clamp(r, 0.0f, 1.0f)) p.set(x, y, fr, fg, fb);
                break;
        case 7: p.rect(cx + rad, cy, cx + rad + 6, cy + 2, fr, fg, fb); p.rect(cx + rad + 5, cy - 4, cx + rad + 6, cy + 2, fr, fg, fb); break;  // cauda
        default: break;
    }
    switch (detail) {
        case 1: p.disc(cx - rad / 3, cy - rad / 3, 1, 0.05f, 0.05f, 0.05f); p.disc(cx + rad / 3, cy - rad / 3, 1, 0.05f, 0.05f, 0.05f); break;  // olhos
        case 2: p.disc(cx - rad / 2, cy + 1, 1, 1.0f, 1.0f, 1.0f); p.disc(cx + rad / 2, cy + 2, 1, 1.0f, 1.0f, 1.0f); p.disc(cx, cy + rad / 2, 1, 1.0f, 1.0f, 1.0f); break;  // pintas
        case 3: p.rect(cx - 1, cy - rad, cx + 1, cy + rad, 0.15f, 0.15f, 0.15f); break;                    // risco
        default: break;
    }
    return img;
}

// Um lugar visto: cor de fundo e um padrão (faixas, listras, xadrez, chão, bolinhas, diagonais)
Image draw_backdrop(const std::string& place, std::mt19937& rng, float variation) {
    Image img;
    Pen p{img, rng, variation};
    uint64_t h = name_hash("lugar:" + place);
    auto take = [&](int n) { const int v = static_cast<int>(h % n); h /= n; return v; };
    const int base = take(12), pattern = take(6), second = (base + 3 + take(8)) % 12;
    const float t = p.jit() * 0.2f;
    auto light = [&](int c, int k) { return 0.45f + 0.5f * kPalette[c][k] + t; };
    p.fill(light(base, 0), light(base, 1), light(base, 2));
    const float sr = kPalette[second][0], sg = kPalette[second][1], sb = kPalette[second][2];
    std::uniform_int_distribution<int> off(0, 3);
    const int o = variation > 0 ? off(rng) : 0;
    switch (pattern) {
        case 0: for (int y = o; y < kImg; y += 8) p.rect(0, y, 31, y + 2, sr, sg, sb); break;
        case 1: for (int x = o; x < kImg; x += 8) p.rect(x, 0, x + 2, 31, sr, sg, sb); break;
        case 2: for (int y = 0; y < kImg; ++y) for (int x = 0; x < kImg; ++x) if (((x + o) / 6 + (y + o) / 6) % 2) p.set(x, y, sr, sg, sb); break;
        case 3: p.rect(0, 20 + o, 31, 31, sr, sg, sb); break;
        case 4: for (int y = 3 + o; y < kImg; y += 7) for (int x = 3 + o; x < kImg; x += 7) p.disc(x, y, 1, sr, sg, sb); break;
        default: for (int y = 0; y < kImg; ++y) for (int x = 0; x < kImg; ++x) if (((x + y + o) / 4) % 3 == 0) p.set(x, y, sr, sg, sb); break;
    }
    return img;
}

bool write_ppm(const std::string& path, const std::vector<Image>& row, int scale) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    const int W = kImg * scale * static_cast<int>(row.size()) + 2 * (static_cast<int>(row.size()) - 1), H = kImg * scale;
    out << "P6\n" << W << " " << H << "\n255\n";
    for (int y = 0; y < H; ++y)
        for (size_t k = 0; k < row.size(); ++k) {
            for (int x = 0; x < kImg * scale; ++x)
                for (int c = 0; c < 3; ++c) out.put(static_cast<char>(std::clamp(int(row[k].at(x / scale, y / scale, c) * 255), 0, 255)));
            if (k + 1 < row.size()) for (int s = 0; s < 2; ++s) for (int c = 0; c < 3; ++c) out.put(char(40));
        }
    return static_cast<bool>(out);
}

}  // namespace eva2
