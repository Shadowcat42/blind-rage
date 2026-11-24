#include "../src/mod_tutorials.h"
#include <iostream>
#include <stdexcept>
using namespace a11y;
static void check(bool pass, const char *message) {
    if (!pass)
        throw std::runtime_error(message);
}
static bool contains(const std::string &text, std::string_view part) {
    return text.find(part) != std::string::npos;
}
int main() {
    try {
        auto ps = rail_shooting_tutorial({});
        check(ps.size() == 5, "five rail-shooting boxes");
        check(contains(ps[0].text, "dodge with cross") && contains(ps[0].text, "holding square") &&
                  contains(ps[0].text, ". Triangle, though slower"),
              "PlayStation names, capitalized only where a name starts a sentence");
        check(contains(ps[0].text, "locked... more on this next."),
              "the player's own wording is not recapitalized");
        check(contains(ps[4].text, "Pressing up on the D-pad") &&
                  contains(ps[4].text, ". Left or right on the D-pad will change") &&
                  contains(ps[4].text, "Hold L1 for auto aim"),
              "D-pad and L1 names");
        check(ps[1].after == TutorialSound::lock_tone &&
                  ps[2].after == TutorialSound::full_lock_tone &&
                  ps[3].after == TutorialSound::beacon && ps[0].after == TutorialSound::none &&
                  ps[4].after == TutorialSound::none,
              "each demonstrated sound follows its box");
        TutorialLabels keys;
        keys.cross = "space", keys.square = "K", keys.triangle = "L", keys.l1 = "Q";
        keys.dpad_up = "up arrow", keys.dpad_left_right = "left arrow or right arrow";
        auto kb = rail_shooting_tutorial(keys);
        check(contains(kb[0].text, "dodge with space") &&
                  contains(kb[0].text, ". L, though slower") &&
                  contains(kb[4].text, "Pressing up arrow") &&
                  contains(kb[4].text, ". Left arrow or right arrow will change") &&
                  contains(kb[4].text, "Hold Q for auto aim"),
              "keyboard bindings replace the controller names");
        for (const auto &box : kb)
            check(!contains(box.text, "{"), "every name is filled in");

        auto note = qte_note();
        check(note.size() == 1 && note[0].after == TutorialSound::none &&
                  note[0].text.starts_with("Central to the Asura's Wrath experience") &&
                  contains(note[0].text, "Be ready!"),
              "one QTE note box with the agreed wording");
        TutorialSequence run({{"one"}, {"two", TutorialSound::lock_tone, 900}, {"three"}});
        using Kind = TutorialSequence::Action::Kind;
        auto a = run.update(1000, false);
        check(a.kind == Kind::open && a.box == 0, "the first box opens at once");
        check(run.update(1010, false).kind == Kind::none,
              "a box that has not paused the game yet is not taken as confirmed");
        check(run.update(1020, true).kind == Kind::none, "waits while the box pauses the game");
        a = run.update(1030, false);
        check(a.kind == Kind::open && a.box == 1,
              "confirming a box without a sound opens the next");
        run.update(1040, true);
        a = run.update(2000, false);
        check(a.kind == Kind::play && a.sound == TutorialSound::lock_tone && a.box == 1,
              "confirming plays that box's sound");
        check(run.update(2500, false).kind == Kind::none, "the next box waits for the sound");
        a = run.update(2900, false);
        check(a.kind == Kind::open && a.box == 2, "then the next box opens");
        run.update(2910, true);
        a = run.update(3000, false);
        check(a.kind == Kind::finished && run.done(), "confirming the last box finishes the run");
        check(run.update(4000, false).kind == Kind::none, "a finished run stays quiet");

        TutorialSequence stuck({{"one"}});
        stuck.update(0, false);
        check(stuck.update(1000, false).kind == Kind::none, "allows time for the box to open");
        check(stuck.update(TutorialSequence::kOpenTimeoutMs, false).kind == Kind::failed,
              "a box that never pauses the game fails the run");
        std::cout << "Mod tutorial tests passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
