// Eva - neuromoduladores: sinais químicos globais que mudam como o cérebro funciona
#pragma once

namespace eva {

// Dopamina: sinal de recompensa/importância. Tem um nível basal e picos
// quando algo bom ou surpreendente acontece; o pico decai com o tempo.
// Memórias formadas durante um pico ficam mais fortes ("peso emocional").
struct Dopamine {
    float baseline = 0.3f;
    float level = 0.3f;
    float tau = 300.0f;  // ms para voltar ao basal

    // Humor: a dopamina tônica. Integra os picos devagar e decai em ~10 s.
    // O pico é "gostei disso"; o humor é "estou bem".
    float mood = 0.0f;
    float mood_tau = 10000.0f;
    float mood_gain = 0.001f;

    void burst(float amount) { level += amount; }
    void step(float dt) {
        mood += mood_gain * (level - baseline) * dt - mood * (dt / mood_tau);
        if (mood < 0.0f) mood = 0.0f;
        level += (baseline - level) * (dt / tau);
    }
};

// Acetilcolina: decide o "modo" do cérebro.
// Alta (acordada): o hipocampo grava o presente rápido.
// Baixa (sono): o hipocampo repete o passado e o córtex consolida.
enum class Mode { Awake, Sleep };

}  // namespace eva
