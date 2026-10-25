#include "eva2.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <map>

namespace eva2 {

std::string Utterance::text() const {
    std::string s;
    for (size_t i = 0; i < words.size(); ++i) s += (i ? " " : "") + words[i];
    if (kind == Kind::Question) s += "?";
    return s;
}

Eva2::Eva2(Eva2Config cfg, unsigned seed)
    : cfg_(cfg), rng_(seed), tm_([&] { TMConfig t = cfg.tm; t.columns = cfg.sdr_columns; return t; }(), seed) {
    vocab_.columns = cfg_.sdr_columns;
    vocab_.active = cfg_.sdr_active;
    for (const char* k : {kYou, kMe, kEnd, kHappy, kCurious, kSurprised, kWorld}) vocab_.add(k, rng_);
    drives_.talk = 0.4f;  // ela chega com alguma vontade de conversar

    // Infância dos olhos: V1 amadurece vendo as coisas do mundo (sem nomes), e
    // fecha o período crítico. V2 continua aprendendo a vida toda.
    std::vector<Image> samples;
    for (int r = 0; r < 4; ++r) for (const auto& o : world_.all_objects()) samples.push_back(draw_object(o, rng_, 1.0f));
    vision_.prime(samples);
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

void Eva2::feed(const std::string& token, bool learn, float dopamine) {
    const int id = vocab_.add(token, rng_);
    tm_.step(vocab_.codes[id], learn, dopamine);
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
        tm_.step(vocab_.codes[id], false, 1.0f);
        if (content) { total += tm_.last_surprise(); ++n; }
    }
    h.surprise = n ? total / n : 0.0f;
    if (!h.new_words.empty()) h.surprise = std::max(h.surprise, 0.9f);

    // química: atenção + novidade + carinho
    dopa_.burst(reward + cfg_.attention_bonus + cfg_.novelty_bonus * h.surprise);
    drives_.on_heard(h.surprise);
    surprise_trace_ = std::max(surprise_trace_, h.surprise);

    // agora vive de verdade, gravando, com a dopamina do momento
    tm_.reset();
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
    // sentido: o que ela vê agora fica ligado ao que ouve agora, em qualquer ordem.
    // Antes de ligar, imagina: o que essas palavras já evocavam (é isso que guia a ação)
    if (cfg_.world_enabled) {
        imagined_.clear();
        for (const auto& w : words) {
            if (meaning_.frequency(w) > cfg_.function_word_freq) continue;  // "a", "o", "e"...
            for (const auto& pr : meaning_.perceptions_for(w, cfg_.meaning_threshold, cfg_.meaning_min_evidence))
                imagined_.push_back(pr);
        }
        meaning_.note_turn(words);
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

// A próxima palavra é a que melhor casa com as colunas previstas. Empates e
// quase-empates: a curiosidade dá um pouco de aleatoriedade (ela explora).
int Eva2::predict_next(std::vector<int>* overlaps_out) {
    const auto pred = tm_.predicted_columns();
    if (overlaps_out) overlaps_out->assign(vocab_.words.size(), 0);
    if (pred.empty()) return -1;
    std::vector<std::pair<int, int>> cands;  // (overlap, id)
    for (size_t k = 0; k < vocab_.words.size(); ++k) {
        const auto& w = vocab_.words[k];
        if (w == kYou || w == kMe || w == kWorld) continue;  // marcadores não se dizem
        if (World::is_perception(w)) continue;  // percepções não se dizem (se imaginam)
        if (World::is_action(w) && !allow_actions_) continue;
        int ov = vocab_.overlap(static_cast<int>(k), pred);
        if (overlaps_out) (*overlaps_out)[k] = ov;
        // penaliza repetir o que acabou de dizer (saciedade)
        int rep = 0;
        for (const auto& r : recent_said_) if (r == w) ++rep;
        ov -= 6 * rep;
        if (ov >= cfg_.min_overlap) cands.push_back({ov, static_cast<int>(k)});
    }
    if (cands.empty()) return -1;
    std::sort(cands.rbegin(), cands.rend());
    // explora entre os quase-empatados conforme a curiosidade
    const int top = cands.front().first;
    const int slack = static_cast<int>(std::round(6.0f * state_level(kCurious)));
    std::vector<int> pool;
    for (auto& [ov, id] : cands) if (ov >= top - slack) pool.push_back(id);
    std::uniform_int_distribution<size_t> pick(0, pool.size() - 1);
    return pool[pick(rng_)];
}

std::vector<std::string> Eva2::generate(int max_words, bool learn) {
    std::vector<std::string> out;
    recent_said_.clear();
    int last_id = -1;
    bool burst_used = false;
    for (int i = 0; i < max_words; ++i) {
        int id = predict_next();
        if (id < 0 && last_id >= 0 && !burst_used) {
            // O contexto exato não prevê nada. No córtex real, a coluna "explode"
            // (todas as células) e a previsão passa a ser a continuação mais comum
            // da palavra, em qualquer contexto. É como a gente conclui uma frase
            // começada: pelo caminho mais batido.
            tm_.reset();
            tm_.step(vocab_.codes[last_id], false, 1.0f);
            id = predict_next();
            burst_used = true;
        }
        if (id < 0) break;
        const std::string& w = vocab_.words[id];
        if (w == kEnd) break;
        if (World::is_action(w)) { pending_action_ = w; break; }  // o corpo responde, não a boca
        if (!is_internal(w)) { out.push_back(w); recent_said_.push_back(w); burst_used = false; }
        tm_.step(vocab_.codes[id], learn, cfg_.self_learning * dopa_.level);
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

    if (!novel) {
        words = generate(cfg_.max_reply_words, true);
        // habituação: se ia dizer exatamente o que acabou de dizer, tenta outro caminho
        if (!words.empty() && is_repeat(words)) {
            recent_said_ = words;  // penaliza essas palavras na próxima tentativa
            tm_.reset();
            feed_tokens(head, false, 1.0f);
            auto alt = generate(cfg_.max_reply_words, true);
            words = is_repeat(alt) ? std::vector<std::string>{} : alt;
        }
    }
    if (words.empty() && !last_heard_.empty()) {
        // novidade (ou nada a dizer): imitar. A primeira palavra que você disse
        // entra no turno dela e o córtex segue dali, como uma criança repetindo.
        tm_.reset();
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
            tm_.reset();
            feed_tokens(head, false, 1.0f);
            tm_.step(vocab_.codes[vocab_.id(last_heard_[k])], false, 1.0f);
            auto cont = generate(cfg_.max_reply_words, false);
            if (cont.empty() || is_repeat(cont)) continue;
            // diz a partir do gancho, gravando como fala dela
            tm_.reset();
            feed_tokens(head, false, 1.0f);
            feed(last_heard_[k], true, cfg_.self_learning * dopa_.level);
            words = generate(cfg_.max_reply_words - 1, true);
            words.insert(words.begin(), last_heard_[k]);
            kind = Utterance::Kind::Reply;
        }
    }
    if (words.empty()) {
        tm_.reset();
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
            tm_.reset(); perceive(true); feed(action, true, dopa_.level);
            out.push_back({action, result + " (entendeu)", reward});
            diary("eva", {action}, result + " (entendeu)");
            last_heard_.clear();
            tm_.reset();
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
                tm_.reset(); perceive(true); feed(action, true, dopa_.level);
                out.push_back({action, result + " (indo para " + goal_place_ + ")", reward});
                diary("eva", {action}, result);
                if (goal_place_ == world_.here) { dopa_.burst(0.5f); goal_place_.clear(); }
                return out;
            }
        }
    }

    for (int k = 0; k < cfg_.max_actions_per_turn; ++k) {
        tm_.reset();
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
        tm_.reset();
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
    tm_.reset();
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
    tm_.reset();
    feed(kWorld, false, 1.0f);
    tm_.step(visual_sdr(c), false, 1.0f);
    feed(kMe, false, 1.0f);
    feed(name, false, 1.0f);
    auto words = generate(5, false);
    words.insert(words.begin(), name);
    tm_.reset();
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
            tm_.reset();
            perceive(false);
            feed_tokens(head, false, 1.0f);
            std::vector<std::string> words;
            if (cfg_.world_enabled) {
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
            tm_.reset();
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
            tm_.reset();
            feed_tokens({kMe, kCurious, w}, false, 1.0f);
            auto words = generate(5, false);
            tm_.reset();
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
    if (history_.empty()) return;
    std::vector<float> w;
    for (const auto& t : history_) w.push_back(0.2f + t.dopamine);
    std::discrete_distribution<size_t> pick(w.begin(), w.end());
    for (int e = 0; e < episodes; ++e) {
        size_t k = pick(rng_);
        tm_.reset();
        // repete um trecho: o turno sorteado e o seguinte (a troca inteira)
        for (size_t j = k; j < history_.size() && j < k + 2; ++j)
            feed_tokens(history_[j].tokens, true, 0.7f * history_[j].dopamine);
    }
    tm_.reset();
    drives_.curiosity = std::min(drives_.curiosity + 0.3f, 2.0f);  // acorda curiosa
}

std::vector<std::string> Eva2::complete(const std::vector<std::string>& start, int max_words) {
    tm_.reset();
    feed_tokens(start, false, 1.0f);
    auto words = generate(max_words, false);
    tm_.reset();
    return words;
}

// ---------------------------------------------------------------- memória longa

bool Eva2::save() const {
    std::ofstream out(cfg_.memory_file, std::ios::binary);
    if (!out) return false;
    const uint32_t nw = static_cast<uint32_t>(vocab_.words.size());
    out.write(reinterpret_cast<const char*>(&nw), sizeof nw);
    for (size_t k = 0; k < vocab_.words.size(); ++k) {
        const uint32_t len = static_cast<uint32_t>(vocab_.words[k].size());
        out.write(reinterpret_cast<const char*>(&len), sizeof len);
        out.write(vocab_.words[k].data(), len);
        for (int c : vocab_.codes[k]) { const int32_t v = c; out.write(reinterpret_cast<const char*>(&v), sizeof v); }
    }
    tm_.save(out);
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
    vision_.save(out);
    const uint32_t nu = static_cast<uint32_t>(unit_columns_.size());
    out.write(reinterpret_cast<const char*>(&nu), sizeof nu);
    for (const auto& cols : unit_columns_) for (int c : cols) { const int32_t v = c; out.write(reinterpret_cast<const char*>(&v), sizeof v); }
    return static_cast<bool>(out);
}

bool Eva2::load() {
    std::ifstream in(cfg_.memory_file, std::ios::binary);
    if (!in) return false;
    uint32_t nw = 0;
    in.read(reinterpret_cast<char*>(&nw), sizeof nw);
    Vocabulary v;
    v.columns = vocab_.columns;
    v.active = vocab_.active;
    for (uint32_t k = 0; k < nw; ++k) {
        uint32_t len = 0;
        in.read(reinterpret_cast<char*>(&len), sizeof len);
        std::string w(len, ' ');
        in.read(w.data(), len);
        std::vector<int> code(v.active);
        for (int& c : code) { int32_t x; in.read(reinterpret_cast<char*>(&x), sizeof x); c = x; }
        v.words.push_back(w);
        v.codes.push_back(code);
        v.index[w] = static_cast<int>(k);
    }
    if (!in || !tm_.load(in)) return false;
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
