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
#include <cstdint>
#include <iosfwd>
#include <random>
#include <string>
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
};

// Desenhos simples das coisas do mundo dela, com variação (posição, tom)
Image draw_object(const std::string& name, std::mt19937& rng, float variation = 0.0f);
Image draw_place(const std::string& place, std::mt19937& rng, float variation = 0.0f);

// grava PPM (P6); conversão para PNG fica para fora
bool write_ppm(const std::string& path, const std::vector<Image>& row, int scale = 4);

}  // namespace eva2
