#include "../src/wave_guide.h"
#include <iostream>
#include <stdexcept>
using namespace a11y;
static void check(bool pass, const char *message) {
    if (!pass)
        throw std::runtime_error(message);
}
// A fan of seven blades fired from (x0, 0) toward -x at 3000 units/s, at angles
// 0, +/-0.2, +/-0.4, +/-0.6 rad from straight at the origin.
static std::vector<WaveSample> fan(float x0, float t, float tilt = 0) {
    std::vector<WaveSample> out;
    const float angles[]{0, .2f, -.2f, .4f, -.4f, .6f, -.6f};
    uint32_t id = 1;
    for (auto a : angles) {
        float d = 3000.f * t;
        out.push_back(
            {id++, x0 - d * std::cos(a + tilt), -d * std::sin(a + tilt), false, 0, 0, 50});
    }
    return out;
}
int main() {
    try {
        // Camera faces the waves (+x); its right is +y.
        {
            WaveGuide guide;
            guide.update(fan(5000, 0), 0, 0, 0, 1, 1000);
            auto cues = guide.update(fan(5000, .1f), 0, 0, 0, 1, 1100);
            check(cues.step.active && std::abs(std::abs(cues.step.pan) - 1) < 1e-4f &&
                      cues.step.urgency > 0 && cues.step.urgency < 1,
                  "the straight-on blade hits: one buzz, panned to a side");
            // The same tick again (the ms clock hasn't advanced): the buzz holds.
            auto again = guide.update(fan(5000, .1f), 0, 0, 0, 1, 1100);
            check(again.step.active && again.step.pan == cues.step.pan,
                  "a frame with no time passed repeats the warning instead of dropping it");
            // Stepping that way by half a gap leaves every blade's path.
            float y = cues.step.pan * 300.f;
            WaveGuide moved;
            moved.update(fan(5000, 0), 0, y, 0, 1, 1000);
            check(!moved.update(fan(5000, .1f), 0, y, 0, 1, 1100).step.active,
                  "standing in a gap is silent");
        }
        {
            // Uneven fan: tilted so the player sits near one blade with a wide gap on one side.
            WaveGuide guide;
            guide.update(fan(5000, 0, .1f), 0, -480, 0, 1, 1000);
            auto cues = guide.update(fan(5000, .1f, .1f), 0, -480, 0, 1, 1100);
            check(!cues.step.active || std::abs(cues.step.pan) == 1.f,
                  "any warning is a clear left or right");
        }
        {
            // A wide horizontal wave 3000 units away at 3000 units/s: jump 0.75 s later.
            WaveGuide guide;
            guide.update({{9, 3300, 50, true, 0, 0, 1500}}, 0, 0, 0, 1, 1000);
            auto cues = guide.update({{9, 3000, 50, true, 0, 0, 1500}}, 0, 0, 0, 1, 1100);
            check(cues.jump.active && !cues.step.active &&
                      std::abs(double(cues.jump.at) - (1100 + 400)) < 5,
                  "a wide wave brings the jump cue, ending 0.6 s before it arrives");
            WaveGuide wide;
            wide.update({{9, 3300, 4000, true, 0, 0, 1500}}, 0, 0, 0, 1, 1000);
            check(!wide.update({{9, 3000, 4000, true, 0, 0, 1500}}, 0, 0, 0, 1, 1100).jump.active,
                  "a wide wave passing far to the side brings no jump cue");
        }
        {
            // A blade whose hit cylinder sits 350 units off its origin (in world y):
            WaveGuide guide;
            WaveSample a{1, 5000, -350, false, 0, 350, 50}, b = a;
            b.x -= 300;
            guide.update({a}, 0, 0, 0, 1, 1000);
            auto cues = guide.update({b}, 0, 0, 0, 1, 1100);
            check(cues.step.active, "the offset hit cylinder, not the origin, decides a hit");
            WaveSample c{2, 5000, 0, false, 0, 350, 50}, d = c;
            d.x -= 300;
            WaveGuide clear;
            clear.update({c}, 0, 0, 0, 1, 1000);
            check(!clear.update({d}, 0, 0, 0, 1, 1100).step.active,
                  "an origin aimed at the player with its cylinder 350 aside passes by");
        }
        {
            // Two wide waves 0.3 s apart share one jump cue; a wave already too close
            // for the sweep gets none.
            WaveGuide guide;
            WaveSample a{20, 3300, 0, true, 0, 0, 1375}, b{21, 4200, 0, true, 0, 0, 1375};
            guide.update({a, b}, 0, 0, 0, 1, 1000);
            a.x -= 300, b.x -= 300;
            auto first = guide.update({a, b}, 0, 0, 0, 1, 1100);
            check(first.jump.active && first.jump.generation == 20, "the nearer wave is cued");
            a.x -= 300, b.x -= 300;
            auto second = guide.update({a, b}, 0, 0, 0, 1, 1200);
            check(second.jump.generation == 20, "a wave 0.3 s behind shares the cue");
            // Once its sweep has played, the same wave is never cued again, however
            // long it keeps coming.
            WaveGuide once;
            WaveSample w{40, 4500, 0, true, 0, 0, 1375};
            int sweeps = 0;
            uint64_t lastAt = 0;
            for (uint64_t now = 1000; now < 2500; now += 16) {
                auto c = once.update({w}, 0, 0, 0, 1, now);
                w.x -= 3000.f * 0.016f;
                if (c.jump.active && c.jump.at != lastAt && now >= lastAt) {
                    if (!lastAt || now > lastAt)
                        ++sweeps;
                    lastAt = c.jump.at;
                }
            }
            check(sweeps == 1, "one sweep per wave, not one every 200 ms");
            WaveGuide late;
            WaveSample c{30, 600, 0, true, 0, 0, 1375};
            late.update({c}, 0, 0, 0, 1, 1000);
            c.x -= 300;
            check(!late.update({c}, 0, 0, 0, 1, 1100).jump.active,
                  "a wave 0.1 s away gets no cue it can't use");
        }
        {
            // Augus's swings: shots at 0.729 s (fan) and 0.746 s (wide wave).
            std::vector<Shot> fan{{.729f, false}}, wide{{.746f, true, 3000, 1375}};
            float seconds = 0;
            check(next_shot(fan, .2f, 1, seconds) == 0 && std::abs(seconds - .529f) < 1e-4f,
                  "the next shot is timed from the animation");
            check(next_shot(fan, .8f, 1, seconds) < 0, "a shot already fired is not next");
            // A fan wind-up 600 units away buzzes before any blade exists.
            WaveGuide guide;
            guide.update({}, 0, 0, 0, 1, 1000);
            WindUp swing{0x80000001, 600, 0, .5f, fan[0]};
            auto early = guide.update({}, 0, 0, 0, 1, 1016, {swing});
            check(early.step.active && early.step.pan == 1.f, "a fan wind-up buzzes early");
            // A wide wind-up 600 units away: arrival 0.5 + 0.2 s, the jump 0.6 s before.
            WaveGuide jumper;
            jumper.update({}, 0, 0, 0, 1, 1000);
            WindUp sweep{0x80000002, 600, 0, .5f, wide[0]};
            auto cue = jumper.update({}, 0, 0, 0, 1, 1016, {sweep});
            check(cue.jump.active && cue.jump.at >= 1016 + 150 && cue.jump.at <= 1016 + 200,
                  "a wide wind-up cues the jump from the predicted arrival");
            // The real wave, fired at 1516 and due at 1716, gets no second sweep.
            WaveSample real{40, 450, 0, true, 0, 0, 1375};
            jumper.update({real}, 0, 0, 0, 1, 1566);
            real.x -= 3000 * .016f;
            auto after = jumper.update({real}, 0, 0, 0, 1, 1582);
            check(!after.jump.active, "the spawned wave shares the wind-up's sweep");
        }
        {
            JumpSynth synth;
            JumpCue cue{true, 2000, 9};
            std::vector<float> out(48000 * 2);
            synth.render(cue, .2f, out.data(), 48000, 1500); // 1.5 s to 2.5 s
            auto energy = [&](double from, double to) {
                double e = 0;
                for (auto i = size_t(from * 48000); i < size_t(to * 48000); ++i)
                    e += out[i * 2] * out[i * 2];
                return e;
            };
            auto crossings = [&](double from, double to) {
                int n = 0;
                for (auto i = size_t(from * 48000) + 1; i < size_t(to * 48000); ++i)
                    n += (out[i * 2 - 2] < 0) != (out[i * 2] < 0);
                return n;
            };
            check(energy(0, .34) == 0 && energy(.36, .49) > 1 && energy(.51, 1) == 0,
                  "the sweep plays only in the 150 ms before the jump moment");
            check(crossings(.43, .49) > crossings(.36, .42), "the sweep rises in pitch");
        }
        std::cout << "Wave guide tests passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
