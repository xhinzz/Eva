#include "eva2.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <map>

namespace eva2 {

static constexpr char kMemoryMagic[8] = {'E', 'V', 'A', '2', 'm', 'e', 'm', '9'};

std::string Utterance::text() const {
    std::string s;
    for (size_t i = 0; i < words.size(); ++i) s += (i ? " " : "") + words[i];
    if (kind == Kind::Question) s += "?";
    return s;
}

Eva2::Eva2(Eva2Config cfg, unsigned seed)
    : cfg_(cfg), rng_(seed),
      tm_([&] { TMConfig t = cfg.tm; t.columns = cfg.sdr_columns; return t; }(), seed),
      ctm_([&] { TMConfig t = cfg.category_tm; t.columns = cfg.categories_enabled ? cfg.categories.columns : 1; return t; }(), seed + 1),
      eyes_([&] { VisionConfig v; v.v2_units = cfg.eye_units; return v; }(), seed + 7),
      cats_(cfg.categories) {
    vocab_.columns = cfg_.sdr_columns;
    vocab_.active = cfg_.sdr_active;
    vocab_.word_active = cfg_.sdr_active;
    vocab_.category_columns = cfg_.categories.columns;
    for (const char* k : {kYou, kMe, kEnd, kHappy, kCurious, kSurprised, kWorld}) vocab_.add(k, rng_);
    // Para a área de categorias, o começo da fala dela e o da sua são a mesma
    // coisa: "começo de frase". Assim a gramática que ela aprendeu ouvindo vale
    // também quando ela fala.
    vocab_.set_category(vocab_.id(kMe), vocab_.category_code(vocab_.id(kYou)));
    drives_.talk = 0.4f;  // ela chega com alguma vontade de conversar

    // Infância dos olhos: V1 amadurece vendo as coisas do mundo (sem nomes), e
    // fecha o período crítico. V2 continua aprendendo a vida toda.
    std::vector<Image> samples;
    if (cfg_.world_enabled)  // sem mundo (só ouvindo), os olhos não fazem falta
        for (int r = 0; r < 4; ++r) for (const auto& o : world_.all_objects()) samples.push_back(draw_object(o, rng_, 1.0f));
    if (!samples.empty()) vision_.prime(samples);
    // cada unidade de V2 fala com um punhado fixo de colunas do córtex de sequências
    std::uniform_int_distribution<int> col(0, cfg_.sdr_columns - 1);
    unit_columns_.assign(vision_.config().v2_units, {});
    for (auto& cols : unit_columns_) for (int k = 0; k < 8; ++k) cols.push_back(col(rng_));
}

std::vector<int> Eva2::visual_sdr(const VisualCode& c) const {
    std::vector<int> cols;
    for (int u : c.v2_active) cols.insert(cols.end(), unit_columns_[u].begin(), unit_columns_[u].end());
    std::sort(cols.begin(), cols.end());
    cols.erase(std::unique(cols.begin(), cols.end()), cols.end());
    return cols;
}

std::vector<std::pair<std::string, float>> Eva2::visual_tokens(const VisualCode& c) const {
    std::vector<std::pair<std::string, float>> out;
    float mx = 0.0f;
    for (int u : c.v2_active) mx = std::max(mx, c.v2_activity[u]);
    for (int u : c.v2_active) out.push_back({"[v" + std::to_string(u) + "]", mx > 0 ? c.v2_activity[u] / mx : 1.0f});
    return out;
}

// ---------------------------------------------------------------- estado interno

float Eva2::state_level(const std::string& name) const {
    if (name == kHappy) return std::clamp(dopa_.mood / 0.7f, 0.0f, 1.0f);
    if (name == kCurious) return std::clamp((drives_.curiosity - 0.5f) / 0.5f, 0.0f, 1.0f);
    if (name == kSurprised) return std::clamp(surprise_trace_, 0.0f, 1.0f);
    return 0.0f;
}

std::vector<std::string> Eva2::active_states() const {
    std::vector<std::string> out;
    for (const char* k : {kHappy, kCurious, kSurprised})
        if (state_level(k) >= cfg_.state_threshold) out.push_back(k);
    return out;
}

void Eva2::pass_time(int ms) {
    for (int t = 0; t < ms; ++t) {
        dopa_.step(1.0f);
        drives_.step(1.0f, dopa_.level, dopa_.baseline);
    }
    surprise_trace_ *= std::exp(-ms / cfg_.surprise_tau);
    clock_ms_ += ms;
}

// ---------------------------------------------------------------- o fluxo

void Eva2::area2_loop() {
    std::unique_lock<std::mutex> lk(area2_m_);
    while (true) {
        area2_cv_.wait(lk, [&] { return area2_has_job_ || area2_stop_; });
        if (area2_stop_) return;
        ctm_.step(area2_cols_, area2_learn_, area2_dop_);
        area2_has_job_ = false;
        area2_cv_.notify_all();
    }
}

Eva2::~Eva2() {
    if (area2_.joinable()) {
        { std::lock_guard<std::mutex> lk(area2_m_); area2_stop_ = true; }
        area2_cv_.notify_all();
        area2_.join();
    }
}

void Eva2::step_token(int id, bool learn, float dopamine) {
    // A área de categorias só recebe língua: palavras, começo e fim de fala.
    // Estados (<feliz>), percepções ([ve:...]) e ações ({...}) não são sintaxe.
    const std::string& tok = vocab_.words[id];
    const bool language = !Vocabulary::internal(tok) || tok == kYou || tok == kMe || tok == kEnd;
    if (!cfg_.categories_enabled || !language) { tm_.step(vocab_.codes[id], learn, dopamine); return; }
    if (!area2_.joinable()) area2_ = std::thread(&Eva2::area2_loop, this);
    // Para a área de categorias, cada fala é uma frase nova (a sintaxe vive dentro
    // da frase): no começo de um turno, seu ou dela, ela recomeça, como ao ouvir.
    if (tok == kYou || tok == kMe) ctm_.reset();
    {
        std::lock_guard<std::mutex> lk(area2_m_);
        area2_cols_ = vocab_.category_code(id);
        area2_learn_ = learn;
        area2_dop_ = dopamine;
        area2_has_job_ = true;
    }
    area2_cv_.notify_all();
    tm_.step(vocab_.codes[id], learn, dopamine);
    std::unique_lock<std::mutex> lk(area2_m_);
    area2_cv_.wait(lk, [&] { return !area2_has_job_; });
}

void Eva2::reset_cortex() {
    tm_.reset();
    if (cfg_.categories_enabled) ctm_.reset();
}

void Eva2::feed(const std::string& token, bool learn, float dopamine) {
    step_token(vocab_.add(token, rng_), learn, dopamine);
}

void Eva2::feed_tokens(const std::vector<std::string>& tokens, bool learn, float dopamine) {
    for (const auto& t : tokens) feed(t, learn, dopamine);
}

void Eva2::perceive(bool learn) {
    if (!cfg_.world_enabled) return;
    feed(kWorld, learn, dopa_.level);
    for (const auto& p : world_.perceive()) feed(p, learn, dopa_.level);
    // os olhos: cada coisa visível vira um código visual, que entra no fluxo
    // como um código esparso (coisas parecidas, códigos parecidos)
    seen_now_.clear();
    for (auto& [name, img] : world_.look(rng_)) {
        VisualCode c = vision_.see(img, learn);
        tm_.step(visual_sdr(c), learn, dopa_.level);
        seen_now_.push_back({name, c});
    }
}

Heard Eva2::hear(const std::vector<std::string>& words, float reward) {
    Heard h;
    world_.pass_turn();
    actions_this_turn_ = 0;
    for (const auto& w : words)
        if (vocab_.id(w) < 0) h.new_words.push_back(w);

    // o turno inteiro, com o estado dela por dentro: <voce> [estados] palavras <fim>
    std::vector<std::string> tokens{kYou};
    for (const auto& s : active_states()) tokens.push_back(s);
    tokens.insert(tokens.end(), words.begin(), words.end());
    tokens.push_back(kEnd);

    // primeiro ela só ouve (sem gravar) para medir a surpresa de verdade:
    // quanto do que você disse ela já previa, palavra a palavra
    perceive(false);
    float total = 0.0f;
    int n = 0;
    for (size_t i = 0; i < tokens.size(); ++i) {
        const bool content = !is_internal(tokens[i]);
        const int id = vocab_.add(tokens[i], rng_);
        step_token(id, false, 1.0f);
        if (content) { total += tm_.last_surprise(); ++n; }
    }
    h.surprise = n ? total / n : 0.0f;
    if (!h.new_words.empty()) h.surprise = std::max(h.surprise, 0.9f);

    // química: atenção + novidade + carinho
    dopa_.burst(reward + cfg_.attention_bonus + cfg_.novelty_bonus * h.surprise);
    drives_.on_heard(h.surprise);
    surprise_trace_ = std::max(surprise_trace_, h.surprise);

    // agora vive de verdade, gravando, com a dopamina do momento
    reset_cortex();
    // recontextualiza com o fim da conversa recente (as últimas falas) para o
    // córtex saber "onde está" na conversa, depois grava este turno
    {
        std::vector<std::string> ctx;
        const size_t n = static_cast<size_t>(std::max(0, cfg_.context_turns));
        for (size_t k = history_.size() >= n ? history_.size() - n : 0; k < history_.size(); ++k)
            ctx.insert(ctx.end(), history_[k].tokens.begin(), history_[k].tokens.end());
        feed_tokens(ctx, false, 1.0f);
    }
    // o mundo entra antes da sua fala: o que ela vê fica ligado ao que ouve
    std::vector<std::string> world_tokens;
    if (cfg_.world_enabled) {
        world_tokens.push_back(kWorld);
        for (const auto& p : world_.perceive()) world_tokens.push_back(p);
        feed_tokens(world_tokens, true, dopa_.level);
        history_.push_back({false, world_tokens, dopa_.level});
    }
    feed_tokens(tokens, true, dopa_.level);
    observe_categories(words);
    // tema: o assunto antigo esmaece; o que você disse vira o assunto, e o que ela
    // vê também entra (o que você apontou, com força total)
    decay_topic(cfg_.topic_decay);
    for (const auto& w : words) add_topic(w, 1.0f);
    if (cfg_.world_enabled)
        for (const auto& [name, code] : seen_now_) {
            auto [w, sc] = visual_name(code);
            if (!w.empty() && sc >= cfg_.meaning_threshold) add_topic(w, name == attended_ ? 1.0f : cfg_.topic_seen);
        }
    // sentido: o que ela vê agora fica ligado ao que ouve agora, em qualquer ordem.
    // Antes de ligar, imagina: o que essas palavras já evocavam (é isso que guia a ação)
    if (cfg_.world_enabled) {
        imagined_.clear();
        for (const auto& w : words) {
            if (meaning_.frequency(w) > cfg_.function_word_freq) continue;  // "a", "o", "e"...
            for (const auto& pr : meaning_.perceptions_for(w, cfg_.meaning_threshold, cfg_.meaning_min_evidence))
                imagined_.push_back(pr);
        }
        last_perception_ = world_.perceive();
        std::vector<std::pair<std::string, float>> moment;
        for (const auto& p : last_perception_)
            moment.push_back({p, attended_.empty() ? 1.0f : cfg_.unattended_gain});  // apontou para algo: lugar/corpo pesam menos
        // o que os olhos veem: as unidades visuais ativas de cada coisa se ligam às palavras.
        // Atenção conjunta: a coisa apontada pesa muito mais; as outras, bem menos.
        for (const auto& [name, code] : seen_now_) {
            float g = 1.0f;
            if (!attended_.empty()) g = (name == attended_ ? cfg_.attention_gain : cfg_.unattended_gain);
            for (const auto& [tok, act] : visual_tokens(code)) moment.push_back({tok, g * act});
        }
        meaning_.bind_event(moment, words, dopa_.level);
        attended_.clear();
    }
    h.dopamine = dopa_.level;
    history_.push_back({false, tokens, dopa_.level});
    last_heard_ = words;
    last_heard_surprise_ = h.surprise;
    diary("voce", words, reward > 0 ? "com carinho" : "");
    pass_time(300);
    return h;
}

float Eva2::listen(const std::vector<std::string>& words, float dopamine) {
    std::vector<std::string> tokens{kYou};
    tokens.insert(tokens.end(), words.begin(), words.end());
    tokens.push_back(kEnd);
    reset_cortex();
    float surprise = 0.0f;
    for (size_t i = 0; i < tokens.size(); ++i) {
        feed(tokens[i], true, dopamine);
        if (i >= 2) surprise += tm_.last_surprise();  // a primeira palavra depois de <voce> é sempre um palpite
    }
    reset_cortex();
    observe_categories(words);
    history_.push_back({false, tokens, dopamine});
    return tokens.size() > 2 ? surprise / (tokens.size() - 2) : 0.0f;
}

// O contexto de cada palavra ouvida alimenta as categorias. Uma palavra que
// acaba de juntar evidência suficiente ganha sua primeira categoria na hora
// (como a criança que de repente usa a palavra nova em outras frases); as
// revisões ficam para o sono.
void Eva2::observe_categories(const std::vector<std::string>& words) {
    if (words.empty()) return;
    meaning_.note_turn(words);  // em quantas frases cada palavra aparece (palavras de função x de conteúdo)
    std::vector<int> ids;
    for (const auto& w : words) ids.push_back(vocab_.add(w, rng_));
    // caminhos: quem vem logo antes de quem
    if (next_counts_.size() < vocab_.words.size()) {
        next_counts_.resize(vocab_.words.size()); next_total_.resize(vocab_.words.size(), 0.0f); prev_total_.resize(vocab_.words.size(), 0.0f);
    }
    if (prev_counts_.size() < vocab_.words.size()) prev_counts_.resize(vocab_.words.size());
    for (size_t i = 0; i + 1 < ids.size(); ++i) {
        next_counts_[ids[i]][ids[i + 1]] += 1.0f; next_total_[ids[i]] += 1.0f; prev_total_[ids[i + 1]] += 1.0f;
        prev_counts_[ids[i + 1]][ids[i]] += 1.0f;
    }
    if (!cfg_.categories_enabled) return;
    cats_.observe(ids, words);
    for (int id : ids)
        if (!cats_.has_code(id) && cats_.count(id) >= cfg_.categories.min_count) {
            cats_.mark_dirty();
            if (cats_.update(id)) apply_category(id);
        }
}

void Eva2::apply_category(int id) {
    if (cats_.has_code(id)) vocab_.set_category(id, cats_.code(id));
}

// A próxima palavra é a que melhor casa com as colunas previstas. Empates e
// quase-empates: a curiosidade dá um pouco de aleatoriedade (ela explora).
int Eva2::predict_next(std::vector<int>* overlaps_out) {
    if (overlaps_out) overlaps_out->assign(vocab_.words.size(), 0);
    const bool use_grammar = cfg_.categories_enabled && grammar_in_speech_;
    auto sayable = [&](size_t k) {
        const auto& w = vocab_.words[k];
        if (w == kYou || w == kMe || w == kWorld) return false;  // marcadores não se dizem
        if (World::is_perception(w)) return false;  // percepções não se dizem (se imaginam)
        if (World::is_action(w) && !allow_actions_) return false;
        return true;
    };
    auto said_recently = [&](size_t k) {
        int rep = 0;
        for (const auto& r : recent_said_) if (r == vocab_.words[k]) ++rep;
        return rep;
    };
    // palavras cujas colunas estão previstas em `pred`
    auto memory_candidates = [&](const std::vector<int>& pred) {
        std::vector<std::pair<int, int>> cands;  // (overlap, id)
        if (pred.empty()) return cands;
        for (size_t k = 0; k < vocab_.words.size(); ++k) {
            if (!sayable(k)) continue;
            int ov = vocab_.overlap(static_cast<int>(k), pred);
            if (overlaps_out) (*overlaps_out)[k] = std::max((*overlaps_out)[k], ov);
            ov -= 6 * said_recently(k);  // saciedade: não repetir o que acabou de dizer
            if (ov >= cfg_.min_overlap) cands.push_back({ov, static_cast<int>(k)});
        }
        return cands;
    };
    const bool use_topic = cfg_.topic_enabled && topic_in_speech_ && !topic_.empty();
    auto choose = [&](std::vector<std::pair<int, int>>& cands) {
        std::sort(cands.rbegin(), cands.rend());
        // explora entre os quase-empatados conforme a curiosidade
        const int top = cands.front().first;
        int slack = static_cast<int>(std::round(6.0f * state_level(kCurious)));
        if (use_topic) slack = std::max(slack, 4);  // entre lembranças quase iguais, o assunto decide
        std::vector<int> pool;
        for (auto& [ov, id] : cands) if (ov >= top - slack) pool.push_back(id);
        if (use_topic && pool.size() > 1) {
            std::vector<double> w;
            for (int id : pool) w.push_back(1.0 + cfg_.topic_gain * topical(id));
            std::discrete_distribution<size_t> pick(w.begin(), w.end());
            return pool[pick(rng_)];
        }
        std::uniform_int_distribution<size_t> pick(0, pool.size() - 1);
        return pool[pick(rng_)];
    };

    if (!use_grammar) {
        auto cands = memory_candidates(tm_.predicted_columns());
        if (cands.empty()) return -1;
        ++gen_stats_.memory;
        return choose(cands);
    }

    const bool dbg = std::getenv("EVA2_GDEBUG") != nullptr;
    auto show = [&](const char* label, std::vector<std::pair<int, int>> c) {
        if (!dbg) return;
        std::sort(c.rbegin(), c.rend());
        std::fprintf(stderr, "    %s:", label);
        for (size_t i = 0; i < c.size() && i < 6; ++i) std::fprintf(stderr, " %s(%d)", vocab_.words[c[i].second].c_str(), c[i].first);
        std::fprintf(stderr, "\n");
    };
    if (dbg) std::fprintf(stderr, "  [prever] palavras: %zu previstas (%zu seguras), surpresa %.2f | categorias: %zu (%zu seguras), surpresa %.2f\n",
                          tm_.predicted_columns().size(), tm_.predicted_columns(true).size(), tm_.last_surprise(),
                          ctm_.predicted_columns().size(), ctm_.predicted_columns(true).size(), ctm_.last_surprise());
    auto csure = ctm_.predicted_columns(true);
    auto cpred = csure.empty() ? ctm_.predicted_columns(false) : csure;
    auto fit = [&](size_t k) {
        const auto code = vocab_.category_code(static_cast<int>(k));
        return Vocabulary::overlap_sorted(code, cpred) / static_cast<double>(std::max<size_t>(1, code.size()));
    };

    // força do apoio da gramática: quantas células do córtex de categorias
    // preveem a espécie da palavra (muitos contextos apoiando = mais forte)
    auto support = [&](size_t k) {
        const auto code = vocab_.category_code(static_cast<int>(k));
        return ctm_.predicted_cells_in(code) / static_cast<double>(std::max<size_t>(1, code.size()));
    };

    // 1. Memória segura: o contexto foi reconhecido e prevê a próxima palavra.
    //    Se a gramática também está segura e discorda (a lembrança é de outra
    //    frase, "emendada" no meio desta), a lembrança não passa.
    auto sure = memory_candidates(tm_.predicted_columns(true));
    show("seguras", sure);
    const auto sure_raw = sure;  // o que a memória de palavras lembra, antes do veto da gramática
    if (!sure.empty() && !csure.empty() && grammar_checks_memory_) {
        std::vector<std::pair<int, int>> ok;
        for (auto& c : sure) if (fit(c.second) >= cfg_.category_match) ok.push_back(c);
        gen_stats_.vetoed += static_cast<long long>(sure.size() - ok.size());
        sure = ok;
    }
    // 0. Intenção: a palavra do assunto ainda não dita, ou o caminho até ela
    //    ("as" antes de "moças"), entra se a gramática está segura de que essa
    //    espécie de palavra cabe aqui. Querer falar do porco vence a lembrança
    //    de "os doces". Uma vez dita a palavra do assunto, o resto da frase segue
    //    a memória e a gramática (senão ela só repetiria você).
    if (dbg && use_topic) {
        std::fprintf(stderr, "    tema:");
        for (auto& [w, t] : topic(5)) std::fprintf(stderr, " %s(%.2f)", w.c_str(), t);
        std::fprintf(stderr, " | ja disse o assunto: %d | gramatica segura: %zu colunas\n", topic_said_, csure.size());
    }
    const auto call = ctm_.predicted_columns(false);
    auto fits_now = [&](size_t k) {
        const auto code = vocab_.category_code(static_cast<int>(k));
        return Vocabulary::overlap_sorted(code, call) / static_cast<double>(std::max<size_t>(1, code.size())) >= cfg_.topic_fit;
    };
    // P. Plano: ela sabe o que quer dizer e em que ordem (descrever uma cena,
    //    responder uma pergunta). O próximo item do plano entra se a gramática
    //    deixa; senão, o caminho até ele (as palavras que costumam vir logo antes,
    //    ou duas antes: "está na" antes de "sala"); quando o plano acabou, o fim.
    if (plan_active_ && !call.empty()) {
        const int end_id = vocab_.id(kEnd);
        if (plan_pos_ >= plan_.size()) {
            if (end_id >= 0 && fit(end_id) >= cfg_.category_match) return end_id;
        } else {
            const auto& item = plan_[plan_pos_];
            double best_sup = 0.0;
            for (size_t k = 0; k < vocab_.words.size(); ++k) if (sayable(k) && fits_now(k)) best_sup = std::max(best_sup, support(k));
            std::vector<int> ids; std::vector<double> w;
            // o que ela quer dizer só precisa caber; não precisa ser a espécie mais apoiada
            for (int k : item)
                if (sayable(k) && !said_recently(k) && fit(k) >= cfg_.plan_fit && support(k) >= 0.15 * best_sup) { ids.push_back(k); w.push_back(support(k)); }
            // a memória de palavras lembra exatamente isto aqui ("o papai" -> "comeu"),
            // mesmo que a espécie da palavra rara ainda seja mal prevista: vale
            if (ids.empty())
                for (const auto& [ov, k] : sure_raw)
                    if (std::find(item.begin(), item.end(), k) != item.end() && !said_recently(k)) { ids.push_back(k); w.push_back(ov); }
            // a gramática não tem opinião firme aqui (nada cabe bem): a intenção decide
            if (ids.empty() && best_sup < 0.5)
                for (int k : item) if (sayable(k) && !said_recently(k) && fit(k) >= 0.15) { ids.push_back(k); w.push_back(0.1 + support(k)); }
            if (std::getenv("EVA2_GDEBUG")) {
                std::fprintf(stderr, "    plano item %zu/%zu:", plan_pos_ + 1, plan_.size());
                for (int k : item) std::fprintf(stderr, " %s(cabe=%.2f,apoio=%.2f)", vocab_.words[k].c_str(), fit(k), support(k));
                std::fprintf(stderr, " | melhor apoio %.2f\n", best_sup);
            }
            if (!ids.empty()) {
                std::discrete_distribution<size_t> pick(w.begin(), w.end());
                ++gen_stats_.topic;
                return ids[pick(rng_)];
            }
            // Palavra rara, ainda sem categoria: a gramática não tem opinião sobre ela.
            // Se a palavra anterior é o caminho usual até ela ("o" antes de "pudim"),
            // a intenção basta.
            if (!recent_said_.empty()) {
                const int prev = vocab_.id(recent_said_.back());
                int pick_k = -1; double bp1 = 0.2;
                for (int t : item) {
                    if (cats_.has_code(t) || said_recently(t) || t >= static_cast<int>(prev_counts_.size()) || prev_total_[t] <= 0) continue;
                    auto it = prev_counts_[t].find(prev);
                    const double p1 = it == prev_counts_[t].end() ? 0.0 : it->second / prev_total_[t];
                    if (p1 > bp1) { bp1 = p1; pick_k = t; }
                }
                if (pick_k >= 0) { ++gen_stats_.topic; return pick_k; }
            }
            // caminho: P(k vem logo antes de t) e P(k vem duas antes de t)
            std::unordered_map<int, double> path;
            for (int t : item) {
                if (t >= static_cast<int>(prev_counts_.size()) || prev_total_[t] <= 0) continue;
                for (const auto& [m, c1] : prev_counts_[t]) {
                    const double p1 = c1 / prev_total_[t];
                    path[m] = std::max(path[m], p1);
                    if (m >= static_cast<int>(prev_counts_.size()) || prev_total_[m] <= 0) continue;
                    for (const auto& [k, c2] : prev_counts_[m]) path[k] = std::max(path[k], 0.7 * p1 * (c2 / prev_total_[m]));
                }
            }
            int best = -1; double bp = 0.0;
            for (const auto& [k, p] : path) {
                if (p < 0.05) continue;
                if (k >= static_cast<int>(vocab_.words.size()) || !sayable(k) || !fits_now(k)) continue;
                // "a" pode vir duas vezes na mesma frase ("a girafa come a gelatina");
                // só palavras de conteúdo não se repetem
                if (said_recently(k) && meaning_.frequency(vocab_.words[k]) <= cfg_.topic_function_freq) continue;
                if (support(k) < 0.15 * best_sup) continue;
                // o caminho usual, pesado pelo quanto a gramática apoia aquela palavra aqui
                // ("gostam das zebras": "das" é menos usual antes de "zebras" que "as", mas muito mais apoiado)
                const double sc = p * support(k);
                if (sc > bp) { bp = sc; best = k; }
            }
            if (std::getenv("EVA2_GDEBUG")) {
                std::vector<std::pair<double, int>> top;
                for (const auto& [k, p] : path) if (k < static_cast<int>(vocab_.words.size())) top.push_back({p, k});
                std::sort(top.rbegin(), top.rend());
                std::fprintf(stderr, "    caminho:");
                for (size_t i = 0; i < top.size() && i < 6; ++i) std::fprintf(stderr, " %s(%.2f,cabe=%d,apoio=%.2f)", vocab_.words[top[i].second].c_str(), top[i].first, (int)fits_now(top[i].second), support(top[i].second));
                std::fprintf(stderr, " -> %s\n", best >= 0 ? vocab_.words[best].c_str() : "(nada)");
            }
            if (best >= 0) { ++gen_stats_.topic; return best; }
        }
    }
    if (use_topic && !topic_said_ && !call.empty() && !plan_active_) {
        // a gramática inteira (não só a "segura"): no começo da fala, a parte segura
        // é só o hábito dela ("os ..."), e o hábito não pode calar o assunto
        auto sure_fit = [&](size_t k) {
            const auto code = vocab_.category_code(static_cast<int>(k));
            return Vocabulary::overlap_sorted(code, call) / static_cast<double>(std::max<size_t>(1, code.size()));
        };
        std::vector<int> ids;
        std::vector<double> w, sup;
        double best_sup = 0.0;
        for (size_t k = 0; k < vocab_.words.size(); ++k) {
            if (!sayable(k) || said_recently(k)) continue;
            const float t = topical(static_cast<int>(k));
            if (t < cfg_.topic_intent || sure_fit(k) < cfg_.topic_fit) continue;
            const double sp = support(k);
            ids.push_back(static_cast<int>(k));
            w.push_back(std::pow(t, cfg_.topic_sharp) * sp);
            sup.push_back(sp);
            best_sup = std::max(best_sup, sp);
        }
        // só o que a gramática apoia com força: metade do apoio da espécie mais
        // apoiada aqui, entre TODAS as palavras (não só as do assunto). Depois de
        // "os", substantivos têm apoio enorme; um verbo que entrou na previsão
        // por acaso, não.
        if (!ids.empty())
            for (size_t k = 0; k < vocab_.words.size(); ++k)
                if (sayable(k) && sure_fit(k) >= cfg_.topic_fit) best_sup = std::max(best_sup, support(k));
        for (size_t i = 0; i < ids.size(); ++i) if (sup[i] < 0.5 * best_sup) w[i] = 0.0;
        if (dbg) {
            std::fprintf(stderr, "    intencao:");
            for (size_t i = 0; i < ids.size(); ++i) {
                std::fprintf(stderr, " %s(t=%.2f,apoio=%.2f%s; via", vocab_.words[ids[i]].c_str(), topical(ids[i]), sup[i], w[i] > 0 ? "" : ",fraco");
                if (ids[i] < static_cast<int>(next_counts_.size()))
                    for (const auto& [t, c] : next_counts_[ids[i]])
                        if (t < static_cast<int>(topic_.size()) && topic_[t] > 0.01f && prev_total_[t] > 0.0f)
                            std::fprintf(stderr, " %s:%.0f/%.0f", vocab_.words[t].c_str(), c, prev_total_[t]);
                std::fprintf(stderr, ")");
            }
            std::fprintf(stderr, "\n");
        }
        double total = 0.0;
        for (double x : w) total += x;
        if (total > 0.0) {
            std::discrete_distribution<size_t> pick(w.begin(), w.end());
            ++gen_stats_.topic;
            return ids[pick(rng_)];
        }
    }
    if (!sure.empty()) { ++gen_stats_.memory; return choose(sure); }

    // 2. A memória só tem chutes (a união de tudo que já veio depois da última
    //    palavra, que chegou de surpresa). A gramática (córtex de categorias)
    //    diz que espécie de palavra cabe agora; dos chutes, ficam os que cabem.
    auto guesses = memory_candidates(tm_.predicted_columns(false));
    if (cpred.empty()) {
        if (guesses.empty()) return -1;
        ++gen_stats_.memory;
        return choose(guesses);
    }
    std::vector<std::pair<int, int>> kept;
    for (auto& c : guesses) if (fit(c.second) >= cfg_.category_match) kept.push_back(c);
    if (!kept.empty()) {
        // a lembrança só vence se a gramática a apoia quase tanto quanto apoia a
        // melhor alternativa: "os cachorros" + fim é possível na gramática dela,
        // mas muito menos apoiado que "os cachorros" + verbo
        double best_kept = 0.0, best_any = 0.0;
        for (auto& c : kept) best_kept = std::max(best_kept, support(c.second));
        for (size_t k = 0; k < vocab_.words.size(); ++k)
            if (sayable(k) && !said_recently(k) && fit(k) >= cfg_.category_match) best_any = std::max(best_any, support(k));
        if (dbg) std::fprintf(stderr, "    apoio: melhor chute %.2f, melhor da gramatica %.2f\n", best_kept, best_any);
        if (best_any > cfg_.grammar_override * best_kept) { gen_stats_.vetoed += static_cast<long long>(kept.size()); kept.clear(); }
    }
    show("chutes", guesses);
    if (dbg) {
        std::vector<std::pair<int, int>> fits;
        for (size_t k = 0; k < vocab_.words.size(); ++k) if (sayable(k)) fits.push_back({static_cast<int>(100 * fit(k)), static_cast<int>(k)});
        show("gramatica(%)", fits);
        std::vector<std::pair<int, int>> sup;
        for (size_t k = 0; k < vocab_.words.size(); ++k) if (sayable(k) && fit(k) >= cfg_.category_match) sup.push_back({static_cast<int>(100 * support(k)), static_cast<int>(k)});
        show("apoio(x100)", sup);
    }
    gen_stats_.vetoed += static_cast<long long>(guesses.size() - kept.size());
    if (!kept.empty()) { ++gen_stats_.memory; return choose(kept); }

    // 3. Nenhuma lembrança serve: a gramática propõe. Qualquer palavra da espécie
    //    prevista serve; as mais ouvidas vêm mais fácil à boca.
    std::vector<int> ids;
    std::vector<double> weight;
    for (size_t k = 0; k < vocab_.words.size(); ++k) {
        if (!sayable(k) || said_recently(k)) continue;
        const double f = fit(k);
        if (f < cfg_.category_match) continue;
        ids.push_back(static_cast<int>(k));
        double wk = std::pow(f, 4.0) * std::pow(support(k), 2.0) * std::sqrt(1.0 + cats_.count(static_cast<int>(k)));
        if (use_topic) wk *= 1.0 + cfg_.topic_gain * topical(static_cast<int>(k));  // o assunto escolhe dentro da espécie
        weight.push_back(wk);
    }
    if (ids.empty()) return -1;
    std::discrete_distribution<size_t> pick(weight.begin(), weight.end());
    ++gen_stats_.grammar;
    return ids[pick(rng_)];
}

std::vector<std::string> Eva2::generate(int max_words, bool learn) {
    std::vector<std::string> out;
    recent_said_.clear();
    topic_said_ = false;
    int last_id = -1;
    bool burst_used = false;
    for (int i = 0; i < max_words; ++i) {
        int id = predict_next();
        if (id < 0 && last_id >= 0 && !burst_used) {
            // O contexto exato não prevê nada. No córtex real, a coluna "explode"
            // (todas as células) e a previsão passa a ser a continuação mais comum
            // da palavra, em qualquer contexto. É como a gente conclui uma frase
            // começada: pelo caminho mais batido.
            reset_cortex();
            step_token(last_id, false, 1.0f);
            id = predict_next();
            burst_used = true;
        }
        if (id < 0) break;
        const std::string& w = vocab_.words[id];
        if (w == kEnd) break;
        if (World::is_action(w)) { pending_action_ = w; break; }  // o corpo responde, não a boca
        if (!is_internal(w)) {
            out.push_back(w); recent_said_.push_back(w); burst_used = false;
            if (id < static_cast<int>(topic_.size()) && topic_[id] >= cfg_.topic_intent) topic_said_ = true;
            if (plan_active_ && plan_pos_ < plan_.size() &&
                std::find(plan_[plan_pos_].begin(), plan_[plan_pos_].end(), id) != plan_[plan_pos_].end()) ++plan_pos_;
        }
        step_token(id, learn, cfg_.self_learning * dopa_.level);
        last_id = id;
    }
    return out;
}

std::vector<Utterance> Eva2::respond() {
    std::vector<Utterance> out;
    if (drives_.talk < cfg_.reply_talk) return out;
    const auto states = active_states();

    // abre o turno dela: <eva> [estados]. O que o córtex prever a seguir é a resposta.
    std::vector<std::string> head{kMe};
    for (const auto& s : states) head.push_back(s);
    feed_tokens(head, true, dopa_.level);

    std::vector<std::string> words;
    Utterance::Kind kind = Utterance::Kind::Reply;
    const bool novel = last_heard_surprise_ >= cfg_.imitate_above && !last_heard_.empty();

    auto is_repeat = [&](const std::vector<std::string>& w) {
        for (const auto& r : recent_replies_) if (r == w) return true;
        return false;
    };

    // uma pergunta que ela sabe responder: a resposta vem da memória, com plano
    bool answered = false;
    if (!last_heard_.empty()) {
        auto plan = answer_plan(last_heard_);
        if (!plan.empty()) {
            plan_ = plan; plan_pos_ = 0; plan_active_ = true;
            words = generate(cfg_.max_reply_words, true);
            plan_active_ = false; plan_.clear();
            answered = !words.empty();
        }
    }
    if (!answered && !novel) {
        words = generate(cfg_.max_reply_words, true);
        // habituação: se ia dizer exatamente o que acabou de dizer, tenta outro caminho
        if (!words.empty() && is_repeat(words)) {
            recent_said_ = words;  // penaliza essas palavras na próxima tentativa
            reset_cortex();
            feed_tokens(head, false, 1.0f);
            auto alt = generate(cfg_.max_reply_words, true);
            words = is_repeat(alt) ? std::vector<std::string>{} : alt;
        }
    }
    if (!answered && words.empty() && !last_heard_.empty()) {
        // novidade (ou nada a dizer): imitar. A primeira palavra que você disse
        // entra no turno dela e o córtex segue dali, como uma criança repetindo.
        reset_cortex();
        feed_tokens(head, false, 1.0f);
        feed(last_heard_.front(), true, cfg_.self_learning * dopa_.level);
        words = generate(cfg_.max_reply_words - 1, true);
        words.insert(words.begin(), last_heard_.front());
        kind = Utterance::Kind::Echo;
        if (is_repeat(words) && !novel) words.clear();
    }
    if (words.empty() && !last_heard_.empty()) {
        // associação livre: o que você disse por último puxa o que vem depois
        // dele na memória dela (é assim que a gente "pega o gancho")
        for (int k = static_cast<int>(last_heard_.size()) - 1; k >= 0 && words.empty(); --k) {
            reset_cortex();
            feed_tokens(head, false, 1.0f);
            step_token(vocab_.id(last_heard_[k]), false, 1.0f);
            auto cont = generate(cfg_.max_reply_words, false);
            if (cont.empty() || is_repeat(cont)) continue;
            // diz a partir do gancho, gravando como fala dela
            reset_cortex();
            feed_tokens(head, false, 1.0f);
            feed(last_heard_[k], true, cfg_.self_learning * dopa_.level);
            words = generate(cfg_.max_reply_words - 1, true);
            words.insert(words.begin(), last_heard_[k]);
            kind = Utterance::Kind::Reply;
        }
    }
    if (words.empty()) {
        reset_cortex();
        return out;  // silêncio: não tem nada novo a dizer
    }
    feed(kEnd, true, cfg_.self_learning * dopa_.level);
    std::vector<std::string> tokens = head;
    tokens.insert(tokens.end(), words.begin(), words.end());
    tokens.push_back(kEnd);
    history_.push_back({true, tokens, dopa_.level});
    recent_replies_.push_back(words);
    if (recent_replies_.size() > 3) recent_replies_.erase(recent_replies_.begin());
    out.push_back({kind, words, states, 0.0f});
    drives_.on_spoke();
    for (const auto& w : words) add_topic(w, cfg_.topic_self);
    diary("eva", words, kind == Utterance::Kind::Echo ? "eco" : "resposta");
    return out;
}

// ---------------------------------------------------------------- agir no mundo

// O corpo dela: percebe, e se o córtex prevê uma ação, faz. Se não prevê
// nada, explora (balbucio motor), mais quando está curiosa ou com fome.
// A recompensa do corpo (comer com fome, dormir cansada) vira dopamina, e a
// dopamina escala o aprendizado da ligação percepção -> ação. Três fatores.
std::vector<Action> Eva2::act() {
    std::vector<Action> out;
    if (!cfg_.world_enabled) return out;

    // Compreensão: o que você acabou de dizer evoca percepções. Se evoca um
    // lugar onde ela não está, ela vai; se evoca algo que dá para pegar, pega.
    // É entender uma frase como um bebê entende "vamos à praia": a imagem vem
    // antes da gramática.
    if (!last_heard_.empty()) {
        std::string best_p; float bs = 0.0f;
        const auto& all = imagined_;
        if (std::getenv("EVA2_DEBUG")) { std::fprintf(stderr, "[imaginou %zu:", all.size()); for (auto& [p, sc] : all) std::fprintf(stderr, " %s(%.2f)", p.c_str(), sc); std::fprintf(stderr, "]\n"); }
        for (const auto& [p, sc] : all)
            if (sc > bs) { bs = sc; best_p = p; }
        // empate entre o que se vê junto (o mar, a areia, o lugar): a frase pede
        // movimento quando evoca um lugar quase tão forte quanto o resto
        for (const auto& [p, sc] : all)
            if (p.rfind("[esta:", 0) == 0 && sc >= 0.9f * bs && World::label(p) != world_.here) { best_p = p; break; }
        std::string action;
        if (best_p.rfind("[esta:", 0) == 0) {
            goal_place_ = World::label(best_p);  // intenção: fica até chegar
        } else if (best_p.rfind("[v", 0) == 0) {
            // evocou uma imagem: qual coisa à vista se parece mais com o que foi evocado?
            std::string obj; float bm = 0.0f;
            for (const auto& [name, code] : seen_now_) {
                float m = 0.0f;
                for (const auto& [tok, act] : visual_tokens(code))
                    for (const auto& [p, sc] : all) if (p == tok) m += sc * act;
                if (m > bm) { bm = m; obj = name; }
            }
            if (!obj.empty() && world_.holding != obj) action = "{pegar:" + obj + "}";
        } else if (best_p.rfind("[tem:", 0) == 0) {
            const std::string obj = World::label(best_p);
            if (world_.holding != obj) action = "{pegar:" + obj + "}";
        }
        bool possible = false;
        for (const auto& a : world_.possible_actions()) if (a == action) possible = true;
        if (std::getenv("EVA2_DEBUG")) std::fprintf(stderr, "[melhor %s -> acao '%s' possivel %d, aqui %s, segurando '%s']\n", best_p.c_str(), action.c_str(), possible, world_.here.c_str(), world_.holding.c_str());
        if (possible) {
            std::string result;
            const float reward = world_.act(action, &result) + 0.3f;  // fazer o que foi pedido é bom
            dopa_.burst(reward);
            reset_cortex(); perceive(true); feed(action, true, dopa_.level);
            out.push_back({action, result + " (entendeu)", reward});
            diary("eva", {action}, result + " (entendeu)");
            last_heard_.clear();
            reset_cortex();
            return out;  // fez o que foi pedido: não desfaz em seguida explorando
        }
        last_heard_.clear();
    }
    // intenção em curso: anda um passo por vez até o lugar que entendeu
    if (!goal_place_.empty()) {
        if (goal_place_ == world_.here) {
            dopa_.burst(0.5f);  // chegou onde foi pedido
            goal_place_.clear();
        } else {
            const std::string step = world_.next_step_towards(goal_place_);
            if (step.empty()) goal_place_.clear();
            else {
                const std::string action = "{ir:" + step + "}";
                std::string result;
                const float reward = world_.act(action, &result);
                reset_cortex(); perceive(true); feed(action, true, dopa_.level);
                out.push_back({action, result + " (indo para " + goal_place_ + ")", reward});
                diary("eva", {action}, result);
                if (goal_place_ == world_.here) { dopa_.burst(0.5f); goal_place_.clear(); }
                return out;
            }
        }
    }

    for (int k = 0; k < cfg_.max_actions_per_turn; ++k) {
        reset_cortex();
        perceive(false);
        allow_actions_ = true;
        pending_action_.clear();
        std::vector<int> ov;
        const int id = predict_next(&ov);
        allow_actions_ = false;
        std::string action;
        if (id >= 0 && World::is_action(vocab_.words[id])) action = vocab_.words[id];
        const auto possible = world_.possible_actions();
        // só vale ação possível aqui; senão, reflexos inatos; senão, explorar
        bool ok = false;
        for (const auto& a : possible) if (a == action) ok = true;
        if (!ok) {
            // Reflexos (ninguém aprende a mamar): com fome, come o que tem na mão ou
            // pega comida que vê; cansada no quarto, dorme. O aprendizado vem depois,
            // porque a recompensa grava "fome + cozinha -> pegar pão" no córtex.
            std::string reflex;
            if (world_.hunger >= 0.6f) {
                for (const auto& e : world_.edible) if (world_.holding == e) reflex = "{comer}";
                if (reflex.empty())
                    for (const auto& o : world_.places.at(world_.here).objects)
                        for (const auto& e : world_.edible) if (o == e && reflex.empty()) reflex = "{pegar:" + o + "}";
            }
            if (reflex.empty() && world_.tiredness >= 0.7f && world_.here == "quarto") reflex = "{dormir}";
            for (const auto& a : possible) if (a == reflex) { action = reflex; ok = true; }
        }
        if (!ok) {
            const float need = std::max(world_.hunger, world_.tiredness);
            const float p_explore = 0.1f + 0.3f * state_level(kCurious) + 0.3f * std::max(0.0f, need - 0.6f);
            std::uniform_real_distribution<float> u(0.0f, 1.0f);
            if (u(rng_) > p_explore) break;
            std::uniform_int_distribution<size_t> pick(0, possible.size() - 1);
            action = possible[pick(rng_)];
            k = cfg_.max_actions_per_turn;  // uma exploração por vez
        }
        std::string result;
        const float reward = world_.act(action, &result);
        dopa_.burst(reward);
        // grava: percepção (como estava) -> ação, com a dopamina que a ação rendeu
        reset_cortex();
        perceive(true);
        feed(action, true, dopa_.level);
        std::vector<std::string> tokens{kWorld};
        for (const auto& p : world_.perceive()) tokens.push_back(p);
        tokens.push_back(action);
        history_.push_back({true, tokens, dopa_.level});
        out.push_back({action, result, reward});
        diary("eva", {action}, result);
        if (reward >= 0.5f) break;  // satisfeita: para
    }
    reset_cortex();
    return out;
}

bool Eva2::take_to(const std::string& place, std::string* result) {
    if (!world_.places.count(place)) { if (result) *result = "esse lugar nao existe"; return false; }
    world_.here = place;
    if (result) *result = "voce levou a Eva para " + place;
    return true;
}

bool Eva2::point_at(const std::string& object, std::string* result) {
    bool visible = false;
    for (const auto& o : world_.places.at(world_.here).objects) if (o == object) visible = true;
    if (world_.holding == object) visible = true;
    if (!visible) { if (result) *result = "ela nao ve " + object + " daqui"; return false; }
    attended_ = object;
    if (result) *result = "voce apontou para " + object + "; ela olhou";
    return true;
}

bool Eva2::give(const std::string& object, std::string* result) {
    world_.holding = object;
    if (result) *result = "voce deu " + object + " para a Eva";
    return true;
}

// Sentido, testado direto: vendo só isto, o que ela diria?
// A palavra vem do córtex associativo (a soma, sobre as unidades visuais ativas,
// do quanto cada uma evoca a palavra); o resto da frase vem do córtex de
// sequências, a partir dela. Uma imagem nunca vista é nomeada pelo que ela
// compartilha com imagens vistas: é generalização de verdade.
// ---------------------------------------------------------------- cenas, episódios, papéis, perguntas

std::string Eva2::meaning_of(const std::string& word) const {
    if (Vocabulary::internal(word)) return {};
    // Uma exposição basta para um nome novo: numa cena com o gato (já conhecido)
    // e algo novo, a palavra nova se liga ao novo, porque a força é normalizada
    // pelo quanto cada percepção já tem nome (exclusividade mútua, de graça).
    // E a palavra precisa ser específica do sentido (vir quase só com ele): "o"
    // vem com tudo e não nomeia nada; "comeu", rara, só vem com comer.
    for (const auto& [p, sc] : meaning_.perceptions_for(word, 0.1f, cfg_.fast_mapping_evidence)) {  // a mais forte primeiro
        if (p.rfind("[ve:", 0) != 0 && p.rfind("[lugar:", 0) != 0 && p.rfind("[acao:", 0) != 0) continue;
        if (meaning_.specificity(p, word) >= 0.5f) return p;
    }
    return {};
}

// as palavras da frase que têm sentido para ela, na ordem: (posição entre elas, sentido)
std::vector<std::pair<int, std::string>> Eva2::content_slots(const std::vector<std::string>& words) const {
    std::vector<std::pair<int, std::string>> out;
    for (const auto& w : words) {
        const std::string m = meaning_of(w);
        if (m.empty()) continue;
        bool dup = false;
        for (const auto& [k, mm] : out) if (mm == m) dup = true;
        if (!dup) out.push_back({static_cast<int>(out.size()), m});
    }
    return out;
}

// As palavras que nomeiam um sentido ("gato", "gatos" para [ve:gato]). Só as
// quase tão específicas dele quanto a mais específica: "o" e "a" vêm com tudo
// e não nomeiam nada; "comeu", mesmo rara, só vem com comer.
std::vector<int> Eva2::forms_of(const std::string& meaning) const {
    std::vector<std::pair<int, float>> cand;
    float best = 0.0f;
    for (size_t k = 0; k < vocab_.words.size(); ++k) {
        if (meaning_of(vocab_.words[k]) != meaning) continue;
        const float st = meaning_.specificity(meaning, vocab_.words[k]);
        cand.push_back({static_cast<int>(k), st});
        best = std::max(best, st);
    }
    std::vector<int> out;
    for (const auto& [k, st] : cand) if (st >= 0.5f * best) out.push_back(k);
    return out;
}

void Eva2::rebuild_prev_counts() {
    prev_counts_.assign(next_counts_.size(), {});
    for (size_t k = 0; k < next_counts_.size(); ++k)
        for (const auto& [t, c] : next_counts_[k]) if (t < static_cast<int>(prev_counts_.size())) prev_counts_[t][static_cast<int>(k)] = c;
}

// O que ela percebe de uma cena: quem e o quê, onde, quantos (um ou muitos) e quando
// (acontecendo agora, ou lembrado depois)
static std::vector<std::pair<std::string, float>> scene_percepts(const Scene& s, bool past) {
    std::vector<std::pair<std::string, float>> p;
    if (!s.agent.empty()) { p.push_back({"[ve:" + s.agent + "]", 1.0f}); p.push_back({s.agent_many ? "[muitos]" : "[um]", 1.0f}); }
    if (!s.patient.empty()) { p.push_back({"[ve:" + s.patient + "]", 1.0f}); p.push_back({s.patient_many ? "[muitos]" : "[um]", 1.0f}); }
    if (!s.place.empty()) p.push_back({"[lugar:" + s.place + "]", 1.0f});
    if (!s.action.empty()) p.push_back({"[acao:" + s.action + "]", 1.0f});
    p.push_back({past ? "[antes]" : "[agora]", 1.0f});
    return p;
}

static int role_in(const Scene& s, const std::string& m) {
    if (m == "[ve:" + s.agent + "]") return kAgent;
    if (!s.patient.empty() && m == "[ve:" + s.patient + "]") return kPatient;
    if (m == "[lugar:" + s.place + "]") return kPlace;
    if (m == "[acao:" + s.action + "]") return kAction;
    return -1;
}

void Eva2::witness(const Scene& scene, const std::vector<std::string>& words, float dopamine) {
    listen(words, dopamine);
    meaning_.bind_event(scene_percepts(scene, false), words, dopamine);
    // papéis: em que posição (entre as palavras com sentido) vem cada papel da cena
    for (const auto& [slot, m] : content_slots(words)) {
        const int r = role_in(scene, m);
        if (r >= 0) roles_.bind(slot, r);
    }
    if (cfg_.episodes_enabled) episodes_.store(scene, clock_ms_, dopamine);
    clock_ms_ += 3000;
}

void Eva2::listen_turns(const std::vector<std::vector<std::string>>& turns, float dopamine) {
    // uma troca inteira, sem recomeçar no meio: a pergunta e a resposta ficam ligadas
    reset_cortex();
    bool first = true;
    for (const auto& t : turns) {
        std::vector<std::string> tokens{first ? kYou : kMe};
        tokens.insert(tokens.end(), t.begin(), t.end());
        tokens.push_back(kEnd);
        feed_tokens(tokens, true, dopamine);
        observe_categories(t);
        history_.push_back({!first, tokens, dopamine});
        first = false;
    }
    reset_cortex();
}

void Eva2::recount(const Scene& scene, const std::vector<std::string>& words) {
    listen(words, 0.6f);
    meaning_.bind_event(scene_percepts(scene, true), words, 0.6f);
}

void Eva2::witness_dialogue(const Scene& scene, const std::vector<std::string>& question, const std::vector<std::string>& answer, bool past) {
    // quem cuida pergunta e responde por ela ("cadê o gato? ... o gato está na sala!"):
    // a resposta entra no turno dela, como andaime
    listen_turns({question, answer}, 0.7f);
    meaning_.bind_event(scene_percepts(scene, past), answer, 0.7f);
    meaning_.bind_event({{past ? "[antes]" : "[agora]", 1.0f}}, question, 0.7f);
    // o que a resposta trouxe que a pergunta não tinha
    std::vector<std::string> asked;
    for (const auto& [k, m] : content_slots(question)) asked.push_back(m);
    int brought = -1;
    for (const auto& [k, m] : content_slots(answer)) {
        if (std::find(asked.begin(), asked.end(), m) != asked.end()) continue;
        const int r = role_in(scene, m);
        if (r == kAgent || r == kPatient || r == kPlace) { brought = r; break; }
    }
    if (brought < 0) return;
    // cada palavra da pergunta sem sentido próprio ("cadê", "quem", "que", "o") fica ligada ao que foi pedido
    for (const auto& w : question) if (meaning_of(w).empty()) qwords_.bind(w, brought);
}

// Das formas de um sentido, as que combinam com quantos são e com quando foi.
// Nada disso é regra: "gatos" e "comem" vieram junto com ver muitos; "comeu"
// veio junto com lembrar. Compara-se cada forma com as irmãs dela.
std::vector<int> Eva2::choose_forms(const std::vector<int>& forms, bool many, bool past) const {
    if (forms.size() <= 1) return forms;
    std::vector<double> score(forms.size(), 0.0);
    double best = -1e9;
    for (size_t i = 0; i < forms.size(); ++i) {
        const std::string& w = vocab_.words[forms[i]];
        if (number_in_speech_) {
            const double d = meaning_.strength("[muitos]", w) - meaning_.strength("[um]", w);
            score[i] += many ? d : -d;
        }
        if (tense_in_speech_) {
            const double t = meaning_.strength("[antes]", w) - meaning_.strength("[agora]", w);
            score[i] += past ? t : -t;
        }
        best = std::max(best, score[i]);
    }
    std::vector<int> out;
    for (size_t i = 0; i < forms.size(); ++i) if (score[i] >= best - 0.02) out.push_back(forms[i]);
    return out;
}

// a frase fala de algo que já passou? (alguma palavra dela veio junto com lembrar)
bool Eva2::sounds_past(const std::vector<std::string>& words) const {
    // a palavra mais marcada de tempo decide ("vê" é agora mesmo que "que" venha
    // mais em perguntas sobre o passado)
    double best = 0.0;
    for (const auto& w : words) {
        const double d = meaning_.strength("[antes]", w) - meaning_.strength("[agora]", w);
        if (std::fabs(d) > std::fabs(best)) best = d;
    }
    return best > 0.05;
}

std::vector<std::vector<int>> Eva2::plan_for(const Scene& s, const std::vector<int>& roles, bool use_order, bool past) {
    std::vector<int> order = roles;
    if (use_order) std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return roles_.expected_slot(a) < roles_.expected_slot(b); });
    else std::shuffle(order.begin(), order.end(), rng_);
    std::vector<std::vector<int>> plan;
    for (int r : order) {
        std::string m;
        bool many = false;
        if (r == kAgent) { m = "[ve:" + s.agent + "]"; many = s.agent_many; }
        else if (r == kPatient) { m = "[ve:" + s.patient + "]"; many = s.patient_many; }
        else if (r == kPlace) m = "[lugar:" + s.place + "]";
        else { m = "[acao:" + s.action + "]"; many = s.agent_many; }  // o verbo concorda com quem faz
        auto forms = choose_forms(forms_of(m), many, past && r == kAction);
        if (!forms.empty()) plan.push_back(forms);
    }
    return plan;
}

std::vector<std::string> Eva2::speak_plan(const std::vector<std::vector<int>>& plan, bool learn) {
    if (plan.empty()) return {};
    reset_cortex();
    feed(kMe, learn, dopa_.level);
    plan_ = plan;
    plan_pos_ = 0;
    plan_active_ = true;
    auto words = generate(cfg_.max_reply_words, learn);
    plan_active_ = false;
    plan_.clear();
    reset_cortex();
    return words;
}

std::vector<std::string> Eva2::describe(const Scene& scene, bool use_order, bool past) {
    std::vector<int> roles{kAgent, kAction};
    if (!scene.patient.empty()) roles.push_back(kPatient);
    else if (scene.action == "está") roles.push_back(kPlace);
    return speak_plan(plan_for(scene, roles, use_order && role_order_in_speech_, past), false);
}

int Eva2::understand(const std::vector<std::string>& words, const std::vector<Scene>& options) {
    Scene guess;
    for (const auto& [slot, m] : content_slots(words)) {
        if (m.rfind("[acao:", 0) == 0) { guess.action = m.substr(6, m.size() - 7); continue; }
        if (m.rfind("[ve:", 0) != 0) continue;
        const std::string ent = m.substr(4, m.size() - 5);
        int r;
        if (role_order_in_understanding_) r = roles_.role_for_slot(slot, {kAgent, kPatient});
        else { std::uniform_int_distribution<int> c(0, 1); r = c(rng_) ? kAgent : kPatient; }
        if (r == kAgent && guess.agent.empty()) guess.agent = ent;
        else if (guess.patient.empty()) guess.patient = ent;
        else if (guess.agent.empty()) guess.agent = ent;
    }
    int best = -1, bs = -1;
    std::vector<int> ties;
    for (size_t i = 0; i < options.size(); ++i) {
        const Scene& o = options[i];
        const int sc = (o.agent == guess.agent) + (o.patient == guess.patient) + (o.action == guess.action);
        if (sc > bs) { bs = sc; ties = {static_cast<int>(i)}; }
        else if (sc == bs) ties.push_back(static_cast<int>(i));
    }
    if (ties.empty()) return -1;
    std::uniform_int_distribution<size_t> pick(0, ties.size() - 1);
    best = ties[pick(rng_)];
    return best;
}

// Pergunta -> o que se pede (aprendido) -> a lembrança que responde -> o plano da resposta
std::vector<std::vector<int>> Eva2::answer_plan(const std::vector<std::string>& question) {
    if (!cfg_.episodes_enabled) return {};
    // só as palavras sem sentido próprio contam como palavra de pergunta ("comeu"
    // nomeia uma ação; "que", "quem", "cadê" não nomeiam nada: pedem algo)
    std::vector<std::string> qw;
    for (const auto& w : question) if (meaning_of(w).empty()) qw.push_back(w);
    auto [asked, conf] = qwords_.asks(qw);
    if (asked < 0 || conf < cfg_.question_confidence) return {};
    Scene cue;
    for (const auto& [slot, m] : content_slots(question)) {
        if (m.rfind("[acao:", 0) == 0) cue.action = m.substr(6, m.size() - 7);
        else if (m.rfind("[ve:", 0) == 0) {
            const std::string ent = m.substr(4, m.size() - 5);
            if (asked == kAgent) cue.patient = ent; else if (cue.agent.empty()) cue.agent = ent;
        }
    }
    if (std::getenv("EVA2_GDEBUG")) std::fprintf(stderr, "  [pergunta] pede %s (%.2f) | pista: quem=%s acao=%s alvo=%s | passado=%d\n",
                                                 role_name(asked), conf, cue.agent.c_str(), cue.action.c_str(), cue.patient.c_str(), (int)sounds_past(question));
    if (cue.agent.empty() && cue.patient.empty()) return {};
    Scene told;
    if (memory_in_answers_) {
        const Episode* ep = nullptr;
        if (asked == kPlace) {
            // onde está X: onde X apareceu por último, fazendo ou sofrendo a ação
            for (auto it = episodes_.all().rbegin(); it != episodes_.all().rend() && !ep; ++it)
                if (it->scene.agent == cue.agent || it->scene.patient == cue.agent) ep = &*it;
        } else ep = episodes_.recall(cue, asked);
        if (!ep) return {};
        told = ep->scene;
    } else {
        // sem memória: ela sabe o que se pede, mas chuta o conteúdo (um episódio qualquer)
        if (episodes_.size() == 0) return {};
        std::uniform_int_distribution<size_t> pick(0, episodes_.size() - 1);
        told = episodes_.all()[pick(rng_)].scene;
        if (!cue.agent.empty()) told.agent = cue.agent;
        if (!cue.patient.empty() && asked == kAgent) told.patient = cue.patient;
    }
    if (asked == kPlace) {
        Scene at{told.agent.empty() ? cue.agent : told.agent, "está", "", told.place};
        at.agent_many = told.agent_many;
        if (!cue.agent.empty() && cue.agent != told.agent) { at.agent = cue.agent; at.agent_many = told.patient_many; }  // cadê o X: X está onde o episódio aconteceu
        return plan_for(at, {kAgent, kAction, kPlace}, role_order_in_speech_, false);  // "cadê" é agora
    }
    // a pergunta veio no passado ("o que o gato comeu?"): a resposta também
    return plan_for(told, {kAgent, kAction, kPatient}, role_order_in_speech_, sounds_past(question));
}

std::vector<std::string> Eva2::answer(const std::vector<std::string>& question) {
    return speak_plan(answer_plan(question), false);
}

// ---------------------------------------------------------------- olhos das cenas

void Eva2::open_eyes(const std::vector<Image>& samples) {
    eyes_.prime(samples);
    eyes_open_ = true;
    things_.threshold = places_.threshold = cfg_.concept_match;
}

// Ver uma coisa: os olhos dão a descrição invariante; a memória de conceitos diz
// se é algo conhecido. Se ela está ouvindo uma palavra que já nomeia um conceito
// parecido o bastante com o que vê, a palavra decide (rótulos ajudam a criança a
// separar categorias; Waxman e Markow 1995, Xu 2002). Senão, os olhos decidem:
// parecido o bastante com um conceito, é ele; senão, é coisa nova.
std::string Eva2::see_with(ConceptMemory& mem, const char* prefix, const char* meaning_prefix, const Image& img, bool learn,
                           const std::vector<std::string>& words) {
    const auto v = eyes_.invariant_dense(img);
    const std::string mp = meaning_prefix;
    int labeled = -1; float ls = -2.0f;
    for (const auto& w : words) {
        const std::string m = meaning_of(w);
        if (m.rfind(mp + prefix, 0) != 0) continue;
        const int id = std::atoi(m.c_str() + mp.size() + 1);
        const float sc = mem.similarity(v, id);
        if (sc > ls) { ls = sc; labeled = id; }
    }
    if (labeled >= 0 && ls >= cfg_.label_match) {
        if (learn) mem.add_exemplar(labeled, v);
        return prefix + std::to_string(labeled);
    }
    auto [id, sc] = mem.best(v);
    if (id >= 0 && (sc >= mem.threshold || !learn)) {
        if (learn) mem.add_exemplar(id, v);
        return prefix + std::to_string(id);
    }
    if (!learn) return {};
    return prefix + std::to_string(mem.create(v));
}

// O nome junta: se a mesma palavra ("gato") se liga com força a dois conceitos
// visuais, eles eram a mesma coisa vista de jeitos diferentes. No sono, viram um
// só (os exemplares, o sentido das palavras e os episódios passam para o mais nomeado).
int Eva2::consolidate_concepts(ConceptMemory& mem, const char* prefix, const char* meaning_prefix) {
    const std::string mp = std::string(meaning_prefix) + prefix;
    int merged = 0;
    for (size_t k = 0; k < vocab_.words.size(); ++k) {
        const std::string& w = vocab_.words[k];
        if (Vocabulary::internal(w) || meaning_.frequency(w) > cfg_.topic_function_freq) continue;
        std::vector<std::pair<float, std::string>> named;
        for (const auto& [p, sc] : meaning_.perceptions_for(w, 0.05f, cfg_.fast_mapping_evidence))
            if (p.rfind(mp, 0) == 0) { const float sp = meaning_.specificity(p, w); if (sp >= cfg_.merge_specificity) named.push_back({sp, p}); }
        if (named.size() < 2) continue;
        std::sort(named.rbegin(), named.rend());
        const std::string& into = named[0].second;
        const int into_id = std::atoi(into.c_str() + mp.size());
        for (size_t i = 1; i < named.size(); ++i) {
            const std::string& from = named[i].second;
            const int from_id = std::atoi(from.c_str() + mp.size());
            if (!mem.alive(from_id) || !mem.alive(into_id)) continue;
            mem.merge(into_id, from_id);
            meaning_.merge_perception(into, from);
            episodes_.rename(prefix + std::to_string(from_id), prefix + std::to_string(into_id));
            ++merged;
        }
    }
    return merged;
}

std::string Eva2::see_thing(const Image& img, bool learn, const std::vector<std::string>& words) {
    return see_with(things_, "c", "[ve:", img, learn, words);
}

std::string Eva2::see_place(const Image& img, bool learn, const std::vector<std::string>& words) {
    return see_with(places_, "l", "[lugar:", img, learn, words);
}

// ---------------------------------------------------------------- tema

void Eva2::add_topic(const std::string& w, float weight) {
    if (!cfg_.topic_enabled || Vocabulary::internal(w)) return;
    if (meaning_.frequency(w) > cfg_.topic_function_freq) return;  // "o", "é", "de": não são assunto
    const int id = vocab_.add(w, rng_);
    if (topic_.size() < vocab_.words.size()) topic_.resize(vocab_.words.size(), 0.0f);
    topic_[id] = std::min(1.5f, topic_[id] + weight);
}

void Eva2::decay_topic(float factor) {
    for (float& t : topic_) t *= factor;
}

// Quanto dizer a palavra k serve ao tema: ela mesma é tema, ou é o caminho
// usual até uma palavra do tema (de tudo que vem antes de "porcos", quanto é
// "os"?). Querer dizer "porcos" puxa "os", não "o"; querer dizer "praia" puxa
// "na". É a intenção chegando um passo antes da palavra.
float Eva2::topical(int k) const {
    if (topic_.empty()) return 0.0f;
    float s = (k < static_cast<int>(topic_.size()) && !topic_said_) ? topic_[k] : 0.0f;
    if (!topic_said_ && k < static_cast<int>(next_counts_.size())) {
        // o caminho mais forte até uma palavra que ela quer dizer (não a soma de
        // muitos caminhos fracos: querer dizer muitas coisas de leve não é querer dizer nada)
        float ahead = 0.0f;
        for (const auto& [t, c] : next_counts_[k])
            if (t < static_cast<int>(topic_.size()) && topic_[t] >= cfg_.topic_intent && prev_total_[t] > 0.0f)
                ahead = std::max(ahead, topic_[t] * c / prev_total_[t]);
        s += ahead;
    }
    return s;
}

std::vector<std::pair<std::string, float>> Eva2::topic(size_t n) const {
    std::vector<std::pair<std::string, float>> out;
    for (size_t k = 0; k < topic_.size(); ++k) if (topic_[k] > 0.05f) out.push_back({vocab_.words[k], topic_[k]});
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.second > b.second; });
    if (out.size() > n) out.resize(n);
    return out;
}

// O nome que um código visual evoca (cosseno entre o código visto e o que cada
// palavra evoca), com a força
std::pair<std::string, float> Eva2::visual_name(const VisualCode& c) const {
    std::map<std::string, float> dot;
    float norm_code = 0.0f;
    for (const auto& [tok, act] : visual_tokens(c)) {
        norm_code += act * act;
        for (const auto& [w, st] : meaning_.words_for(tok, 0.0f)) {
            if (meaning_.frequency(w) > cfg_.function_word_freq) continue;
            dot[w] += act * st;
        }
    }
    std::string name; float best = 0.0f;
    for (const auto& [w, d] : dot) {
        float norm_w = 0.0f;
        for (const auto& [p, st] : meaning_.perceptions_for(w, 0.0f)) norm_w += st * st;
        const float sc = d / (std::sqrt(norm_code * norm_w) + 1e-6f);
        if (sc > best) { best = sc; name = w; }
    }
    return {name, best};
}

std::vector<std::string> Eva2::name_image(const Image& img) {
    VisualCode c = vision_.see(img, false);
    // cosseno entre o código visto (ativação por unidade) e o que cada palavra
    // evoca (força por percepção). Uma palavra de lugar ("cozinha") evoca a
    // mesa, o café, a fruta e o próprio lugar: espalhada, parece menos com
    // qualquer coisa só. "café" evoca quase só o café: cosseno alto.
    std::map<std::string, float> dot;
    float norm_code = 0.0f;
    for (const auto& [tok, act] : visual_tokens(c)) {
        norm_code += act * act;
        for (const auto& [w, st] : meaning_.words_for(tok, 0.0f)) {
            if (meaning_.frequency(w) > cfg_.function_word_freq) continue;  // "olha", "a": vão com tudo
            dot[w] += act * st;
        }
    }
    std::map<std::string, float> score;
    for (const auto& [w, d] : dot) {
        float norm_w = 0.0f;
        for (const auto& [p, st] : meaning_.perceptions_for(w, 0.0f)) norm_w += st * st;
        score[w] = d / (std::sqrt(norm_code * norm_w) + 1e-6f);
    }
    std::string name; float best = 0.0f;
    for (const auto& [w, sc] : score) if (sc > best) { best = sc; name = w; }
    if (std::getenv("EVA2_DEBUG")) { std::vector<std::pair<float, std::string>> v; for (auto& [w, sc] : score) v.push_back({sc, w}); std::sort(v.rbegin(), v.rend()); std::fprintf(stderr, "[nomear:"); for (size_t i = 0; i < v.size() && i < 4; ++i) std::fprintf(stderr, " %s(%.2f)", v[i].second.c_str(), v[i].first); std::fprintf(stderr, "]\n"); }
    if (name.empty() || best < cfg_.meaning_threshold) return {};
    reset_cortex();
    feed(kWorld, false, 1.0f);
    tm_.step(visual_sdr(c), false, 1.0f);
    feed(kMe, false, 1.0f);
    feed(name, false, 1.0f);
    auto words = generate(5, false);
    words.insert(words.begin(), name);
    reset_cortex();
    return words;
}

std::vector<std::string> Eva2::name_what_you_see(const std::string& object) {
    return name_image(draw_object(object, rng_, 1.0f));
}

// Imaginar: as palavras evocam unidades visuais; a descida reconstrói a imagem.
// Várias palavras juntas acendem unidades de coisas diferentes: sai a mistura.
Image Eva2::imagine_image(const std::vector<std::string>& words, float* confidence) {
    std::vector<float> weights(vision_.config().v2_units, 0.0f);
    std::vector<int> units;
    float total = 0.0f;
    for (const auto& w : words)
        for (const auto& [p, sc] : meaning_.perceptions_for(w, 0.05f)) {
            if (p.rfind("[v", 0) != 0) continue;
            const int u = std::atoi(p.c_str() + 2);
            if (u < 0 || u >= static_cast<int>(weights.size())) continue;
            if (weights[u] == 0.0f) units.push_back(u);
            weights[u] += sc;
            total += sc;
        }
    if (confidence) *confidence = units.empty() ? 0.0f : total / units.size();
    if (units.empty()) { Image blank; for (float& x : blank.px) x = 0.5f; return blank; }
    return vision_.imagine(units, &weights);
}

std::vector<Utterance> Eva2::idle(int ms) {
    std::vector<Utterance> out;
    int elapsed = 0;
    while (elapsed < ms) {
        const int chunk = std::min(1000, ms - elapsed);
        pass_time(chunk);
        elapsed += chunk;
        const auto states = active_states();
        if (drives_.talk >= 1.0f) {
            // pensar alto: o que ela vê evoca palavras; o córtex de sequências segue dali
            std::vector<std::string> head{kMe};
            for (const auto& s : states) head.push_back(s);
            reset_cortex();
            perceive(false);
            feed_tokens(head, false, 1.0f);
            std::vector<std::string> words;
            std::uniform_real_distribution<double> u01(0.0, 1.0);
            if (cfg_.episodes_enabled && episodes_.size() > 0 && u01(rng_) < cfg_.recall_chance) {
                // lembrar: um episódio marcante e recente volta, e ela conta
                if (const Episode* ep = episodes_.salient(clock_ms_, u01(rng_))) {
                    words = describe(ep->scene, true, true);  // lembrança: no passado
                    if (!words.empty()) last_recalled_ = ep->scene;
                    episodes_.told(ep);
                }
            }
            if (words.empty() && cfg_.world_enabled) {
                // a coisa à vista com nome mais firme puxa a fala
                std::string best; float bs = 0.0f;
                for (const auto& p : world_.perceive()) {
                    auto ws = meaning_.words_for(p, cfg_.meaning_threshold);
                    if (!ws.empty() && ws.front().second > bs) { bs = ws.front().second; best = ws.front().first; }
                }
                if (!best.empty()) {
                    feed(best, false, 1.0f);
                    words = generate(7, false);
                    words.insert(words.begin(), best);
                }
            }
            if (words.empty()) words = generate(8, false);
            reset_cortex();
            if (!words.empty()) {
                out.push_back({Utterance::Kind::Thought, words, states, 0.0f});
                drives_.on_spoke();
                diary("eva", words, "pensamento");
            }
        } else if (drives_.curiosity >= 1.0f && cfg_.world_enabled) {
            // curiosa: olha o que tem em volta; se vê algo que não sabe nomear, aponta
            // (é a pergunta antes da palavra, como um bebê faz)
            const auto& objs = world_.places.at(world_.here).objects;
            std::vector<std::string> unnamed;
            for (const auto& o : objs) if (name_what_you_see(o).empty()) unnamed.push_back(o);
            if (!unnamed.empty()) {
                std::uniform_int_distribution<size_t> pick(0, unnamed.size() - 1);
                const std::string o = unnamed[pick(rng_)];
                out.push_back({Utterance::Kind::Point, {o}, states, 0.0f});
                drives_.on_asked();
                diary("eva", {"(aponta para " + o + ")"}, "pergunta");
                continue;
            }
        } else if (drives_.curiosity >= 1.0f && vocab_.words.size() > 7) {
            // curiosa, tudo nomeado: pega uma palavra e pergunta com ela
            std::uniform_int_distribution<size_t> pick(6, vocab_.words.size() - 1);
            const std::string w = vocab_.words[pick(rng_)];
            reset_cortex();
            feed_tokens({kMe, kCurious, w}, false, 1.0f);
            auto words = generate(5, false);
            reset_cortex();
            words.insert(words.begin(), w);
            out.push_back({Utterance::Kind::Question, words, states, 0.0f});
            drives_.on_asked();
            drives_.on_spoke();
            diary("eva", words, "pergunta");
        }
    }
    return out;
}

// Sono: repete turnos guardados (os emocionais com mais frequência) e grava de
// novo. É consolidação: o que importou fica mais firme.
void Eva2::sleep(int episodes) {
    if (eyes_open_) {
        consolidate_concepts(things_, "c", "[ve:");
        consolidate_concepts(places_, "l", "[lugar:");
    }
    if (history_.empty()) return;
    if (cfg_.categories_enabled) {
        // de noite as categorias se reorganizam; as conversas regravadas a seguir
        // passam a usar os códigos novos
        cats_.update_all();
        for (int k = 0; k < static_cast<int>(vocab_.words.size()); ++k) apply_category(k);
    }
    std::vector<float> w;
    for (const auto& t : history_) w.push_back(0.2f + t.dopamine);
    std::discrete_distribution<size_t> pick(w.begin(), w.end());
    for (int e = 0; e < episodes; ++e) {
        size_t k = pick(rng_);
        reset_cortex();
        // repete um trecho: o turno sorteado e o seguinte (a troca inteira)
        for (size_t j = k; j < history_.size() && j < k + 2; ++j)
            feed_tokens(history_[j].tokens, true, 0.7f * history_[j].dopamine);
    }
    reset_cortex();
    drives_.curiosity = std::min(drives_.curiosity + 0.3f, 2.0f);  // acorda curiosa
}

std::vector<std::string> Eva2::complete(const std::vector<std::string>& start, int max_words) {
    reset_cortex();
    feed_tokens(start, false, 1.0f);
    auto words = generate(max_words, false);
    reset_cortex();
    return words;
}

// ---------------------------------------------------------------- memória longa

bool Eva2::save() const {
    std::ofstream out(cfg_.memory_file, std::ios::binary);
    if (!out) return false;
    out.write(kMemoryMagic, 8);
    const uint32_t nw = static_cast<uint32_t>(vocab_.words.size());
    out.write(reinterpret_cast<const char*>(&nw), sizeof nw);
    for (size_t k = 0; k < vocab_.words.size(); ++k) {
        const uint32_t len = static_cast<uint32_t>(vocab_.words[k].size());
        out.write(reinterpret_cast<const char*>(&len), sizeof len);
        out.write(vocab_.words[k].data(), len);
        for (const auto* part : {&vocab_.ident[k], &vocab_.cat[k]}) {
            const uint32_t m = static_cast<uint32_t>(part->size());
            out.write(reinterpret_cast<const char*>(&m), sizeof m);
            for (int c : *part) { const int32_t v = c; out.write(reinterpret_cast<const char*>(&v), sizeof v); }
        }
    }
    tm_.save(out);
    ctm_.save(out);
    const uint32_t nh = static_cast<uint32_t>(history_.size());
    out.write(reinterpret_cast<const char*>(&nh), sizeof nh);
    for (const auto& t : history_) {
        const uint8_t mine = t.mine;
        out.write(reinterpret_cast<const char*>(&mine), 1);
        out.write(reinterpret_cast<const char*>(&t.dopamine), sizeof t.dopamine);
        const uint32_t n = static_cast<uint32_t>(t.tokens.size());
        out.write(reinterpret_cast<const char*>(&n), sizeof n);
        for (const auto& s : t.tokens) {
            const uint32_t len = static_cast<uint32_t>(s.size());
            out.write(reinterpret_cast<const char*>(&len), sizeof len);
            out.write(s.data(), len);
        }
    }
    out.write(reinterpret_cast<const char*>(&dopa_.mood), sizeof dopa_.mood);
    out.write(reinterpret_cast<const char*>(&drives_.curiosity), sizeof drives_.curiosity);
    meaning_.save(out);
    cats_.save(out);
    episodes_.save(out);
    roles_.save(out);
    qwords_.save(out);
    out.write(reinterpret_cast<const char*>(&clock_ms_), sizeof clock_ms_);
    {
        const uint8_t open = eyes_open_;
        out.write(reinterpret_cast<const char*>(&open), 1);
        if (eyes_open_) { eyes_.save(out); things_.save(out); places_.save(out); }
    }
    {   // caminhos entre palavras (quem vem antes de quem): servem ao tema
        const uint32_t n = static_cast<uint32_t>(next_counts_.size());
        out.write(reinterpret_cast<const char*>(&n), sizeof n);
        for (uint32_t k = 0; k < n; ++k) {
            out.write(reinterpret_cast<const char*>(&next_total_[k]), sizeof(float));
            out.write(reinterpret_cast<const char*>(&prev_total_[k]), sizeof(float));
            const uint32_t m = static_cast<uint32_t>(next_counts_[k].size());
            out.write(reinterpret_cast<const char*>(&m), sizeof m);
            for (const auto& [t, c] : next_counts_[k]) {
                const int32_t ti = t;
                out.write(reinterpret_cast<const char*>(&ti), sizeof ti);
                out.write(reinterpret_cast<const char*>(&c), sizeof c);
            }
        }
    }
    vision_.save(out);
    const uint32_t nu = static_cast<uint32_t>(unit_columns_.size());
    out.write(reinterpret_cast<const char*>(&nu), sizeof nu);
    for (const auto& cols : unit_columns_) for (int c : cols) { const int32_t v = c; out.write(reinterpret_cast<const char*>(&v), sizeof v); }
    return static_cast<bool>(out);
}

bool Eva2::load() {
    std::ifstream in(cfg_.memory_file, std::ios::binary);
    if (!in) return false;
    char magic[8] = {};
    in.read(magic, 8);
    if (!in || std::string(magic, 8) != std::string(kMemoryMagic, 8)) {
        std::fprintf(stderr, "(memoria em %s e de uma versao antiga da Eva; comecando do zero)\n", cfg_.memory_file.c_str());
        return false;
    }
    uint32_t nw = 0;
    in.read(reinterpret_cast<char*>(&nw), sizeof nw);
    Vocabulary v = vocab_;
    v.words.clear(); v.ident.clear(); v.cat.clear(); v.codes.clear(); v.index.clear();
    for (uint32_t k = 0; k < nw; ++k) {
        uint32_t len = 0;
        in.read(reinterpret_cast<char*>(&len), sizeof len);
        std::string w(len, ' ');
        in.read(w.data(), len);
        std::vector<int> parts[2];
        for (auto& part : parts) {
            uint32_t m = 0;
            in.read(reinterpret_cast<char*>(&m), sizeof m);
            if (m > 4096) return false;
            part.resize(m);
            for (int& c : part) { int32_t x; in.read(reinterpret_cast<char*>(&x), sizeof x); c = x; }
        }
        std::vector<int> code = parts[0];
        code.insert(code.end(), parts[1].begin(), parts[1].end());
        std::sort(code.begin(), code.end());
        v.words.push_back(w);
        v.ident.push_back(parts[0]);
        v.cat.push_back(parts[1]);
        v.codes.push_back(code);
        v.index[w] = static_cast<int>(k);
    }
    if (!in || !tm_.load(in) || !ctm_.load(in)) return false;
    vocab_ = v;
    uint32_t nh = 0;
    in.read(reinterpret_cast<char*>(&nh), sizeof nh);
    history_.clear();
    for (uint32_t i = 0; i < nh; ++i) {
        Turn t;
        uint8_t mine = 0;
        in.read(reinterpret_cast<char*>(&mine), 1);
        t.mine = mine;
        in.read(reinterpret_cast<char*>(&t.dopamine), sizeof t.dopamine);
        uint32_t n = 0;
        in.read(reinterpret_cast<char*>(&n), sizeof n);
        for (uint32_t j = 0; j < n; ++j) {
            uint32_t len = 0;
            in.read(reinterpret_cast<char*>(&len), sizeof len);
            std::string s(len, ' ');
            in.read(s.data(), len);
            t.tokens.push_back(s);
        }
        history_.push_back(t);
    }
    in.read(reinterpret_cast<char*>(&dopa_.mood), sizeof dopa_.mood);
    in.read(reinterpret_cast<char*>(&drives_.curiosity), sizeof drives_.curiosity);
    meaning_.load(in);
    cats_.load(in);
    episodes_.load(in);
    roles_.load(in);
    qwords_.load(in);
    in.read(reinterpret_cast<char*>(&clock_ms_), sizeof clock_ms_);
    {
        uint8_t open = 0;
        in.read(reinterpret_cast<char*>(&open), 1);
        eyes_open_ = open;
        if (eyes_open_) { eyes_.load(in); things_.load(in); places_.load(in); }
    }
    {
        uint32_t n = 0;
        in.read(reinterpret_cast<char*>(&n), sizeof n);
        if (n > 10000000) return false;
        next_counts_.assign(n, {}); next_total_.assign(n, 0.0f); prev_total_.assign(n, 0.0f);
        for (uint32_t k = 0; k < n; ++k) {
            in.read(reinterpret_cast<char*>(&next_total_[k]), sizeof(float));
            in.read(reinterpret_cast<char*>(&prev_total_[k]), sizeof(float));
            uint32_t m = 0;
            in.read(reinterpret_cast<char*>(&m), sizeof m);
            for (uint32_t i = 0; i < m && in; ++i) {
                int32_t t; float c;
                in.read(reinterpret_cast<char*>(&t), sizeof t);
                in.read(reinterpret_cast<char*>(&c), sizeof c);
                next_counts_[k][t] = c;
            }
        }
        rebuild_prev_counts();
    }
    vision_.load(in);
    uint32_t nu = 0;
    in.read(reinterpret_cast<char*>(&nu), sizeof nu);
    if (nu == unit_columns_.size()) for (auto& cols : unit_columns_) for (int& c : cols) { int32_t v; in.read(reinterpret_cast<char*>(&v), sizeof v); c = v; }
    return true;
}

void Eva2::diary(const std::string& who, const std::vector<std::string>& words, const std::string& note) {
    std::ofstream out(cfg_.diary_file, std::ios::app);
    if (!out) return;
    out << who << ":";
    for (const auto& w : words) out << " " << w;
    if (!note.empty()) out << "   (" << note << ")";
    out << "\n";
}

}  // namespace eva2
