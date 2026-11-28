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

        auto chase = chase_tutorial({});
        check(chase.size() == 3 && chase[1].after == TutorialSound::fire_warning &&
                  contains(chase[0].text, "Hold square to fire") &&
                  contains(chase[1].text, "dodge with cross") &&
                  contains(chase[2].text, "hit it with circle") &&
                  contains(chase[2].text, "press R2") && contains(chase[2].text, "Hold L1"),
              "Episode 8 chase boxes with the warning demonstrated after the second");
        for (const auto &box : chase)
            check(!contains(box.text, "{"), "every chase name is filled in");
        auto melee = melee_tutorial({});
        check(melee.size() == 4 && melee[0].text.starts_with("At last, you get to punching!") &&
                  contains(melee[0].text, "Press circle to attack") &&
                  contains(melee[1].text, "Press L1 to lock on") &&
                  contains(melee[2].text, "Hold circle for a dash attack") &&
                  contains(melee[3].text, "Press up on the D-pad to turn the beacon off"),
              "Episode 2 melee boxes with the agreed wording");
        for (const auto &box : melee)
            check(!contains(box.text, "{") && box.after == TutorialSound::none,
                  "every melee name is filled in and no box plays a demo");
        auto tip = fight_tip(fight_tip_for("BCPawn_zou01", "BC-S060"));
        check(tip.size() == 1 &&
                  tip[0].text.starts_with("This giant elephant Gohma can't just be beaten down.") &&
                  tip[0].text.ends_with("Good luck.") && tip[0].after == TutorialSound::none,
              "the elephant Gohma tip, with the agreed wording");
        check(fight_tip_for("BCPawn_zou02", "BC-S060") == FightTip::none &&
                  fight_tip(FightTip::none).empty(),
              "no tip for the section 3 Gohma or other enemies");
        check(fight_tip_for("BCPawn_zou01", "BC-S110") == FightTip::none,
              "the same enemy in another episode brings no tip");
        auto augus = fight_tip(fight_tip_for("BCPawn_ogs02", "BC-S110"));
        check(augus.size() == 1 &&
                  augus[0].text.starts_with("Augus has decided he wants to become a zoner") &&
                  augus[0].text.ends_with("jump when the tone ends."),
              "the Augus sword-wave tip, with the agreed wording");
        check(fight_tip_for("BCPawn_ogs01", "BC-S110") == FightTip::none,
              "Augus's first section brings no tip");
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
