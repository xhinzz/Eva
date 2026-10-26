// Eva 2 - olhos e córtex visual.
//
// Imagens pequenas (32x32, cor). O córtex visual tem duas camadas, ambas com
// aprendizado hebbiano competitivo (sem professor, sem retropropagação):
//   V1: dicionário de pedaços (8x8). Cada pedaço da imagem é descrito por
//       poucos "traços" (bordas, cores) que a camada aprendeu vendo.
//   V2: código do objeto: poucas unidades ativas que resumem o conjunto de
//       traços. Objetos parecidos compartilham unidades, porque compartilham traços.
// As mesmas sinapses servem para subir (ver) e descer (imaginar): ativar um
// código em V2 reconstrói traços em V1, que reconstroem pixels. Lembrar é
// gerar; imaginar o desconhecido é gerar a partir de vários códigos juntos.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <iosfwd>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace eva2 {

constexpr int kImg = 32;        // lado da imagem
constexpr int kPatch = 8;       // lado do pedaço
constexpr int kStride = 4;            // pedaços sobrepostos (passo 4): código mais estável a deslocamentos
constexpr int kGrid = (kImg - kPatch) / kStride + 1;  // 7x7 pedaços
constexpr int kPatchDim = kPatch * kPatch * 3;

struct Image {
    std::array<float, kImg * kImg * 3> px{};  // 0..1, RGB
    float& at(int x, int y, int c) { return px[(y * kImg + x) * 3 + c]; }
    float at(int x, int y, int c) const { return px[(y * kImg + x) * 3 + c]; }
};

struct VisionConfig {
    int v1_units = 96;     // traços no dicionário
    int v1_k = 3;          // traços ativos por pedaço
    int v2_units = 160;    // unidades de objeto
    int v2_k = 6;          // unidades ativas por objeto
    float lr_v1 = 0.05f, lr_v2 = 0.05f;
    float boost = 0.0f;    // boosting homeostático em V2 (0 = desligado: as unidades nascem de imagens reais, então nenhuma fica morta)
    float adapt = 0.9f;    // adaptação: quanto do traço "comum" é descontado antes de V2
};

struct VisualCode {
    std::vector<int> v2_active;           // unidades ativas (o "código do objeto")
    std::vector<float> v2_activity;       // ativação de cada unidade (0..)
    std::vector<std::vector<std::pair<int, float>>> v1_codes;  // por pedaço: (traço, coeficiente)
};

class VisualCortex {
public:
    explicit VisualCortex(VisionConfig cfg = {}, unsigned seed = 3);

    // Semear: as unidades nascem parecidas com pedaços/códigos reais (senão
    // começam como ruído e muitas nunca vencem). Chame com algumas imagens antes de aprender.
    void prime(const std::vector<Image>& samples);
    // Ver: sobe a imagem pelas camadas; se `learn`, as sinapses se ajustam (Oja/competitivo)
    VisualCode see(const Image& img, bool learn);
    // Imaginar: desce de um código de V2 até pixels
    Image imagine(const std::vector<int>& v2_units, const std::vector<float>* weights = nullptr) const;
    Image imagine(const VisualCode& code) const { return imagine(code.v2_active, &code.v2_activity); }
    // Reconstrução direta pela via V1 (sem passar por V2): mostra o que V1 "sabe"
    Image reconstruct_v1(const VisualCode& code) const;

    static float error(const Image& a, const Image& b);  // erro médio por pixel (0..1)
    // semelhança entre dois códigos de objeto (0..1): quantas unidades em comum
    static float similarity(const VisualCode& a, const VisualCode& b);

    const VisionConfig& config() const { return cfg_; }
    std::vector<float> pooled_public(const VisualCode& c) const { return pooled(c); }  // depuração
    // Descrição que não depende de onde a coisa está na imagem: quanto cada traço
    // de V1 aparece na imagem inteira (agrupamento máximo, como as células
    // complexas e o córtex temporal; modelo HMAX), descontado o que é comum a
    // tudo (o fundo). É o que os conceitos usam.
    std::vector<float> invariant(const VisualCode& c) const;
    // a mesma ideia, mais estável: para cada traço de V1, a resposta máxima dele em
    // qualquer pedaço da imagem (correlação densa + máximo, a camada C1 do HMAX),
    // em desvios do que é típico (cada traço comparado com a média e a variação
    // dele nas coisas vistas na infância dos olhos)
    std::vector<float> invariant_dense(const Image& img) const;
    std::vector<int> prime_origin;  // depuração: índice da amostra que semeou cada unidade de V2
    void save(std::ostream& out) const;
    bool load(std::istream& in);

private:
    void patch_of(const Image& img, int gx, int gy, std::vector<float>& out) const;
    std::vector<float> pooled(const VisualCode& c) const;
    void add_patch(Image& img, int gx, int gy, const std::vector<float>& p) const;

    VisionConfig cfg_;
    std::mt19937 rng_;
    std::vector<std::vector<float>> v1_;  // [unit][kPatchDim], normalizados
    std::vector<std::vector<float>> v2_;  // [unit][entrada agrupada], normalizados (reconhecer: subir)
    std::vector<std::vector<float>> v2_gen_;  // [unit][pixels]: sinapses de descida, hebbianas (imaginar)
    int v2_in_dim() const { return 4 * cfg_.v1_units; }  // traços agrupados por quadrante (invariância a posição)
    std::vector<float> v1_use_, v2_use_;  // quantas vezes cada unidade venceu
    std::vector<float> in_mean_;          // média dos traços agrupados (adaptação)
    std::vector<float> v2_duty_;          // fração recente de vitórias (para o boosting)
    float duty_target_ = 0.05f;
    bool v1_frozen_ = false;  // período crítico de V1 fechado (depois de prime)
    std::vector<float> dense_mu_, dense_sd_;  // estatística das respostas densas (para a descrição invariante)
    std::vector<float> dense_raw(const Image& img) const;
};

// Conceitos que os olhos formam sozinhos: a descrição invariante de cada coisa
// vista é comparada com os protótipos já formados;
// se combina bastante, é a mesma coisa (e o protótipo aprende); se não, é uma
// coisa nova. Ninguém dá o nome: o conceito nasce antes da palavra. Duas coisas
// parecidas demais viram uma só; uma coisa vista de jeitos muito diferentes pode
// virar duas. São os erros de verdade de um sistema visual.
class ConceptMemory {
public:
    float threshold = 0.5f;   // cosseno mínimo com algum exemplar para ser "a mesma coisa"
    int max_exemplars = 8;    // cada conceito guarda alguns exemplares vistos (teoria dos exemplares)
    int recognize(const std::vector<float>& v, bool learn) {
        int best = -1; float bs = -2.0f;
        const float nv = norm(v);
        // parecença com um conceito = média da parecença com os exemplares dele
        // (a média não deixa um conceito "escorrer" de exemplar em exemplar)
        for (size_t i = 0; i < ex_.size(); ++i) {
            float sum = 0.0f;
            for (const auto& e : ex_[i]) {
                float d = 0.0f;
                for (size_t k = 0; k < v.size(); ++k) d += v[k] * e[k];
                sum += d / (nv * norm(e) + 1e-9f);
            }
            const float sc = sum / ex_[i].size();
            if (sc > bs) { bs = sc; best = static_cast<int>(i); }
        }
        if (best >= 0 && bs >= threshold) {
            if (learn) {
                auto& list = ex_[best];
                if (static_cast<int>(list.size()) < max_exemplars) list.push_back(v);
                else list[seen_[best] % max_exemplars] = v;
                ++seen_[best];
            }
            return best;
        }
        if (!learn) return best;
        ex_.push_back({v});
        seen_.push_back(1);
        return static_cast<int>(ex_.size()) - 1;
    }
    // parecença de uma vista com um conceito (média sobre os exemplares)
    float similarity(const std::vector<float>& v, int id) const {
        if (id < 0 || id >= static_cast<int>(ex_.size()) || ex_[id].empty()) return -2.0f;
        const float nv = norm(v);
        float sum = 0.0f;
        for (const auto& e : ex_[id]) { float d = 0.0f; for (size_t k = 0; k < v.size(); ++k) d += v[k] * e[k]; sum += d / (nv * norm(e) + 1e-9f); }
        return sum / ex_[id].size();
    }
    std::pair<int, float> best(const std::vector<float>& v) const {
        int b = -1; float bs = -2.0f;
        for (size_t i = 0; i < ex_.size(); ++i) {
            if (ex_[i].empty()) continue;  // conceito que foi juntado a outro
            const float sc = similarity(v, static_cast<int>(i));
            if (sc > bs) { bs = sc; b = static_cast<int>(i); }
        }
        return {b, bs};
    }
    // `from` era a mesma coisa que `into`: os exemplares passam para `into`
    void merge(int into, int from) {
        if (into == from || from < 0 || into < 0) return;
        for (auto& e : ex_[from]) add_exemplar(into, e);
        ex_[from].clear();
    }
    bool alive(int id) const { return id >= 0 && id < static_cast<int>(ex_.size()) && !ex_[id].empty(); }
    void add_exemplar(int id, const std::vector<float>& v) {
        auto& list = ex_[id];
        if (static_cast<int>(list.size()) < max_exemplars) list.push_back(v);
        else list[seen_[id] % max_exemplars] = v;
        ++seen_[id];
    }
    int create(const std::vector<float>& v) { ex_.push_back({v}); seen_.push_back(1); return static_cast<int>(ex_.size()) - 1; }
    size_t size() const { size_t n = 0; for (const auto& e : ex_) n += !e.empty(); return n; }
    template <class Out> void save(Out& out) const {
        const uint32_t n = static_cast<uint32_t>(ex_.size());
        out.write(reinterpret_cast<const char*>(&n), sizeof n);
        for (size_t i = 0; i < ex_.size(); ++i) {
            const uint32_t m = static_cast<uint32_t>(ex_[i].size()), d = m ? static_cast<uint32_t>(ex_[i][0].size()) : 0;
            out.write(reinterpret_cast<const char*>(&m), sizeof m);
            out.write(reinterpret_cast<const char*>(&d), sizeof d);
            out.write(reinterpret_cast<const char*>(&seen_[i]), sizeof(uint32_t));
            for (const auto& e : ex_[i]) out.write(reinterpret_cast<const char*>(e.data()), d * sizeof(float));
        }
    }
    template <class In> bool load(In& in) {
        uint32_t n = 0;
        if (!in.read(reinterpret_cast<char*>(&n), sizeof n) || n > 1000000) return false;
        ex_.assign(n, {}); seen_.assign(n, 0);
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t m = 0, d = 0;
            in.read(reinterpret_cast<char*>(&m), sizeof m);
            in.read(reinterpret_cast<char*>(&d), sizeof d);
            in.read(reinterpret_cast<char*>(&seen_[i]), sizeof(uint32_t));
            if (m > 1000 || d > 100000) return false;
            ex_[i].assign(m, std::vector<float>(d));
            for (auto& e : ex_[i]) in.read(reinterpret_cast<char*>(e.data()), d * sizeof(float));
        }
        return static_cast<bool>(in);
    }
private:
    static float norm(const std::vector<float>& v) { float s = 0; for (float x : v) s += x * x; return std::sqrt(s); }
    std::vector<std::vector<std::vector<float>>> ex_;
    std::vector<uint32_t> seen_;
};

// Desenhos simples das coisas do mundo dela, com variação (posição, tom)
Image draw_object(const std::string& name, std::mt19937& rng, float variation = 0.0f);
Image draw_place(const std::string& place, std::mt19937& rng, float variation = 0.0f);

// Aparência própria de cada coisa do vocabulário (sorteada uma vez a partir do nome)
// e de cada lugar (fundo com padrão); vista cada vez um pouco diferente
Image draw_sprite(const std::string& name, std::mt19937& rng, float variation = 1.0f);
// dá a lista de nomes para que cada um ganhe uma aparência única (sem colisões)
void set_sprite_names(const std::vector<std::string>& names);
Image draw_backdrop(const std::string& place, std::mt19937& rng, float variation = 1.0f);

// grava PPM (P6); conversão para PNG fica para fora
bool write_ppm(const std::string& path, const std::vector<Image>& row, int scale = 4);

}  // namespace eva2
