// Eva 2 - a infância: os momentos de uma casa, um depois do outro.
//
// Antes cada programa (a conversa, os testes) tinha a sua cópia; agora é uma só.
// Um momento pode ser:
//   * ouvir alguém falar (a fala comum da casa);
//   * ver uma cena e ouvir a descrição ("o gato come o peixe"), às vezes com
//     uma pergunta e a resposta ("cadê o gato? o gato está na sala"), uma
//     pergunta de sim ou não ("o gato comeu o peixe? não, o gato comeu o pão"),
//     ou alguém dizendo o que não é ("o gato não está na sala");
//   * conversar depois sobre o que passou ("o que o gato comeu?");
//   * um livro ("olha a girafa"): ela conhece a girafa, mas nunca viu uma na casa;
//   * alguém pergunta algo que ninguém sabe ("cadê a girafa?" "não sei");
//   * nomes das coisas de cada um ("o meu gato chama mimi", "o seu ursinho chama
//     bob"), entre os pais ou com ela (a resposta no turno dela, como andaime);
//   * alguém conta o que ela não viu ("o papai está no mercado");
//   * sentimentos: depois de ver alguém chorar ou rir, ela sente; alguém diz
//     "você está triste", pergunta "como você está?" e responde por ela;
//     os pais dizem como eles estão ("eu estou feliz").
// Ela dorme a cada 2000 momentos.
#pragma once
#include <cstdio>
#include <functional>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "corpus.hpp"
#include "eva2.hpp"

namespace eva2 {

inline Scene scene_of(const corpus::Grammar::Event& ev) {
    Scene s{ev.agent, ev.action, ev.patient, ev.place};
    s.agent_many = ev.an == 'p';
    s.patient_many = ev.pn == 'p';
    s.valence = ev.valence;
    return s;
}

class Childhood {
public:
    using Event = corpus::Grammar::Event;
    // como ela percebe uma cena e uma coisa (símbolos, ou pelos olhos)
    using SeeEvent = std::function<Scene(Eva2&, const Event&, const std::vector<std::string>&, bool)>;
    using SeeThing = std::function<std::string(Eva2&, const std::string&, const std::vector<std::string>&)>;

    struct Owned { std::string owner, thing, name; };

    Childhood(corpus::Grammar& home, unsigned seed, SeeEvent see = {}, SeeThing see_thing = {})
        : home_(home), r_(seed), see_(see), see_thing_(see_thing) {
        if (!see_) see_ = [](Eva2&, const Event& ev, const std::vector<std::string>&, bool) { return scene_of(ev); };
        if (!see_thing_) see_thing_ = [](Eva2&, const std::string& t, const std::vector<std::string>&) { return t; };
        // a casa: cada um tem umas coisas com nome (os nomes da infância; os do teste são outros)
        auto things = home_.nameable();
        auto names = corpus::Grammar::pet_names();
        std::shuffle(things.begin(), things.end(), r_);
        size_t k = 0;
        for (const char* owner : {"mamãe", "papai", "eva"})
            for (int i = 0; i < 5 && k < things.size(); ++i, ++k) {
                std::uniform_int_distribution<size_t> pn(0, kChildhoodNames - 1);
                owned.push_back({owner, things[k], names[pn(r_)]});
            }
    }
    static constexpr size_t kChildhoodNames = 15;  // os primeiros 15 nomes; os outros ficam para o teste

    std::vector<Scene> seen;   // o que aconteceu de verdade, para conferir
    std::set<std::pair<std::string, std::string>> pairs;  // quem já apareceu junto com quem
    std::vector<Owned> owned;

    // vive `moments` momentos, dormindo a cada 2000; on_day(dia, momentos de hoje, total)
    void live(Eva2& e, int moments, const std::function<void(int, int, int)>& on_day = {}) {
        for (int done = 0; done < moments;) {
            const int today = std::min(2000, moments - done);
            for (int i = 0; i < today; ++i) moment(e);
            done += today;
            e.sleep(300);
            ++day_;
            if (on_day) on_day(day_, today, done);
        }
    }

private:
    corpus::Grammar& home_;
    std::mt19937 r_;
    SeeEvent see_;
    SeeThing see_thing_;
    int day_ = 0;
    std::vector<std::pair<Event, Scene>> past_;  // cenas de antes (e como ela as viu)
    std::uniform_real_distribution<double> u_{0.0, 1.0};
    double u() { return u_(r_); }
    template <class T> const T& pick(const std::vector<T>& v) { std::uniform_int_distribution<size_t> p(0, v.size() - 1); return v[p(r_)]; }
    const char* parent() { return u() < 0.5 ? "mamãe" : "papai"; }

    void moment(Eva2& e) {
        const double x = u();
        if (x < 0.30) { e.listen(home_.sentence()); return; }
        if (x < 0.33) { book(e); return; }
        if (x < 0.345) { unknown(e); return; }
        if (x < 0.40) { names(e); return; }
        if (x < 0.43) { tell(e); return; }
        if (x < 0.45) { parents_feel(e); return; }
        if (x >= 0.85 && past_.size() > 30) { remember(e, (x - 0.85) / 0.15); return; }
        scene(e);
    }

    // uma cena acontece na frente dela, e alguém descreve
    void scene(Eva2& e) {
        auto ev = home_.random_event();
        const auto words = home_.describe(ev);
        const auto sc = see_(e, ev, words, true);
        e.witness(sc, words);
        seen.push_back(scene_of(ev));
        past_.push_back({ev, sc});
        pairs.insert({ev.agent, ev.patient});
        pairs.insert({ev.patient, ev.agent});
        const double y = u();
        if (y < 0.25) {  // pergunta e resposta
            std::uniform_int_distribution<int> f(0, 2);
            int field = f(r_);
            if (!home_.can_ask(ev, field)) field = 0;
            e.witness_dialogue(sc, home_.question(ev, field), home_.answer(ev, field));
        } else if (y < 0.33) {  // é verdade? (metade das vezes não é)
            const bool yes = u() < 0.5 || !home_.can_alter(ev);
            const Event claim = yes ? ev : home_.altered(ev);
            e.witness_check(sc, home_.yes_no(claim), home_.yes_no_answer(ev, yes));
        } else if (y < 0.39 && home_.can_alter(ev)) {  // o que não é
            e.witness_negation(sc, home_.negated(home_.altered(ev)));
        }
        // ela sente o que viu; alguém nota e diz, pergunta, e responde por ela
        if (std::fabs(e.feeling()) >= e_threshold(e) && u() < 0.6) feelings(e);
    }
    static float e_threshold(const Eva2&) { return 0.25f; }

    void feelings(Eva2& e) {
        const bool happy = e.feeling() >= 0.0f;
        const double y = u();
        if (y < 0.5) e.witness_feeling(corpus::Grammar::feeling(false, happy), true, happy);   // "você está feliz"
        else if (y < 0.75) e.witness_feeling_dialogue(corpus::Grammar::ask_feeling(), corpus::Grammar::feeling(true, happy));  // "como você está?" "eu estou feliz"
        else {  // "você está triste?" "não, eu estou feliz"
            const bool claim = u() < 0.5;
            auto q = corpus::Grammar::feeling(false, claim);
            q.push_back("<?>");
            auto a = corpus::Grammar::feeling(true, happy);
            a.insert(a.begin(), claim == happy ? "sim" : "não");
            e.witness_feeling_dialogue(q, a);
        }
    }
    // os pais dizem como eles estão (ela vê a cara deles)
    void parents_feel(Eva2& e) {
        const bool happy = u() < 0.6;
        e.witness_feeling(corpus::Grammar::feeling(true, happy), false, happy);
    }

    // conversar sobre o que já passou
    void remember(Eva2& e, double y) {
        std::uniform_int_distribution<size_t> back(5, 30);
        const auto& [ev, seen_as] = past_[past_.size() - back(r_)];
        if (y < 0.4) {
            std::uniform_int_distribution<int> f(1, 2);
            const int field = f(r_);
            if (home_.can_ask(ev, field)) e.witness_dialogue(seen_as, home_.question(ev, field, true), home_.answer(ev, field, true), true);
        } else if (y < 0.67) {
            e.recount(seen_as, home_.describe(ev, true));
        } else {
            const bool yes = u() < 0.5 || !home_.can_alter(ev);
            const Event claim = yes ? ev : home_.altered(ev);
            e.witness_check(seen_as, home_.yes_no(claim, true), home_.yes_no_answer(ev, yes, true), true);
        }
    }

    // um livro: "olha a girafa" (às vezes coisas da casa, às vezes só de livro)
    void book(Eva2& e) {
        static std::vector<std::string> only;
        if (only.empty()) only = home_.book_things();
        std::string t;
        if (u() < 0.6) t = pick(only);
        else { auto things = home_.nameable(); t = pick(things); }
        std::vector<std::string> words{"olha"};
        auto np = np_of(t);
        words.insert(words.end(), np.begin(), np.end());
        e.look_at_book(see_thing_(e, t, words), words);
    }
    std::vector<std::string> np_of(const std::string& lemma) {
        // "a girafa": o artigo da gramática (a mesma frase que "cadê a girafa" usaria)
        Event ev; ev.kind = corpus::Grammar::AT; ev.agent = lemma; ev.place = "casa";
        auto q = home_.question(ev, 0);
        return {q.begin() + 1, q.end()};
    }

    // ninguém sabe: "cadê a girafa?" "não sei"
    void unknown(Eva2& e) {
        static std::vector<std::string> only;
        if (only.empty()) only = home_.book_things();
        const std::string t = pick(only);
        std::vector<std::string> q{"cadê"};
        auto np = np_of(t);
        q.insert(q.end(), np.begin(), np.end());
        e.witness_unknown(q, {"não", "sei"});
    }

    // nomes das coisas de cada um, e de quem é (meu e seu mudam com quem fala)
    void names(Eva2& e) {
        const Owned& o = pick(owned);
        const std::string thing = see_thing_(e, o.thing, {});
        const double y = u();
        if (o.owner != "eva") {
            const std::string other = o.owner == "mamãe" ? "papai" : "mamãe";
            if (y < 0.35) e.witness_naming(o.owner, "eva", o.owner, thing, home_.named(o.thing, true, o.name));         // "o meu gato chama mimi"
            else if (y < 0.65) e.witness_name_dialogue(other, o.owner, o.owner, thing, home_.ask_name(o.thing, false), home_.named(o.thing, true, o.name));  // entre os pais
            else e.witness_name_dialogue(o.owner, "eva", o.owner, thing, home_.ask_name(o.thing, true), home_.named(o.thing, false, o.name));  // "como chama o meu gato?" "o seu gato chama mimi" (andaime)
        } else {
            const char* p = parent();
            if (y < 0.5) e.witness_naming(p, "eva", "eva", thing, home_.named(o.thing, false, o.name));               // "o seu ursinho chama bob"
            else e.witness_name_dialogue(p, "eva", "eva", thing, home_.ask_name(o.thing, false), home_.named(o.thing, true, o.name));  // "como chama o seu ursinho?" "o meu ursinho chama bob"
        }
    }

    // alguém conta o que ela não viu ("o papai está no mercado")
    void tell(Eva2& e) {
        auto ev = home_.random_event();
        e.told(home_.describe(ev), parent(), "eva");
        seen.push_back(scene_of(ev));
    }
};

}  // namespace eva2
