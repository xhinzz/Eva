#include "eva.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace eva {

Eva::Eva(std::vector<std::string> concepts, EvaConfig cfg, unsigned seed)
    : concepts_(std::move(concepts)), cfg_(cfg), rng_(seed) {
    cfg_.max_concepts = std::max<int>(cfg_.max_concepts, static_cast<int>(concepts_.size()) + 4);
    const int G = cfg_.group_size;
    NeuronParams cxn{};
    cxn.adapt_inc = cfg_.cx_adapt_inc;
    cortex_ = std::make_unique<Population>("cortex", cfg_.max_concepts * G, cxn);
    NeuronParams hpn{};
    hpn.trace_tau = cfg_.hp_trace_tau;
    hpn.adapt_inc = cfg_.hp_adapt_inc;
    hpn.adapt_tau = cfg_.hp_adapt_tau;
    hippo_ = std::make_unique<Population>("hipocampo", cfg_.hippo_size, hpn);
    hippo_->set_max_active(cfg_.hippo_max_active);
    cortex_->set_max_active(cfg_.cortex_max_active);

    // Córtex <-> córtex: onde fica o conhecimento de longo prazo. Aprende DEVAGAR.
    PlasticityParams cx{};
    cx.w_max = cfg_.cx_w_max;
    cx.max_in = cfg_.cx_max_in;
    cx.a_minus = cfg_.cx_a_minus;
    cx_cx_ = std::make_unique<Projection>(*cortex_, *cortex_, 0.5f, 0.01f, cx, rng_);

    // Hipocampo: aprende RÁPIDO (poucas repetições bastam).
    PlasticityParams hp{};
    hp.w_max = cfg_.hp_w_max;
    hp.max_in = cfg_.hp_max_in;
    hp.a_minus = cfg_.hp_a_minus;  // o hipocampo tende a potenciar (LTP) com facilidade

    // córtex -> hipocampo: o hipocampo "vê" o que o córtex está pensando
    PlasticityParams cxhp = hp;
    cxhp.w_max = cfg_.cxhp_w_max;
    cxhp.max_in = cfg_.cxhp_max_in;  // orçamento fixo: cada célula se especializa
    cx_hp_ = std::make_unique<Projection>(*cortex_, *hippo_, 0.25f, cfg_.cxhp_w_init, cxhp, rng_);
    cx_hp_->set_slow_fraction(cfg_.cxhp_slow_fraction);
    if (cfg_.cxhp_slow_fraction > 0.0f) hippo_->set_slow_tau(cfg_.cxhp_slow_tau);

    // hipocampo <-> hipocampo: liga as partes de um episódio (completa padrões)
    hp_hp_ = std::make_unique<Projection>(*hippo_, *hippo_, 0.5f, 0.0f, hp, rng_);
    hp_hp_->set_delay(cfg_.hp_hp_delay);

    // Giro denteado (ver EvaConfig). Tudo fixo: o DG não aprende, só separa.
    NeuronParams dgn{};
    dgn.adapt_inc = 0.0f;  // as vencedoras do DG ficam estáveis durante a palavra
    dgn.refractory = 1;
    dg_pop_ = std::make_unique<Population>("giro denteado", std::max(1, cfg_.dg_size), dgn);
    dg_pop_->set_max_active(cfg_.dg_max_active);
    PlasticityParams fixed{};
    cx_dg_ = std::make_unique<Projection>(*cortex_, *dg_pop_, 0.25f, 1.0f, fixed, rng_);
    cx_dg_->normalize_in(cfg_.dg_cx_w);
    ca3_dg_ = std::make_unique<Projection>(*hippo_, *dg_pop_, 0.25f, 1.0f, fixed, rng_);
    ca3_dg_->normalize_in(cfg_.dg_ca3_w);
    ca3_dg_->set_slow_fraction(1.0f);  // o estado anterior persiste e pesa na escolha
    dg_pop_->set_slow_tau(cfg_.dg_ca3_tau);
    // fibras musgosas: cada célula do DG -> poucas do CA3, fortes
    dg_ca3_ = std::make_unique<Projection>(*dg_pop_, *hippo_, 0.0f, 0.0f, fixed, rng_);
    if (cfg_.dg_size > 0) {
        dg_ca3_->wire_fanout(cfg_.dg_fanout, cfg_.dg_mossy_w, rng_);
        if (cfg_.dg_modulatory) {  // o DG escolhe, mas quem dispara precisa também da entrada do córtex
            dg_ca3_->set_modulatory(true);
            hippo_->set_mod_gain(cfg_.dg_mod_gain);
        }
        cx_hp_->set_gain(cfg_.dg_cx_direct_gain);
    } else {
        cx_dg_->set_gain(0.0f); ca3_dg_->set_gain(0.0f); dg_ca3_->set_gain(0.0f);
    }

    // hipocampo -> córtex: o "índice" do episódio reacende os conceitos dele
    PlasticityParams hpcx = hp;
    hpcx.w_max = cfg_.hpcx_w_max;
    hpcx.max_in = cfg_.hpcx_max_in;
    hp_cx_ = std::make_unique<Projection>(*hippo_, *cortex_, 0.5f, 0.0f, hpcx, rng_);
    hp_cx_->set_delay(cfg_.hp_cx_delay);

    // Contexto <-> hipocampo: cada episódio tem um "carimbo" de onde/quando.
    // ctx -> hp: o hipocampo liga as palavras ao momento (separa episódios).
    // hp -> ctx: lembrar um episódio reinstala seu contexto (coerência ao recitar).
    context_ = std::make_unique<Population>("contexto", std::max(1, cfg_.context_size));
    context_->set_max_active(cfg_.context_max_active);
    PlasticityParams ctxhp = hp;
    ctxhp.w_max = cfg_.ctx_hp_w_max;
    ctxhp.max_in = cfg_.ctx_hp_max_in;
    ctx_hp_ = std::make_unique<Projection>(*context_, *hippo_, cfg_.ctx_hp_connect, cfg_.ctx_hp_w_init, ctxhp, rng_);
    PlasticityParams hpctx = hp;
    hpctx.w_max = cfg_.hp_ctx_w_max;
    hpctx.max_in = cfg_.hp_ctx_max_in;
    hp_ctx_ = std::make_unique<Projection>(*hippo_, *context_, 0.5f, 0.0f, hpctx, rng_);
    if (cfg_.ctx_mod_gain > 0.0f) {  // contexto como ganho: só amplifica quem já recebe das palavras
        ctx_hp_->set_modulatory(true);
        hippo_->set_mod_gain(cfg_.ctx_mod_gain);
    }
    if (cfg_.context_size <= 0) { ctx_hp_->set_gain(0.0f); hp_ctx_->set_gain(0.0f); }

    for (const char* k : {kEnd, kHappy, kCurious, kSurprised})
        if (find_concept(k) < 0) concepts_.push_back(k);
    const int used = static_cast<int>(concepts_.size()) * G;
    cx_cx_->set_pre_limit(used);
    cx_hp_->set_pre_limit(used);
    set_mode(Mode::Awake);
}

// ---------------------------------------------------------------- conceitos

int Eva::find_concept(const std::string& name) const {
    for (size_t i = 0; i < concepts_.size(); ++i)
        if (concepts_[i] == name) return static_cast<int>(i);
    return -1;
}

int Eva::concept_index(const std::string& name) const {
    const int i = find_concept(name);
    if (i < 0) throw std::runtime_error("Eva não conhece o conceito: " + name);
    return i;
}

int Eva::add_concept(const std::string& name) {
    if (static_cast<int>(concepts_.size()) >= cfg_.max_concepts) return -1;  // sem espaço
    concepts_.push_back(name);
    const int used = static_cast<int>(concepts_.size()) * cfg_.group_size;
    cx_cx_->set_pre_limit(used);
    cx_hp_->set_pre_limit(used);
    return static_cast<int>(concepts_.size()) - 1;
}

// ---------------------------------------------------------------- modos

void Eva::set_mode(Mode m) {
    mode_ = m;
    // Acetilcolina (teoria de Hasselmo): acordada, ACh alta suprime as conexões
    // internas do hipocampo, que fica "escutando" o mundo. No sono, ACh cai, as
    // conexões internas ganham força e as memórias se reativam sozinhas (replay).
    hp_hp_->set_gain(m == Mode::Sleep ? cfg_.sleep_recurrent_gain : 1.0f);
    cortex_->set_adapt_inc(m == Mode::Sleep ? cfg_.cx_adapt_inc_retrieval : cfg_.cx_adapt_inc);
    freeze_learning(frozen_);
}

// Modo de recuperação (ACh baixa): as conexões internas do hipocampo mandam,
// a entrada do córtex enfraquece. É o que separa "ouvir" de "lembrar".
void Eva::retrieval_mode(bool on, float recurrent_gain) {
    hp_hp_->set_gain(on ? recurrent_gain : 1.0f);
    cortex_->set_adapt_inc(on ? cfg_.cx_adapt_inc_retrieval : cfg_.cx_adapt_inc);
    const float base = cfg_.dg_size > 0 ? cfg_.dg_cx_direct_gain : 1.0f;
    cx_hp_->set_gain(base * (on ? cfg_.retrieval_cx_gain : 1.0f));
    cx_dg_->set_gain(on ? cfg_.retrieval_cx_gain : 1.0f);
    hippo_->set_max_active(on && cfg_.retrieval_max_active > 0 ? cfg_.retrieval_max_active : cfg_.hippo_max_active);
}

// Enquanto a pista é dada, o córtex entra normal; depois, só a via interna manda.
void Eva::cue_gate(bool cue_on) {
    const float base = cfg_.dg_size > 0 ? cfg_.dg_cx_direct_gain : 1.0f;
    const float g = cue_on ? 1.0f : cfg_.retrieval_cx_gain;
    cx_hp_->set_gain(base * g);
    cx_dg_->set_gain(g);
}

void Eva::freeze_learning(bool frozen) {
    frozen_ = frozen;
    if (frozen) {
        for (auto* p : {cx_cx_.get(), cx_hp_.get(), hp_hp_.get(), hp_cx_.get(), ctx_hp_.get(), hp_ctx_.get()})
            p->set_learning_rate(0.0f);
        return;
    }
    if (mode_ == Mode::Awake) {
        // Divagando, o hipocampo só reproduz: não grava os próprios devaneios
        // (senão pensamentos aleatórios virariam memórias).
        const float lr_hp = wandering_ ? 0.0f : cfg_.lr_hp_awake;
        cx_cx_->set_learning_rate(cfg_.lr_cx_awake);
        cx_hp_->set_learning_rate(lr_hp * 0.25f);
        hp_hp_->set_learning_rate(lr_hp);
        hp_cx_->set_learning_rate(lr_hp);
        ctx_hp_->set_learning_rate(lr_hp * cfg_.ctx_lr_scale);
        hp_ctx_->set_learning_rate(lr_hp);
    } else {  // no sono só o córtex aprende; o hipocampo apenas repete
        cx_cx_->set_learning_rate(cfg_.lr_cx_sleep);
        cx_hp_->set_learning_rate(0.0f);
        hp_hp_->set_learning_rate(0.0f);
        hp_cx_->set_learning_rate(0.0f);
        ctx_hp_->set_learning_rate(0.0f);
        hp_ctx_->set_learning_rate(0.0f);
    }
}

void Eva::new_episode() {
    if (cfg_.context_size <= 0) { episode_ctx_.clear(); return; }
    std::uniform_int_distribution<int> pick(0, context_->size() - 1);
    // deriva temporal: o contexto muda aos poucos (episódios próximos no tempo
    // se parecem), ou todo de uma vez (context_drift = 0)
    if (cfg_.context_drift <= 0 || episode_ctx_.empty()) episode_ctx_.clear();
    else for (int k = 0; k < cfg_.context_drift && !episode_ctx_.empty(); ++k) episode_ctx_.erase(episode_ctx_.begin());
    while (static_cast<int>(episode_ctx_.size()) < cfg_.context_active) {
        const int u = pick(rng_);
        if (std::find(episode_ctx_.begin(), episode_ctx_.end(), u) == episode_ctx_.end())
            episode_ctx_.push_back(u);
    }
}

// ---------------------------------------------------------------- um passo (1 ms)

void Eva::step(const std::vector<int>& active, float drive) {
    // 1) pulsos do passo anterior chegam ao destino
    cx_cx_->propagate();
    cx_hp_->propagate();
    hp_hp_->propagate();
    hp_cx_->propagate();
    cx_dg_->propagate();
    ca3_dg_->propagate();
    dg_ca3_->propagate();
    ctx_hp_->propagate();
    hp_ctx_->propagate();

    // 2) entrada sensorial: cada conceito ativo excita seu grupo no córtex
    //    (como trens de pulsos de Poisson vindos dos sentidos)
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    const int G = cfg_.group_size;
    const float rate = cfg_.sensory_rate * drive;  // chance de pulso por ms
    for (int c : active)
        for (int k = 0; k < G; ++k)
            if (u(rng_) < rate) cortex_->inject(c * G + k, cfg_.sensory_weight);

    //    interocepção: o que ela sente agora também entra no córtex, sempre
    if (mode_ == Mode::Awake) {
        surprise_trace_ *= std::exp(-dt_ / cfg_.surprise_tau);
        for (const char* k : {kHappy, kCurious, kSurprised}) {
            const float lvl = state_level(k);
            if (lvl <= 0.0f) continue;
            const int c = find_concept(k);
            for (int j = 0; j < G; ++j)
                if (u(rng_) < cfg_.intero_rate * lvl) cortex_->inject(c * G + j, cfg_.sensory_weight);
        }
    }

    //    o contexto do episódio atual também é "sentido" (onde/quando estou)
    if (drive > 0.0f)
        for (int unit : episode_ctx_)
            if (u(rng_) < rate) context_->inject(unit, cfg_.sensory_weight);

    // 3) ruído de fundo + inibição global (interneurônios mantêm tudo sob controle)
    cortex_->add_noise(cfg_.noise, rng_);
    hippo_->add_noise(cfg_.noise, rng_);
    context_->add_noise(cfg_.noise, rng_);
    context_->inhibit(cfg_.ctx_inhibition * static_cast<float>(context_->spikes().size()));
    cortex_->inhibit(cfg_.cx_inhibition * static_cast<float>(cortex_->spikes().size()));
    hippo_->inhibit(cfg_.hp_inhibition * static_cast<float>(hippo_->spikes().size()));

    // 4) neurônios integram e disparam
    if (cfg_.dg_size > 0) dg_pop_->step(dt_);  // sem ruído: separação determinística
    cortex_->step(dt_);
    hippo_->step(dt_);
    context_->step(dt_);

    if (recording_) {
        for (int i : hippo_->spikes()) ++rec_hp_[i];
        for (int i : context_->spikes()) ++rec_ctx_[i];
    }
    cx_spikes_ += cortex_->spikes().size();
    hp_spikes_ += hippo_->spikes().size();
    ++stat_ms_;
    clock_ms_ += dt_;

    // 5) química: dopamina decai ao basal; impulsos crescem
    dopa_.step(dt_);
    if (mode_ == Mode::Awake) drives_.step(dt_, dopa_.level, dopa_.baseline);

    // 6) aprendizado (modulado pela dopamina)
    const float d = dopa_.level;
    cx_cx_->learn(d, dt_);
    cx_hp_->learn(d, dt_);
    hp_hp_->learn(d, dt_);
    hp_cx_->learn(d, dt_);
    ctx_hp_->learn(d, dt_);
    hp_ctx_->learn(d, dt_);
}

void Eva::rest(int ms) {
    for (int t = 0; t < ms; ++t) step({}, 0.0f);
}

// ---------------------------------------------------------------- experiência

// Surpresa = quanto do que aconteceu ela NÃO previa. Mostramos o primeiro
// conceito sozinho, bem rápido e sem aprender, e medimos se os outros acendem.
// É o erro de previsão que, no cérebro, dispara os neurônios de dopamina.
float Eva::predict_surprise(const std::vector<int>& idx) {
    if (idx.size() < 2) return 0.3f;
    freeze_learning(true);
    retrieval_mode(true, cfg_.recite_gain);  // prever = recitar por dentro
    const std::vector<int> cue{idx[0]};
    std::vector<int> count(concepts_.size(), 0);
    for (int t = 0; t < cfg_.probe_ms; ++t) {
        cue_gate(t < cfg_.recite_cue_ms);
        step(cue, 1.0f);
        for (int i : cortex_->spikes()) ++count[i / cfg_.group_size];
    }
    retrieval_mode(false, 1.0f);
    rest(100);
    freeze_learning(false);

    // "Reconheci para onde isso vai": basta prever bem UMA das próximas palavras.
    // (A média sobre todas as palavras castigava frases longas: a previsão só
    // alcança um ou dois passos à frente, e toda repetição parecia nova.)
    float predicted = 0.0f;
    for (size_t k = 1; k < idx.size(); ++k)
        predicted = std::max(predicted, std::min(1.0f, count[idx[k]] / cfg_.predict_full_spikes));
    return 1.0f - predicted;
}

std::vector<int> Eva::resolve(const std::vector<std::string>& names, Experience& ex) {
    std::vector<int> idx;
    for (const auto& n : names) {
        int i = find_concept(n);
        if (i < 0) {
            i = add_concept(n);
            if (i < 0) continue;  // sem espaço para palavras novas
            ex.new_words.push_back(n);
        }
        idx.push_back(i);
        ex.concepts.push_back(n);
    }
    return idx;
}

void Eva::begin_experience(const std::vector<int>& idx, float reward, Experience& ex) {
    set_mode(Mode::Awake);
    last_ctx_ = episode_ctx_;
    episode_ctx_.clear();  // ao prever, nenhum contexto é imposto
    // surpresa: palavras novas são quase 100% inesperadas; pares conhecidos, pouco
    float surprise = predict_surprise(idx);
    if (!ex.new_words.empty()) surprise = std::max(surprise, 0.9f);
    ex.surprise = surprise;
    // dopamina: recompensa explícita + bônus de novidade
    // atenção: alguém falar com ela já vale um pouco de dopamina (engajamento),
    // o novo vale mais, e carinho (reward) vale mais ainda
    dopa_.burst(reward + cfg_.attention_bonus + cfg_.novelty_bonus * surprise * 0.5f);
    drives_.on_heard(surprise);
    surprise_trace_ = std::max(surprise_trace_, surprise);
    // Surpresa alta = fronteira de evento: um contexto novo é sorteado.
    // Surpresa baixa = "já vivi isso": o contexto não é imposto; o hipocampo
    // reinstala sozinho o contexto do episódio original (completar padrões).
    episode_ctx_ = last_ctx_;
    if (surprise >= cfg_.boundary_surprise) new_episode();
    else episode_ctx_.clear();
}

void Eva::end_experience(Experience& ex) {
    ex.dopamine = dopa_.level;
    ex.context = episode_ctx_;
    last_ctx_ = episode_ctx_;
    episode_ctx_.clear();
    rest(600);  // um instante de pausa: os neurônios se recuperam e os traços se apagam
}

// Uma cena: tudo ao mesmo tempo (ligações simétricas).
Experience Eva::experience(const std::vector<std::string>& names, float reward, int ms) {
    Experience ex;
    const std::vector<int> idx = resolve(names, ex);
    if (idx.empty()) return ex;
    begin_experience(idx, reward, ex);
    for (int t = 0; t < ms; ++t) step(idx, 1.0f);
    end_experience(ex);
    return ex;
}

// Uma frase: uma palavra por vez, com sobreposição. Como A dispara pouco antes
// de B, o STDP fortalece A -> B mais que B -> A: a rede aprende a ordem.
Experience Eva::tell(const std::vector<std::string>& words, float reward,
                     std::vector<std::vector<int>>* word_cells, std::vector<std::vector<int>>* raster,
                     std::vector<std::vector<int>>* cx_raster) {
    Experience ex;
    std::vector<int> idx = resolve(words, ex);
    if (idx.empty()) return ex;
    last_heard_ = ex.concepts;
    begin_experience(idx, reward, ex);
    idx.push_back(concept_index(kEnd));  // a frase termina: ela vive o "fim"

    const int W = cfg_.seq_word_ms, O = cfg_.seq_overlap_ms;
    const int total = static_cast<int>(idx.size()) * W + O;
    if (word_cells) word_cells->assign(idx.size(), std::vector<int>(hippo_->size(), 0));
    for (int t = 0; t < total; ++t) {
        std::vector<int> active;
        for (size_t k = 0; k < idx.size(); ++k) {
            const int start = static_cast<int>(k) * W;
            if (t >= start && t < start + W + O) active.push_back(idx[k]);
        }
        // passagem de bastão: inibição breve no CA3 quando uma palavra nova começa
        if (cfg_.encode_reset_ms > 0 && t > 0 && t % W < cfg_.encode_reset_ms)
            hippo_->inhibit(cfg_.encode_reset_inhibition);
        if (cfg_.encode_theta_ms > 0 && t % cfg_.encode_theta_ms < cfg_.encode_reset_ms)
            hippo_->inhibit(cfg_.encode_reset_inhibition);
        step(active, 1.0f);
        if (word_cells) {
            const size_t k = std::min(idx.size() - 1, static_cast<size_t>(t / W));
            for (int i : hippo_->spikes()) ++(*word_cells)[k][i];
        }
        if (raster) raster->push_back(hippo_->spikes());
        if (cx_raster) cx_raster->push_back(cortex_->spikes());
    }
    end_experience(ex);
    return ex;
}

// Leitura da ordem: o tempo é dividido em janelas curtas; em cada janela, o
// conceito mais ativo no córtex é "a palavra atual". Repetições seguidas são
// uma palavra só; quando uma palavra já dita volta, a sequência acabou (ciclo).
std::vector<int> Eva::read_sequence(const std::vector<std::vector<int>>& bins, int min_spikes) const {
    std::vector<int> seq;
    for (const auto& b : bins) {
        int best = -1, bn = 0;
        for (size_t c = 0; c < b.size(); ++c)
            if (b[c] > bn) { bn = b[c]; best = static_cast<int>(c); }
        if (best < 0 || bn < min_spikes) continue;
        if (!seq.empty() && seq.back() == best) continue;
        if (std::find(seq.begin(), seq.end(), best) != seq.end()) break;
        seq.push_back(best);
    }
    return seq;
}

std::vector<std::string> Eva::recite(const std::string& cue, int max_words) {
    std::vector<std::string> out;
    const int ic = find_concept(cue);
    if (ic < 0) return out;

    freeze_learning(true);
    rest(300);
    retrieval_mode(true, cfg_.recite_gain);  // modo "interno": o hipocampo encadeia
    std::vector<int> count(concepts_.size(), 0), onset(concepts_.size(), -1);
    int quiet = 0;
    for (int t = 0; t < cfg_.recite_max_ms; ++t) {
        if (cfg_.theta_ms > 0 && t % cfg_.theta_ms < cfg_.theta_inhib_ms)
            hippo_->inhibit(cfg_.theta_inhibition);
        // enquanto a pista é dada, o córtex entra normal; depois, só a via interna manda
        cue_gate(t < cfg_.recite_cue_ms);
        step(t < cfg_.recite_cue_ms ? std::vector<int>{ic} : std::vector<int>{}, cfg_.recite_drive);
        const auto& sp = cortex_->spikes();
        quiet = sp.empty() ? quiet + 1 : 0;
        for (int i : sp) {
            const int c = i / cfg_.group_size;
            // o conceito "entra" na sequência no instante do seu n-ésimo pulso
            if (++count[c] == cfg_.onset_spikes) onset[c] = t;
        }
        if (std::getenv("EVA_TRACE") && t < 80) {
            std::fprintf(stderr, "t%3d hp%2zu |", t, hippo_->spikes().size());
            for (size_t c = 0; c < concepts_.size(); ++c) { int n = 0; for (int i : sp) if (i / cfg_.group_size == (int)c) ++n; std::fprintf(stderr, n ? " %2d" : "  .", n); }
            std::fprintf(stderr, "\n");
        }
        if (t > cfg_.recite_cue_ms + 50 && quiet > 60) break;  // a sequência terminou
    }
    retrieval_mode(false, 1.0f);
    rest(300);
    freeze_learning(false);

    // a pista foi dada de fora: vem primeiro, sempre
    onset[ic] = -1;
    out.push_back(cue);
    std::vector<int> order;
    for (size_t c = 0; c < onset.size(); ++c)
        if (onset[c] >= 0 && count[c] >= cfg_.readout_min_spikes) order.push_back(static_cast<int>(c));
    std::sort(order.begin(), order.end(), [&](int a, int b) { return onset[a] < onset[b]; });
    if (std::getenv("EVA_DEBUG")) { for (int c : order) std::fprintf(stderr, "  %s(t=%d,n=%d)", concepts_[c].c_str(), onset[c], count[c]); std::fprintf(stderr, "\n"); }
    int last = 0;
    for (int c : order) {
        if (concepts_[c] == kEnd) break;        // chegou o "fim": a frase acabou
        if (is_internal(concepts_[c])) continue;  // estados internos não se dizem
        if (onset[c] - last > cfg_.recite_max_gap_ms) break;  // silêncio: o resto é outra memória
        last = onset[c];
        out.push_back(concepts_[c]);
        if (static_cast<int>(out.size()) >= max_words) break;
    }
    return out;
}

// ---------------------------------------------------------------- sono

void Eva::sleep(int ms) {
    set_mode(Mode::Sleep);
    // Ondas lentas: a cada 250 ms vem um "estado UP" de 60 ms em que o
    // hipocampo inteiro fica perto do limiar, com ruído. A memória com as
    // conexões internas mais fortes "acende" primeiro e se completa sozinha
    // (sharp-wave ripple / replay), reativando o córtex. A fadiga dos neurônios
    // encerra o replay, e na próxima onda outra memória pode vencer.
    for (int t = 0; t < ms; ++t, ++sleep_clock_) {
        if (sleep_clock_ % cfg_.sleep_period < cfg_.sleep_up) {
            for (int i = 0; i < hippo_->size(); ++i) hippo_->inject(i, cfg_.sleep_tonic);
            hippo_->add_noise(cfg_.sleep_noise, rng_);
        }
        step({}, 0.0f);
    }
    set_mode(Mode::Awake);
}

// ---------------------------------------------------------------- viver acordada

// Divagação: acordada e sem estímulo, o hipocampo tem pequenos "replays"
// espontâneos (a rede de modo padrão do cérebro). Cada um é um pensamento.
// Se a vontade de falar estiver alta, o pensamento vira fala. Se a curiosidade
// estiver alta, ela pergunta sobre o que menos conhece.
std::vector<Utterance> Eva::live(int ms) {
    wandering_ = true;
    set_mode(Mode::Awake);
    std::vector<Utterance> out;
    std::vector<int> thought(concepts_.size(), 0);
    std::vector<std::vector<int>> bins;
    const int window = cfg_.wander_up + 100;  // UP + eco no córtex

    for (int t = 0; t < ms; ++t, ++wander_clock_) {
        const long long phase = wander_clock_ % cfg_.wander_period;
        if (phase == 0) {
            retrieval_mode(true, cfg_.wander_gain);
            thought.assign(concepts_.size(), 0);
            bins.clear();
        }
        if (phase < cfg_.wander_up) {
            for (int i = 0; i < hippo_->size(); ++i) hippo_->inject(i, cfg_.wander_tonic);
            hippo_->add_noise(cfg_.wander_noise, rng_);
        }
        if (phase == cfg_.wander_up) retrieval_mode(false, 1.0f);

        step({}, 0.0f);

        if (phase < window) {
            if (phase % cfg_.readout_bin_ms == 0) bins.emplace_back(concepts_.size(), 0);
            for (int i : cortex_->spikes()) {
                const int c = i / cfg_.group_size;
                ++thought[c];
                ++bins.back()[c];
            }
        }

        if (phase == window) {  // fim do pensamento: decidir se fala
            std::vector<int> active;  // na ordem em que vieram
            for (int c : read_sequence(bins, cfg_.readout_min_spikes)) {
                if (is_internal(concepts_[c])) break;  // fim de frase
                if (thought[c] >= cfg_.thought_threshold) active.push_back(c);
            }
            if (active.size() > 4) active.resize(4);

            if (drives_.talk >= 1.0f && !active.empty()) {
                Utterance u{Utterance::Kind::Thought, clock_ms_, {}, active_states()};
                for (int c : active) u.words.push_back(concepts_[c]);
                out.push_back(u);
                drives_.on_spoke();
            } else if (drives_.curiosity >= 1.0f && !concepts_.empty()) {
                // curiosa: traz à tona o conceito que menos conhece e deixa a rede
                // completar o que vier. Sai como pergunta: é o que ela quer saber.
                std::string target;
                float best = 1e9f;
                for (const auto& c : concepts_) {
                    if (c == last_question_ || is_internal(c)) continue;
                    const float k = knowledge(c);
                    if (k < best) { best = k; target = c; }
                }
                if (!target.empty()) {
                    Utterance u{Utterance::Kind::Question, clock_ms_, recite(target, 4), active_states()};
                    out.push_back(u);
                    last_question_ = target;
                    drives_.on_asked();
                    drives_.on_spoke();
                }
            }
        }
    }
    retrieval_mode(false, 1.0f);
    wandering_ = false;
    freeze_learning(frozen_);
    return out;
}

// ---------------------------------------------------------------- interocepção e diálogo

float Eva::state_level(const std::string& name) const {
    if (name == kHappy) return std::clamp(dopa_.mood / 0.7f, 0.0f, 1.0f);
    if (name == kCurious) return std::clamp((drives_.curiosity - 0.5f) / 0.5f, 0.0f, 1.0f);
    if (name == kSurprised) return std::clamp(surprise_trace_, 0.0f, 1.0f);
    return 0.0f;
}

std::vector<std::string> Eva::active_states() const {
    std::vector<std::string> out;
    for (const char* k : {kHappy, kCurious, kSurprised})
        if (state_level(k) >= cfg_.state_threshold) out.push_back(k);
    return out;
}

// Recita a partir de um conjunto de pistas (conceitos). Igual a `recite`, mas as
// pistas podem ser vários conceitos, inclusive internos, e nenhuma pista é dita.
std::vector<std::string> Eva::chain_from(const std::vector<int>& cues, int max_words) {
    std::vector<std::string> out;
    freeze_learning(true);
    rest(300);
    retrieval_mode(true, cfg_.recite_gain);
    std::vector<int> count(concepts_.size(), 0), onset(concepts_.size(), -1);
    int quiet = 0;
    for (int t = 0; t < cfg_.recite_max_ms; ++t) {
        cue_gate(t < cfg_.recite_cue_ms);
        step(t < cfg_.recite_cue_ms ? cues : std::vector<int>{}, cfg_.recite_drive);
        const auto& sp = cortex_->spikes();
        quiet = sp.empty() ? quiet + 1 : 0;
        for (int i : sp) {
            const int c = i / cfg_.group_size;
            if (++count[c] == cfg_.onset_spikes) onset[c] = t;
        }
        if (t > cfg_.recite_cue_ms + 50 && quiet > 60) break;
    }
    retrieval_mode(false, 1.0f);
    rest(300);
    freeze_learning(false);

    std::vector<int> order;
    for (size_t c = 0; c < onset.size(); ++c) {
        if (onset[c] < 0 || count[c] < cfg_.readout_min_spikes) continue;
        if (std::find(cues.begin(), cues.end(), static_cast<int>(c)) != cues.end()) continue;
        order.push_back(static_cast<int>(c));
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) { return onset[a] < onset[b]; });
    int last = 0;
    for (int c : order) {
        if (concepts_[c] == kEnd) break;
        if (is_internal(concepts_[c])) continue;  // estados não se dizem, se sentem
        if (onset[c] - last > cfg_.recite_max_gap_ms) break;
        last = onset[c];
        out.push_back(concepts_[c]);
        if (static_cast<int>(out.size()) >= max_words) break;
    }
    return out;
}

// A resposta nasce do estado. Os conceitos interoceptivos ativos (e só eles)
// servem de pista; o que o hipocampo encadear a partir deles é o que ela diz.
// Se nada vier e ela quiser falar, ecoa o que acabou de ouvir.
// A resposta nasce do estado, e o estado escolhe o tipo de resposta, como em
// qualquer bicho: novidade -> imitar (ecoar o que ouviu); estar bem -> trazer o
// que viveu estando bem; curiosidade -> perguntar sobre o que menos conhece.
std::vector<Utterance> Eva::respond() {
    std::vector<Utterance> out;
    if (drives_.talk < cfg_.reply_talk) return out;  // sem vontade: silêncio
    const auto states = active_states();
    const float happy = state_level(kHappy), surprised = state_level(kSurprised);

    // 1) estou bem: o humor puxa o que foi vivido em bom humor
    if (happy >= cfg_.state_threshold && happy >= surprised) {
        auto words = chain_from({concept_index(kHappy)}, 6);
        if (!words.empty()) {
            out.push_back({Utterance::Kind::Reply, clock_ms_, words, states});
            drives_.on_spoke();
            return out;
        }
    }
    // 2) novidade (ou nada puxou): imitar. É como a fala começa.
    if (cfg_.echo_when_silent && !last_heard_.empty()) {
        auto words = recite(last_heard_.front(), 6);
        // repete a frase se a cadeia foi além da pista; se é tudo novo, repete até
        // uma palavra só (como uma criança repetindo um som que acabou de ouvir)
        if (words.size() > 1 || (words.size() == 1 && surprised >= cfg_.state_threshold)) {
            out.push_back({Utterance::Kind::Echo, clock_ms_, words, states});
            drives_.on_spoke();
        }
    }
    return out;
}

std::string Utterance::text() const {
    std::string s;
    for (size_t i = 0; i < words.size(); ++i) s += (i ? " " : "") + words[i];
    if (kind == Kind::Question) s += "?";
    return s;
}

// ---------------------------------------------------------------- lembrar e medir

RecallResult Eva::recall(const std::string& cue, bool with_hippocampus, int ms) {
    freeze_learning(true);  // lembrar não deve alterar a memória (neste teste)
    const float saved_gain = hp_cx_->gain();
    if (!with_hippocampus) hp_cx_->set_gain(0.0f);

    rest(100);
    RecallResult r{cue, std::vector<int>(concepts_.size(), 0)};
    const std::vector<int> active{concept_index(cue)};
    for (int t = 0; t < ms; ++t) {
        step(active, 1.0f);
        for (int i : cortex_->spikes()) ++r.spikes_per_concept[i / cfg_.group_size];
    }
    rest(100);

    hp_cx_->set_gain(saved_gain);
    freeze_learning(false);
    return r;
}

float Eva::cortex_rate() const {
    return stat_ms_ ? 1000.0f * cx_spikes_ / (float(stat_ms_) * cortex_->size()) : 0.0f;
}
float Eva::hippo_rate() const {
    return stat_ms_ ? 1000.0f * hp_spikes_ / (float(stat_ms_) * hippo_->size()) : 0.0f;
}

float Eva::cortical_link(const std::string& a, const std::string& b) const {
    const int G = cfg_.group_size;
    const int ia = concept_index(a), ib = concept_index(b);
    const float ab = cx_cx_->mean_weight(ia * G, (ia + 1) * G, ib * G, (ib + 1) * G);
    const float ba = cx_cx_->mean_weight(ib * G, (ib + 1) * G, ia * G, (ia + 1) * G);
    return 0.5f * (ab + ba);
}

// Quanto ela sabe sobre um conceito: ligações no córtex com os outros
// conceitos + quanto o hipocampo o indexa.
float Eva::knowledge(const std::string& c) const {
    const int G = cfg_.group_size;
    const int ic = concept_index(c);
    float k = 0.0f;
    for (const auto& o : concepts_)
        if (o != c && !is_internal(o)) k += cortical_link(c, o);
    k += 0.5f * hp_cx_->mean_weight(0, hippo_->size(), ic * G, (ic + 1) * G);
    return k;
}

}  // namespace eva
