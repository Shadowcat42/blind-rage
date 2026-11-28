#pragma once
// The mod's own tutorial boxes: content and sequencing. The boxes are the
// game's pausing tutorial box (see docs/MOD_TUTORIALS_RESEARCH.md); the wording
// was agreed with the player (docs/MOD_TUTORIAL_TEXT.md).
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace a11y {
enum class TutorialSound { none, lock_tone, full_lock_tone, beacon, fire_warning };
struct TutorialBox {
    std::string text;
    // Played after the box is confirmed, while the game is unpaused, so a game
    // sound is not cut and nothing overlaps speech. The next box opens once
    // `delay_ms` has passed.
    TutorialSound after = TutorialSound::none;
    uint32_t delay_ms = 0;
};
// Button names as the player's controls call them.
struct TutorialLabels {
    std::string cross = "cross", square = "square", triangle = "triangle", circle = "circle",
                l1 = "L1", r2 = "R2";
    std::string dpad_up = "up on the D-pad", dpad_left_right = "left or right on the D-pad";
};
inline std::string tutorial_fill(std::string text, const TutorialLabels &labels) {
    const std::pair<std::string_view, const std::string *> names[]{
        {"{cross}", &labels.cross},
        {"{square}", &labels.square},
        {"{triangle}", &labels.triangle},
        {"{circle}", &labels.circle},
        {"{r2}", &labels.r2},
        {"{l1}", &labels.l1},
        {"{dpad_up}", &labels.dpad_up},
        {"{dpad_left_right}", &labels.dpad_left_right}};
    for (auto [token, value] : names)
        for (size_t at; (at = text.find(token)) != std::string::npos;) {
            text.replace(at, token.size(), *value);
            // A name that starts a sentence is capitalized.
            bool starts =
                at == 0 || (at >= 2 && text[at - 1] == ' ' &&
                            (text[at - 2] == '.' || text[at - 2] == '?' || text[at - 2] == '!'));
            if (starts && at < text.size() && text[at] >= 'a' && text[at] <= 'z')
                text[at] = char(text[at] - 'a' + 'A');
        }
    return text;
}
// Episode 1's first rail-shooting section.
inline std::vector<TutorialBox> rail_shooting_tutorial(const TutorialLabels &labels) {
    std::vector<TutorialBox> boxes{
        {"Welcome to Asura's Wrath! Before you get to punching, welcome to the wonderful world "
         "of the rail shooter. In these sections, the game moves Asura and the camera for you, "
         "along a set route. You don't steer, but you can dodge with {cross}, which later "
         "episodes may expect of you. You aim a reticle with the left stick and fire at what "
         "comes at you. Pressing or holding {square} fires a constant stream of fast, but weak "
         "shots. {triangle}, though slower, fires a number of homing projectiles based off how "
         "many targets are currently locked... more on this next."},
        {"As you move your reticle over targets, they're locked onto. Think of it as marking "
         "multiple targets. You can only mark so many at once. You will know when a target is "
         "locked because of this beep, which gets higher as you mark more.",
         TutorialSound::lock_tone, 900},
        {"When you hear this sound, it means you have maxed out the number of targets you can "
         "possibly hit. Press {triangle} to unleash your wrath and fire homing shots at all "
         "marked targets.",
         TutorialSound::full_lock_tone, 900},
        {"An audio beacon leads your aim to a group of targets. Left or right in your ears "
         "means left or right. Higher or lower pitch means above or below, respectively. The "
         "pulses speed up as your aim closes in, and become a steady tone when you're on "
         "target.",
         TutorialSound::beacon, 1200},
        {"Pressing {dpad_up} will change your aiming mode: no guidance, the beacon, or the "
         "beacon with a wider locking cone than normal for slightly assisted aiming. "
         "{dpad_left_right} will change which enemy cluster the beacon is pointing at, however "
         "you shouldn't need this often. Don't feel like manually aiming? Hold {l1} for auto "
         "aim, and just blast away."}};
    for (auto &box : boxes)
        box.text = tutorial_fill(box.text, labels);
    return boxes;
}
// Episode 8's chase: a shooting section without the game's lock-on.
inline std::vector<TutorialBox> chase_tutorial(const TutorialLabels &labels) {
    std::vector<TutorialBox> boxes{
        {"This is a chase. Like rail shooting, the game moves Asura forward for you, but there "
         "is no lock-on here, and pushing left or right moves Asura himself across the path as "
         "well as your aim. Hold {square} to fire at the enemy you're chasing."},
        {"The enemy fires blasts that skim along the ground towards you, and it fires more as "
         "your Burst gauge fills. This buzz warns you. Centred means it's coming straight at "
         "you, so move aside or dodge with {cross}. Off to one side means it will pass on that "
         "side.",
         TutorialSound::fire_warning, 2300},
        {"When the enemy drops back to you, the game tells you to attack: hit it with {circle} "
         "for combos. Fill your Burst gauge and press {r2} to finish the chase. The beacon here "
         "points only at the enemy. Hold {l1} to auto aim up and down; left and right stay "
         "with you, since they also move Asura."}};
    for (auto &box : boxes)
        box.text = tutorial_fill(box.text, labels);
    return boxes;
}
// Episode 2's first melee section: a short introduction, since Episode 3 has
// the game's full melee tutorial.
inline std::vector<TutorialBox> melee_tutorial(const TutorialLabels &labels) {
    std::vector<TutorialBox> boxes{
        {"At last, you get to punching! (While loudly insisting you didn't kill the Emperor.) "
         "In these sections you move Asura freely with the left stick. Press {circle} to "
         "attack, and keep pressing for a combo. Episode 3 covers the rest in full."},
        {"On their own, your attacks only turn Asura toward an enemy that's close and roughly "
         "in front of him. Anything farther away or behind you gets missed. Press {l1} to lock "
         "on to the nearest enemy, and your attacks will follow it wherever it goes. Press {l1} "
         "again to let go."},
        {"Hold {circle} for a dash attack: Asura charges in, turned toward his target. "
         "Combined with lock-on, it's a quick way to get to an enemy that's out of reach."},
        {"A melee beacon leads you to enemies. Left or right in your ears means left or right; "
         "a higher tone means ahead of you, a lower one behind. It points to the nearest enemy "
         "and pulses faster as you close in. It turns into a steady tone once an enemy is in "
         "reach, which means your attacks will turn toward it. Press {dpad_up} to turn the "
         "beacon off or back on."}};
    for (auto &box : boxes)
        box.text = tutorial_fill(box.text, labels);
    return boxes;
}
// Fight tips: a short box when an enemy whose fight isn't obvious appears. Only
// what a blind player can't work out alone; the rest is left to discover.
enum class FightTip { none, elephant_gohma, augus_waves };
// Episode 7's giant elephant Gohma (BCPawn_zou01, BC-S060's second section).
inline std::vector<TutorialBox> fight_tip(FightTip tip) {
    if (tip == FightTip::elephant_gohma)
        return {{"This giant elephant Gohma can't just be beaten down. Watch for sweeping blows, "
                 "fireballs that come down where you were standing, and a huge stomp whose "
                 "shockwave lands under you. Right after the stomp it's open to a special attack, "
                 "so watch for the prompt. Once your Burst is half full it gets angry, and each "
                 "volley becomes a rain of fireballs. Good luck."}};
    // Episode 11's Augus after the eyecatch (BCPawn_ogs02, BC-S110's second section).
    if (tip == FightTip::augus_waves)
        return {{"Augus has decided he wants to become a zoner, keeping his distance and "
                 "fighting with sword waves. A buzz means a blade is about to hit you: step "
                 "toward the side it's on, and it stops once you're clear. A quick rising tone "
                 "means a wide wave is coming: jump when the tone ends."}};
    return {};
}
// The enemy and map that bring each tip: tied to the episode, so the same enemy
// appearing elsewhere brings no tip.
inline FightTip fight_tip_for(std::string_view enemy_class, std::string_view map) {
    if (enemy_class == "BCPawn_zou01" && map == "BC-S060")
        return FightTip::elephant_gohma;
    if (enemy_class == "BCPawn_ogs02" && map == "BC-S110")
        return FightTip::augus_waves;
    return FightTip::none;
}
// Shown when Episode 1's first QTE appears, before that QTE is announced.
inline std::vector<TutorialBox> qte_note() {
    return {{"Central to the Asura's Wrath experience are many, many QTEs, all of which are "
             "announced. Be ready! You could be asked to press something, multiple somethings or "
             "do something with either stick at any time."}};
}
// Steps through a run of boxes, driven by the tutorial box's own updates.
class TutorialSequence {
  public:
    struct Action {
        enum Kind { none, open, play, finished, failed } kind = none;
        size_t box = 0;
        TutorialSound sound = TutorialSound::none;
    };
    explicit TutorialSequence(std::vector<TutorialBox> boxes) : boxes_(std::move(boxes)) {}
    const std::vector<TutorialBox> &boxes() const {
        return boxes_;
    }
    bool done() const {
        return done_;
    }
    // `paused`: the game's tutorial pause is on (the box's bUsePause flag).
    Action update(uint64_t now, bool paused) {
        if (done_)
            return {};
        if (!started_) {
            started_ = true;
            return open(0, now);
        }
        if (open_) {
            if (paused) {
                seen_paused_ = true;
                return {};
            }
            // A box that never paused the game did not open.
            if (!seen_paused_) {
                if (now - opened_ < kOpenTimeoutMs)
                    return {};
                done_ = true;
                return {Action::failed, current_};
            }
            // Confirmed: the game cleared its pause.
            open_ = false;
            const auto &box = boxes_[current_];
            wait_until_ = now + box.delay_ms;
            if (box.after != TutorialSound::none)
                return {Action::play, current_, box.after};
        }
        if (now < wait_until_)
            return {};
        if (current_ + 1 >= boxes_.size()) {
            done_ = true;
            return {Action::finished, current_};
        }
        return open(current_ + 1, now);
    }
    static constexpr uint64_t kOpenTimeoutMs = 2000;

  private:
    Action open(size_t box, uint64_t now) {
        current_ = box;
        open_ = true;
        seen_paused_ = false;
        opened_ = now;
        if (boxes_.empty()) {
            done_ = true;
            return {Action::finished};
        }
        return {Action::open, box};
    }
    std::vector<TutorialBox> boxes_;
    size_t current_ = 0;
    bool started_ = false, open_ = false, seen_paused_ = false, done_ = false;
    uint64_t opened_ = 0, wait_until_ = 0;
};
} // namespace a11y
