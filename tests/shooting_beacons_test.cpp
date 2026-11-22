#include "../src/shooting_beacons.h"
#include <iostream>
#include <stdexcept>
using namespace a11y;
static void check(bool value, const char *why) {
    if (!value)
        throw std::runtime_error(why);
}
static LockVector direction(float yaw, float pitch = 0) {
    yaw *= .0174532925f;
    pitch *= .0174532925f;
    return {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
}
int main() {
    try {
        check(shooting_mode("", 1) == ShootingMode::boosted &&
                  shooting_mode("", 0) == ShootingMode::off &&
                  shooting_mode("") == ShootingMode::beacons &&
                  shooting_mode("beacons", 1) == ShootingMode::beacons &&
                  shooting_mode("autoaim") == ShootingMode::boosted &&
                  shooting_mode("native") == ShootingMode::off &&
                  shooting_mode("tracking") == ShootingMode::beacons &&
                  shooting_mode("bogus", 1) == ShootingMode::boosted &&
                  shooting_mode("bogus") == ShootingMode::beacons &&
                  shooting_mode_recognized("off") && !shooting_mode_recognized("bogus"),
              "configuration migration preserves legacy choices and fails safely");
        check(next_shooting_mode(ShootingMode::off) == ShootingMode::beacons &&
                  next_shooting_mode(ShootingMode::beacons) == ShootingMode::boosted &&
                  next_shooting_mode(ShootingMode::boosted) == ShootingMode::off &&
                  !shooting_assisted(ShootingMode::off) && shooting_assisted(ShootingMode::boosted),
              "modes cycle off, beacons, boosted; tracking is L1 in every mode");
        {
            AimSnapshot shot;
            shot.valid = shot.active = true;
            shot.aim = {1, 0, 0};
            shot.camera_up = {0, 0, 1};
            shot.maximum = 8;
            auto at = [](float yaw, float pitch) {
                auto y = yaw * 3.14159265f / 180.f, p = pitch * 3.14159265f / 180.f;
                return LockVector{std::cos(p) * std::cos(y), std::cos(p) * std::sin(y),
                                  std::sin(p)};
            };
            shot.targets[0] = {{0x100, 1}, at(20, 0), false, 1000};
            shot.targets[1] = {{0x200, 2}, at(-4, 2), false, 1000};
            shot.targets[2] = {{0x300, 3}, at(1, 0), true, 1000};
            shot.count = 3;
            auto nudge = tracking_nudge(shot, {}, 1.f);
            check(nudge && nudge->target == AimIdentity{0x200, 2} && nudge->right < 0 &&
                      nudge->up > 0 && std::abs(std::hypot(nudge->right, nudge->up) - 1) < .01f &&
                      std::abs(nudge->right / nudge->up + 2) < .05f,
                  "tracking picks the nearest unlocked target and steers straight at full speed");
            nudge = tracking_nudge(shot, {0x100, 1}, .5f);
            check(nudge && nudge->target == AimIdentity{0x100, 1} && nudge->right == .5f &&
                      nudge->up == 0,
                  "tracking keeps its current target and caps deflection at the strength");
            nudge = tracking_nudge(shot, {0x200, 2}, 1.f, {{0x100, 1}});
            check(nudge && nudge->target == AimIdentity{0x100, 1} && nudge->right == 1.f,
                  "tracking follows the beacon's cluster over a nearer target");
            shot.targets[0].locked = true;
            nudge = tracking_nudge(shot, {}, 1.f, {{0x100, 1}});
            check(nudge && nudge->target == AimIdentity{0x200, 2},
                  "a fully locked beacon cluster falls back to the nearest target");
            shot.targets[0].locked = false;
            shot.targets[1].direction = at(.2f, -.1f);
            nudge = tracking_nudge(shot, {0x200, 2}, 1.f);
            check(nudge && nudge->right == 0 && nudge->up == 0,
                  "tracking settles without jitter once aligned");
            shot.locks = 8;
            check(!tracking_nudge(shot, {}, 1.f), "tracking stops at a full volley");
            shot.locks = 0;
            shot.targets[0].locked = shot.targets[1].locked = true;
            check(!tracking_nudge(shot, {}, 1.f), "locked targets are not tracked");
            shot.targets[0].locked = shot.targets[1].locked = false;
            shot.targets[1].reachable = false;
            nudge = tracking_nudge(shot, {0x200, 2}, 1.f);
            check(nudge && nudge->target == AimIdentity{0x100, 1},
                  "tracking skips a target the cursor cannot reach, even its current one");
            shot.targets[1].reachable = true;
        }
        {
            // The Episode 1 projection seen in captures: forward +X, right +Y, up +Z,
            // 1280x720 and an 85% title-safe frame.
            AimFrame frame;
            frame.clip_x = 1280;
            frame.clip_y = 720;
            frame.min_x = 1280 * .075f;
            frame.max_x = 1280 - frame.min_x;
            frame.min_y = 720 * .075f;
            frame.max_y = 720 - frame.min_y;
            frame.matrix = {0, 0, .999f, 1, 2.4142f, 0, 0, 0, 0, 4.2919f, 0, 0, 0, 0, -9.99f, 0};
            check(frame.contains({1000, 0, 0}), "a target straight ahead is reachable");
            check(frame.contains({1000, 350, 0}) && !frame.contains({1000, 400, 0}),
                  "the frame edge follows the title-safe width");
            check(frame.contains({1000, 0, 195}) && !frame.contains({1000, 0, 210}),
                  "the frame edge follows the title-safe height");
            check(!frame.contains({-1000, 0, 0}), "a target behind the camera is not reachable");
        }
        AudioRetry retry;
        check(retry.due(0), "first audio start is immediate");
        retry.failure(100);
        check(!retry.due(1099) && retry.due(1100), "first retry waits one second");
        retry.failure(1100);
        check(!retry.due(3099) && retry.due(3100), "backoff doubles");
        for (int n = 0; n < 8; ++n)
            retry.failure(10000);
        check(!retry.due(39999) && retry.due(40000), "backoff caps at thirty seconds");
        retry.success();
        check(retry.due(0), "success clears the backoff");
        AimSnapshot frame;
        frame.controller = {1, 1};
        frame.pawn = {2, 2};
        frame.active = frame.valid = true;
        frame.aim = {1, 0, 0};
        frame.camera_up = {0, 0, 1};
        frame.maximum = 4;
        frame.tick = 100;
        frame.count = 3;
        frame.targets[0] = {{10, 1}, direction(-15), false, 1000.f};
        frame.targets[1] = {{11, 1}, direction(-17), false, 1000.f};
        frame.targets[2] = {{20, 1}, direction(25), false, 1000.f};
        {
            // The beacon applies the same reachability as tracking: off-frame,
            // out-of-range and game-declined targets are never guided to.
            AimSnapshot lone = frame;
            lone.count = 1;
            lone.targets[0].reachable = false;
            ShootingBeacons only;
            check(!only.update(lone).active && only.clusters() == 0,
                  "the beacon never guides to an unreachable target");
            lone.targets[0].reachable = true;
            check(only.update(lone).active, "the same target is guided once reachable");
        }
        ShootingBeacons beacons;
        auto cue = beacons.update(frame);
        check(cue.active && cue.pan < 0 && beacons.clusters() == 2,
              "initial guidance picks one nearby angular cluster");
        auto generation = cue.generation;
        frame.targets[frame.count++] = {{30, 1}, direction(0), false, 1000.f};
        cue = beacons.update(frame);
        check(cue.pan < 0 && cue.generation == generation,
              "closer arrivals do not steal the active cluster");
        frame.targets[0].locked = true;
        cue = beacons.update(frame);
        check(cue.pan < -.5f, "guide remaining unlocked members before advancing");
        frame.targets[1].locked = true;
        cue = beacons.update(frame);
        check(std::abs(cue.pan) < .01f && cue.generation != generation,
              "automatically advance only when the cluster is acquired");
        cue = beacons.update(frame, 1);
        check(cue.pan > .5f, "next cluster control follows screen order");
        cue = beacons.update(frame, -1);
        check(std::abs(cue.pan) < .01f, "previous cluster control returns in screen order");
        frame.locks = 4;
        check(!beacons.update(frame).active, "native capacity silences guidance");
        frame.locks = 3;
        check(!beacons.update(frame).active,
              "partial angular loss does not resume a full-volley beacon");
        ++frame.fired;
        check(beacons.update(frame).active, "actual firing resumes guidance");
        frame.active = false;
        check(!beacons.update(frame).active, "pause or cinematic cancels guidance");
        frame.active = true;
        frame.valid = false;
        check(!beacons.update(frame).active, "unsupported capture is silent");
        frame.valid = true;
        frame.count = 0;
        check(!beacons.update(frame).active, "empty screen has no invented target");
        frame.count = 1;
        frame.targets[0] = {{10, 2}, direction(0, 15), false, 1000.f};
        cue = beacons.update(frame);
        check(cue.active && cue.elevation > 0,
              "reused object identity is fresh and above is higher pitch");
        frame.aim = direction(20);
        frame.targets[0].direction = direction(20);
        cue = beacons.update(frame);
        check(std::abs(cue.pan) < .001f && cue.error < .01f,
              "feedback is relative to aim rather than camera center");
        frame.camera_up = {0, 1, 0};
        frame.aim = {1, 0, 0};
        frame.targets[0].direction = {0.98f, 0, .2f};
        cue = beacons.update(frame);
        check(cue.pan < 0, "rolled camera basis preserves screen-relative direction");
        {
            ShootingBeacons sparse;
            AimSnapshot sample = frame;
            sample.aim = {1, 0, 0};
            sample.camera_up = {0, 0, 1};
            sample.count = 2;
            sample.targets[0] = {{1, 1}, direction(10), false, 1000.f};
            sample.targets[1] = {{2, 1}, direction(17.8f), false, 1000.f};
            auto target = sparse.update(sample);
            check(sparse.clusters() == 1 && std::abs(target.error - 10.f) < .01f,
                  "sparse clusters guide an actual member rather than an unacquirable midpoint");
        }
        {
            AimSnapshot depth = frame;
            depth.aim = {1, 0, 0};
            depth.camera_up = {0, 0, 1};
            depth.count = 2;
            depth.targets[0] = {{1, 1}, direction(0), false, 3000};
            depth.targets[1] = {{2, 1}, direction(.4f), false, 1000};
            ShootingBeacons layers;
            auto near = layers.update(depth);
            check(layers.clusters() == 2 && std::abs(near.error - .4f) < .01f,
                  "almost-overlapping targets 2000 units apart split and favor the foreground");
            auto far = layers.update(depth, 1);
            check(far.error < .01f,
                  "manual cycling can intentionally select a farther depth layer");
            check(layers.update(depth).error < .01f,
                  "explicit far-layer selection is not immediately overridden");
            ShootingBeacons arrival;
            depth.count = 1;
            arrival.update(depth);
            depth.count = 2;
            check(std::abs(arrival.update(depth).error - .4f) < .01f,
                  "automatic guidance yields when a clearly nearer overlapping target arrives");
            ShootingBeacons jitter;
            depth.count = 1;
            jitter.update(depth);
            depth.count = 2;
            depth.targets[1].distance = 2850;
            check(jitter.update(depth).error < .01f,
                  "small depth changes do not steal existing focus");
            depth.count = 3;
            depth.targets[0] = {{1, 1}, direction(0), false, 1000};
            depth.targets[1] = {{2, 1}, direction(0), false, 1240};
            depth.targets[2] = {{3, 1}, direction(0), false, 1500};
            ShootingBeacons chain;
            chain.update(depth);
            check(chain.clusters() == 2,
                  "depth neighborhoods cannot chain into one large range span");
            depth.count = 1;
            depth.targets[0].distance = std::numeric_limits<float>::quiet_NaN();
            check(!chain.update(depth).active, "unknown distance cannot participate in guidance");
            depth.targets[0].distance = 0;
            check(!chain.update(depth).active, "zero distance is invalid");
        }
        AimMailbox mailbox;
        mailbox.put(frame);
        frame.tick = 200;
        mailbox.put(frame);
        check(mailbox.take()->tick == 200 && !mailbox.take(),
              "latest frame replaces older data without narration queue overflow");
        std::vector<uint8_t> data(0x20000);
        auto put = [&](uint32_t at, uint32_t value) {
            for (unsigned i = 0; i < 4; ++i)
                data[at + i] = uint8_t(value >> (24 - 8 * i));
        };
        auto real = [&](uint32_t at, float value) { put(at, std::bit_cast<uint32_t>(value)); };
        Memory memory([&](uint32_t at, void *out, size_t n) {
            if (uint64_t(at) + n > data.size())
                return false;
            memcpy(out, data.data() + at, n);
            return true;
        });
        constexpr uint32_t pc = 0x1000, pawn = 0x2000, world = 0x3000, hud = 0x4000,
                           camera = 0x5000, target = 0x6000, locals = 0x9000;
        put(pc + 0x20, 1);
        put(pc + 0x5c4, pawn);
        put(pawn + 0x20, 2);
        put(pc + 0xd4, world);
        put(pc + 0x3c8, hud);
        put(pc + 0x370, camera);
        real(camera + 0x324, 80);
        put(pc + 0x7bc, 4);
        real(locals + 0x28, 1);
        put(locals + 0x50, 0xa000);
        put(locals + 0x54, 1);
        put(locals + 0x58, 1);
        put(0xa000, target);
        put(target + 0x20, 4);
        put(target + 0x1e4, 0x40000000);
        real(target + 0xc0, 1000);
        real(target + 0xc4, 100);
        auto snapshot = capture_aim(memory, pc, locals, 3, 100);
        check(snapshot.active && snapshot.valid && snapshot.count == 1 &&
                  !snapshot.targets[0].locked,
              "native candidate capture copies finite enabled in-view components");
        check(std::abs(snapshot.targets[0].distance - std::sqrt(1010000.f)) < .01f,
              "snapshot retains real camera-to-target distance before normalization");
        real(camera + 0x30c, 100);
        real(locals + 0x1c, 100);
        auto moved = capture_aim(memory, pc, locals, 3, 100);
        check(moved.valid && std::abs(moved.targets[0].distance - std::sqrt(820000.f)) < .01f,
              "camera motion updates depth without moving targets or reticle");
        real(camera + 0x30c, 0);
        real(locals + 0x1c, 0);
        put(pc + 0x7f0, 0xb000);
        put(pc + 0x7f4, 1);
        put(pc + 0x7f8, 1);
        put(0xb000, target);
        put(0xb004, 1);
        snapshot = capture_aim(memory, pc, locals, 3, 100);
        check(snapshot.valid && snapshot.targets[0].locked && snapshot.locks == 0,
              "in-flight targets are skipped without counting against the next volley");
        put(world + 0x368, 1);
        check(!capture_aim(memory, pc, locals, 3, 100).active, "paused world blocks capture");
        put(world + 0x368, 0);
        put(locals + 0x54, 513);
        put(locals + 0x58, 513);
        check(!capture_aim(memory, pc, locals, 3, 100).valid,
              "oversized lists fail closed without truncating a cluster");
        put(locals + 0x54, 1);
        put(locals + 0x58, 1);
        real(target + 0xc0, -1000);
        check(capture_aim(memory, pc, locals, 3, 100).count == 0,
              "behind-camera candidates are not guided");
        BeaconSynth synth;
        std::array<float, 9600> samples{};
        BeaconCue tone{true, -1, 0, 10, 1, 100};
        synth.render(tone, .2f, samples.data(), 4800, 100);
        double left = 0, right = 0;
        for (unsigned i = 0; i < samples.size(); i += 2) {
            left += std::abs(samples[i]);
            right += std::abs(samples[i + 1]);
        }
        check(left > 1 && right == 0,
              "left beacon produces only the left channel at the pan endpoint");
        {
            BeaconSynth triangle;
            BeaconCue aligned{true, -1, 0, 0, 1, 100};
            triangle.render(aligned, .2f, samples.data(), 4800, 100);
            triangle.render(aligned, .2f, samples.data(), 4800, 100);
            auto harmonic = [&](unsigned multiple) {
                double real = 0, imaginary = 0;
                for (unsigned i = 0; i < 4800; ++i) {
                    auto angle = 6.283185307179586 * 700 * multiple * i / 48000.;
                    real += samples[i * 2] * std::cos(angle);
                    imaginary += samples[i * 2] * std::sin(angle);
                }
                return std::hypot(real, imaginary);
            };
            auto fundamental = harmonic(1);
            check(std::abs(harmonic(3) / fundamental - 1. / 9.) < .005 &&
                      harmonic(2) / fundamental < .005,
                  "triangle has the expected odd harmonics rather than a sine or square spectrum");
            for (unsigned block = 0; block < 10; ++block) {
                double energy = 0;
                for (unsigned i = block * 480; i < (block + 1) * 480; ++i)
                    energy += samples[i * 2] * samples[i * 2];
                check(energy > 1, "aligned beacon has no silent pulse gaps");
            }
            check(std::all_of(samples.begin(), samples.end(),
                              [](float v) { return std::abs(v) <= .2f; }),
                  "triangle preserves the configured peak level");
            BeaconSynth distant;
            aligned.error = 45;
            std::vector<float> sweep(48000);
            distant.render(aligned, .2f, sweep.data(), 24000, 100);
            unsigned starts = 0;
            bool previous = false;
            for (unsigned block = 0; block < 50; ++block) {
                double energy = 0;
                for (unsigned i = block * 480; i < (block + 1) * 480; ++i)
                    energy += sweep[i * 2] * sweep[i * 2];
                bool sounding = energy > .1;
                starts += sounding && !previous;
                previous = sounding;
            }
            check(starts == 2, "far beacon repeats within 350 ms instead of the old 660 ms gap");
            aligned.error = 6;
            distant.render(aligned, .2f, samples.data(), 4800, 100);
            distant.render(aligned, .2f, samples.data(), 4800, 100);
            for (unsigned block = 0; block < 10; ++block) {
                double energy = 0;
                for (unsigned i = block * 480; i < (block + 1) * 480; ++i)
                    energy += samples[i * 2] * samples[i * 2];
                check(energy > .1,
                      "near-aligned pulse gaps fill before the fully continuous range");
            }
        }
        synth.render(tone, .2f, samples.data(), 4800, 500);
        check(std::all_of(samples.end() - 960, samples.end(), [](float v) { return v == 0; }),
              "stale frame fades to silence without reading game memory");
        tone.pan = std::numeric_limits<float>::quiet_NaN();
        synth.render(tone, .2f, samples.data(), 4800, 100);
        check(std::all_of(samples.begin(), samples.end(), [](float v) { return std::isfinite(v); }),
              "invalid audio parameters never submit NaNs");
        std::cout
            << "Manual shooting capture, clustering, capacity and stereo synthesis tests passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
