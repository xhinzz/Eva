// Eva - impulsos internos (motivação)
//
// No cérebro, a motivação não nasce no córtex: nasce em estruturas antigas
// (hipotálamo, área tegmental ventral) que mantêm "necessidades" que crescem
// com o tempo e são saciadas por ações. Fome, sono, curiosidade e a vontade de
// compartilhar funcionam assim. Aqui modelamos duas delas como variáveis
// contínuas, não como neurônios: é o nível de detalhe certo para esta camada.
#pragma once
#include <algorithm>

namespace eva {

struct Drives {
    // Vontade de falar: cresce com o tempo em silêncio e com emoção
    // (dopamina acima do basal). Saciada ao falar; reduzida quando alguém fala com ela.
    float talk = 0.0f;
    float talk_tau = 8000.0f;        // ms para ir de 0 a 1 só com o tempo
    float talk_emotion_gain = 0.0004f;

    // Curiosidade: cresce com o tempo sem novidade. Saciada por surpresa.
    float curiosity = 0.3f;
    float curiosity_tau = 15000.0f;

    void step(float dt, float dopamine, float dopamine_baseline) {
        talk += dt / talk_tau;
        const float emotion = std::max(0.0f, dopamine - dopamine_baseline);
        talk += talk_emotion_gain * emotion * dt;
        curiosity += dt / curiosity_tau;
        talk = std::min(talk, 2.0f);
        curiosity = std::min(curiosity, 2.0f);
    }

    void on_spoke() { talk = 0.0f; }
    void on_heard(float surprise) {
        talk *= 0.5f;  // ouvir alguém alivia um pouco a vontade de falar
        curiosity = std::max(0.0f, curiosity - surprise);  // o novo sacia a curiosidade
    }
    void on_asked() { curiosity = 0.2f; }
};

}  // namespace eva
