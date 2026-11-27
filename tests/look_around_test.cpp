#include "../src/look_around.h"
#include "../src/gameplay_dump.h"
#include <bit>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace a11y;
static void check(bool pass, const char *message) {
    if (!pass)
        throw std::runtime_error(message);
}
int main() {
    try {
        std::vector<uint8_t> data(0x20000);
        auto put = [&](uint32_t at, uint32_t value) {
            for (int i = 0; i < 4; ++i)
                data[at + i] = uint8_t(value >> (24 - 8 * i));
        };
        auto real = [&](uint32_t at, float value) { put(at, std::bit_cast<uint32_t>(value)); };
        Memory memory([&](uint32_t at, void *out, size_t size) {
            if (uint64_t(at) + size > data.size())
                return false;
            memcpy(out, data.data() + at, size);
            return true;
        });
        constexpr uint32_t pc = 0x1000, manager = 0x2000, camera = 0x3000, list = 0x4000,
                           targets = 0x5000, actors = 0x6000, names = 0x7000;
        LookFields fields;
        fields.view_target = 0x40;
        fields.enable_offset = 0x100;
        fields.enable_mask = 0x80000000;
        fields.angle_rate = 0x104;
        fields.list = 0x108;
        fields.group_name = 0;
        fields.targets = 12;
        fields.trigger_time = 24;
        fields.element_size = 32;
        // The view: at the origin, facing +X, 60-degree FOV.
        put(pc + 0x370, manager);
        real(manager + 0x30c + 24, 60);
        put(manager + fields.view_target, camera);
        put(camera + fields.enable_offset, 0x80000000);
        real(camera + fields.angle_rate, .3f); // cone: 30 * 0.3 = 9 degrees
        put(camera + fields.list, list);
        put(camera + fields.list + 4, 3);
        // Group 0: one target 30 degrees to the right. Group 1: "reset", dead ahead.
        // Group 2: one target 10 degrees up, needing 0.5 s.
        auto target = [&](unsigned group, unsigned index, float x, float y, float z) {
            auto actor = actors + (group * 4 + index) * 0x100;
            put(targets + group * 0x40 + index * 4, actor);
            real(actor + 0x54, x), real(actor + 0x58, y), real(actor + 0x5c, z);
        };
        for (unsigned g = 0; g < 3; ++g) {
            put(list + g * 32 + 12, targets + g * 0x40);
            put(list + g * 32 + 16, 1);
            real(list + g * 32 + 24, g == 2 ? .5f : 1.f);
        }
        target(0, 0, 1000 * std::cos(.5236f), 1000 * std::sin(.5236f), 0);
        target(1, 0, 1000, 0, 0);
        target(2, 0, 1000 * std::cos(.1745f), 0, 1000 * std::sin(.1745f));
        // Group 1's name: リセット用, as big-endian UTF-16 with a terminator.
        const char16_t reset[] = u"リセット用";
        for (unsigned i = 0; i < 6; ++i) {
            data[names + i * 2] = uint8_t(reset[i] >> 8);
            data[names + i * 2 + 1] = uint8_t(reset[i]);
        }
        put(list + 32, names);
        put(list + 32 + 4, 6);
        auto classes = [&](uint32_t object, std::string_view cls) {
            return cls == "BCCameraActorViewCheck" && object == camera;
        };
        LookTracker look;
        auto snap = look.update(memory, pc, fields, classes, 1000);
        check(snap.active && snap.target && snap.remaining == 2 && snap.group == 2 &&
                  std::abs(snap.angle - 10) < .2f && std::abs(snap.cone - 9) < .01f && !snap.inside,
              "the nearest group to find is chosen; the reset group is skipped");
        auto cue = look_cue(snap);
        check(cue.active && cue.elevation > .4f && std::abs(cue.pan) < .01f && cue.error > 4,
              "above the view: higher pitch, centred, still pulsing");
        // Turn the view up onto group 2 and hold it.
        put(manager + 0x30c + 12, uint32_t(10.f / 360.f * 65536.f));
        snap = look.update(memory, pc, fields, classes, 1100);
        check(snap.inside && look_cue(snap).error <= 4, "inside the game's cone: steady");
        snap = look.update(memory, pc, fields, classes, 1500);
        check(snap.found == 0, "not found before the hold time and margin");
        snap = look.update(memory, pc, fields, classes, 1900);
        check(snap.found == 1, "found after holding for the game's time");
        snap = look.update(memory, pc, fields, classes, 1950);
        check(snap.group == 0 && snap.remaining == 1 && snap.yaw > 25,
              "guidance moves on to the next group, to the right");
        put(camera + fields.enable_offset, 0);
        check(!look.update(memory, pc, fields, classes, 2000).active,
              "nothing plays while the game is not checking views");
        check(std::abs(segment_distance({5, 3, 0}, {0, 0, 0}, {10, 0, 0}) - 3) < .001f &&
                  std::abs(segment_distance({-4, 3, 0}, {0, 0, 0}, {10, 0, 0}) - 5) < .001f,
              "dump path distance is measured to the segment, not the infinite line");
        std::cout << "Look-around tests passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
