#include "../src/walk_beacon.h"
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
        {
            // Melee rapid fire: projectiles from the map cache and enemy pawns.
            constexpr uint32_t map = 0xc000, list = 0xc100, enemyShot = 0xd000, ownShot = 0xd400,
                               laser = 0xd800, enemy = 0xe000, ally = 0xe800;
            put(pawn + 0x454, 0);
            check(!melee_rapid_fire(memory, pc), "melee capture needs aimed fire");
            data[pawn + 0x454] = 24;
            check(melee_rapid_fire(memory, pc), "attack type 24 is rapid fire");
            real(pc + 0x6b8, 100); // AimShotAimPoint straight ahead of the camera.
            put(world + 0x4ac, map);
            put(map + 0xc0, list);
            put(map + 0xc4, 3);
            put(map + 0xc8, 3);
            put(list, enemyShot);
            put(list + 4, ownShot);
            put(list + 8, laser);
            for (auto actor : {enemyShot, ownShot, laser, enemy, ally})
                put(actor + 0x20, 7);
            real(enemyShot + 0x54, 1000);
            real(enemyShot + 0x58, 30);
            put(ownShot + 0x98, pawn);
            real(ownShot + 0x54, 1000);
            real(laser + 0x54, 1000);
            put(world + 0x3d8, enemy);
            put(enemy + 0x1e8, ally);
            put(ally + 0x1e8, pawn);
            put(enemy + 0x2d8, 100);
            put(ally + 0x2d8, 100);
            put(pawn + 0x2d8, 100);
            data[enemy + 0x468] = 1;
            real(enemy + 0x54, 800);
            real(enemy + 0x58, -200);
            real(ally + 0x54, 800);
            auto classes = [&](uint32_t object, std::string_view cls) {
                if (cls == "BCProj_Laser_Base")
                    return object == laser;
                if (cls == "BCPawn")
                    return object == enemy || object == ally || object == pawn;
                return false;
            };
            auto melee =
                capture_melee_aim(memory, pc, 100, AimPriority::projectiles, 2.56f, classes);
            check(melee.active && melee.valid && melee.count == 2 && melee.window_degrees == 2.56f,
                  "melee capture keeps enemy shots and enemies, not own shots, lasers or allies");
            check(melee.targets[0].projectile && melee.targets[0].reachable &&
                      !melee.targets[1].projectile && !melee.targets[1].reachable,
                  "projectiles first: enemies wait while a projectile is reachable");
            melee = capture_melee_aim(memory, pc, 100, AimPriority::enemies, 2.56f, classes);
            check(!melee.targets[0].reachable && melee.targets[1].reachable,
                  "enemies first: projectiles wait while an enemy is reachable");
            put(enemy + 0x2d8, 0);
            melee = capture_melee_aim(memory, pc, 100, AimPriority::enemies, 2.56f, classes);
            check(melee.count == 1 && melee.targets[0].reachable,
                  "with no preferred kind left, the other kind is guided");
            put(enemy + 0x2d8, 100);
            put(enemyShot + 0x298, 0x1000000);
            check(capture_melee_aim(memory, pc, 100, AimPriority::projectiles, 2.56f, classes)
                          .count == 1,
                  "exploded projectiles are not targets");
            put(enemyShot + 0x298, 0);
            ShootingBeacons meleeBeacons;
            melee = capture_melee_aim(memory, pc, 100, AimPriority::projectiles, 2.56f, classes);
            auto cue = meleeBeacons.update(melee);
            check(cue.active && meleeBeacons.guiding().pointer == enemyShot,
                  "beacon guides to the reachable projectile");
            auto plain = melee;
            plain.window_degrees = 0;
            ShootingBeacons railStyle;
            auto unscaled = railStyle.update(plain);
            check(std::abs(cue.error - unscaled.error * 4.f / 2.56f) < .01f,
                  "beacon turns continuous at the game's homing angle");
            data[pawn + 0x454] = 0;
            {
                // Walking: nearest touch trigger not yet entered, nested sequences
                // included, quiet while an enemy is alive.
                constexpr uint32_t level = 0xf000, seqs = 0xf100, seq = 0xf200, items = 0xf300,
                                   fired = 0xf400, ahead = 0xf500, nested = 0xf600,
                                   nestedItems = 0xf680, behind = 0xf700, triggers = 0x10000;
                put(world + 0x28, level);
                put(level + 0xa0, seqs);
                put(level + 0xa4, 1);
                put(seqs, seq);
                put(seq + 0xb0, items);
                put(seq + 0xb4, 3);
                put(items, fired);
                put(items + 4, ahead);
                put(items + 8, nested);
                put(nested + 0xb0, nestedItems);
                put(nested + 0xb4, 1);
                put(nestedItems, behind);
                unsigned n = 0;
                for (auto event : {fired, ahead, behind}) {
                    auto trigger = triggers + n++ * 0x100;
                    put(event + 0x20, 9);
                    put(event + 0xb8, trigger);
                    put(event + 0xd0, 0x80000000);
                    put(trigger + 0x20, 3);
                }
                put(fired + 0xc4, 1);
                real(triggers + 0x100 + 0x54, 1000); // ahead
                real(triggers + 0x200 + 0x54, -500); // behind, nearer
                data[pawn + 0x466] = 1;
                auto walkClasses = [&](uint32_t object, std::string_view cls) {
                    if (cls == "SeqEvent_Touch")
                        return object == fired || object == ahead || object == behind;
                    if (cls == "Sequence")
                        return object == nested;
                    return classes(object, cls);
                };
                auto events = walk_touch_events(memory, world, walkClasses);
                check(events.size() == 3, "touch triggers are found in nested level sequences");
                auto walk = capture_walk(memory, pc, 100, events, walkClasses);
                check(walk.active && walk.valid && walk.enemies && walk.remaining == 2 &&
                          walk.target.pointer == behind && std::abs(walk.distance - 500) < .1f,
                      "nearest unfired trigger is chosen; fired ones are skipped");
                check(!walk_cue(walk).active, "walking guidance stays quiet while enemies live");
                put(enemy + 0x2d8, 0);
                walk = capture_walk(memory, pc, 100, events, walkClasses);
                auto cue = walk_cue(walk);
                check(!walk.enemies && cue.active && std::abs(std::abs(cue.pan) - 1) < .01f &&
                          std::abs(525.f * std::pow(2.f, cue.elevation * .65f) - 440.f) < .5f,
                      "a trigger behind plays at 440 Hz, panned fully to one side");
                put(behind + 0xc4, 1);
                walk = capture_walk(memory, pc, 100, events, walkClasses);
                cue = walk_cue(walk);
                check(walk.target.pointer == ahead && cue.elevation == 0 &&
                          std::abs(cue.pan) < .01f && std::abs(cue.error - 15.f) < .01f,
                      "the next trigger ahead plays at 525 Hz, centred, pulsing by distance");
                data[pawn + 0x454] = 24;
                check(!capture_walk(memory, pc, 100, events, walkClasses).active,
                      "walking guidance yields to rapid fire");
                data[pawn + 0x454] = 0;
                data[pawn + 0x466] = 0;
                put(enemy + 0x2d8, 100);
            }
        }
        check(std::abs(melee_homing_degrees() - 2.5626f) < .01f &&
                  std::abs(melee_homing_threshold(1) - .001f) < .00002f,
              "native homing window is 1 - dot < 0.001, about 2.56 degrees");
        check(std::abs(melee_homing_degrees(3) - 7.688f) < .01f &&
                  melee_homing_degrees(20) == 30.f && melee_homing_degrees(-1) > 2.5f,
              "boosted homing scales the angle and stays bounded");
        check(aim_target_words(true, 0, 0) == "Projectile, ahead." &&
                  aim_target_words(false, -.5f, .3f) == "Enemy, left, high." &&
                  aim_target_words(false, .4f, -.1f) == "Enemy, right." &&
                  aim_target_words(true, 0, -.4f) == "Projectile, low.",
              "selected targets are described by kind and direction");
        check(aim_priority("enemies") == AimPriority::enemies &&
                  aim_priority("x") == AimPriority::projectiles &&
                  std::string(aim_priority_name(AimPriority::enemies)) == "enemies",
              "aim priority setting parses with projectiles as the default");
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
            // 0.2 s holds a whole number of 525-Hz cycles for the harmonic check.
            std::vector<float> tone(19200);
            triangle.render(aligned, .2f, tone.data(), 9600, 100);
            triangle.render(aligned, .2f, tone.data(), 9600, 100);
            auto harmonic = [&](unsigned multiple) {
                double real = 0, imaginary = 0;
                for (unsigned i = 0; i < 9600; ++i) {
                    auto angle = 6.283185307179586 * 525 * multiple * i / 48000.;
                    real += tone[i * 2] * std::cos(angle);
                    imaginary += tone[i * 2] * std::sin(angle);
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
                    energy += tone[i * 2] * tone[i * 2];
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
                    energy += tone[i * 2] * tone[i * 2];
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
