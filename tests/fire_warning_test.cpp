#include "../src/fire_warning.h"
#include <iostream>
#include <stdexcept>
using namespace a11y;
static void check(bool pass, const char *message) {
    if (!pass)
        throw std::runtime_error(message);
}
int main() {
    try {
        // Player at the origin facing +x; "right" is (0, 1) in this test.
        FireTracker tracker;
        check(!tracker.update({{7, 3000, 400}}, 0, 0, 0, 1, 1000).active,
              "one sighting has no movement yet");
        auto passing = tracker.update({{7, 2400, 400}}, 0, 0, 0, 1, 1500);
        check(passing.active && passing.pan > .5f,
              "fire still far ahead already pans to the side its path passes on");
        auto nearer = tracker.update({{7, 1200, 400}}, 0, 0, 0, 1, 2500);
        check(nearer.active && nearer.urgency > passing.urgency && nearer.pan > .5f,
              "urgency rises, the side stays");
        tracker.reset();
        tracker.update({{8, 3000, 0}}, 0, 0, 0, 1, 1000);
        auto straight = tracker.update({{8, 2400, 0}}, 0, 0, 0, 1, 1500);
        check(straight.active && std::abs(straight.pan) < .05f,
              "fire heading straight in is centred");
        tracker.reset();
        tracker.update({{9, 3000, 2500}}, 0, 0, 0, 1, 1000);
        check(!tracker.update({{9, 2400, 2500}}, 0, 0, 0, 1, 1500).active,
              "a path that clearly misses does not warn");
        tracker.reset();
        tracker.update({{10, 1000, 0}}, 0, 0, 0, 1, 1000);
        check(!tracker.update({{10, 1500, 0}}, 0, 0, 0, 1, 1500).active,
              "fire moving away is not a warning");
        tracker.reset();
        tracker.update({{11, 1200, -300}, {12, 4000, 300}}, 0, 0, 0, 1, 1000);
        auto two = tracker.update({{11, 600, -300}, {12, 3400, 300}}, 0, 0, 0, 1, 1500);
        check(two.active && two.pan < -.3f, "the soonest arrival wins, here on the left");
        tracker.reset();
        tracker.update({{13, 14000, 0}}, 0, 0, 0, 1, 1000);
        check(!tracker.update({{13, 13000, 0}}, 0, 0, 0, 1, 1500).active,
              "fire beyond range is ignored");
        tracker.reset();
        tracker.update({{14, 2000, 0}}, 500, 0, 0, 1, 1000);
        auto moving = tracker.update({{14, 2000, 0}}, 1000, 0, 0, 1, 1500);
        check(moving.active, "the player running at stationary fire is a threat too");
        check(!tracker.update({}, 0, 0, 0, 1, 2000).active, "no fire, no warning");

        FireSynth synth;
        float out[2 * 4800]{};
        synth.render({}, .3f, out, 4800, 10000);
        float silent = 0;
        for (float v : out)
            silent = std::max(silent, std::abs(v));
        check(silent == 0, "an inactive cue is silent");
        FireCue cue{true, 1.f, 1.f, 10000};
        synth.render(cue, .3f, out, 4800, 10000);
        float left = 0, right = 0;
        for (int i = 2400; i < 4800; ++i) {
            left = std::max(left, std::abs(out[i * 2]));
            right = std::max(right, std::abs(out[i * 2 + 1]));
        }
        check(right > .05f && left < right * .1f, "a right-side warning buzzes on the right");
        synth.render(cue, .3f, out, 4800, 10500);
        float stale = 0;
        for (int i = 4000; i < 4800; ++i)
            stale = std::max(stale, std::abs(out[i * 2 + 1]));
        check(stale < right, "a stale cue fades out");
        std::cout << "Fire warning tests passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
