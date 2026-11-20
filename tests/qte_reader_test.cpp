#include "../src/qte_reader.h"
#include <iostream>
#include <stdexcept>
using namespace a11y;
static void check(bool pass, const char *message) {
    if (!pass)
        throw std::runtime_error(message);
}
template <size_t N> static void set(CapturedString<N> &out, std::string_view text) {
    out.length = uint16_t(text.size());
    for (size_t i = 0; i < text.size(); ++i)
        out.bytes[i * 2 + 1] = uint8_t(text[i]);
}
static PresentationEvent icon(std::string_view input, std::string_view tag, uint64_t order) {
    PresentationEvent event;
    event.kind = PresentationKind::button_create;
    event.movie = 100;
    event.index = 10;
    event.order = order;
    set(event.type, input);
    set(event.tag, tag);
    return event;
}
int main() {
    try {
        auto buttons = [](std::string_view button) {
            return controller_button(button, PromptDevice::playstation);
        };
        QteReader reader(buttons);
        QteGroup group;
        group.source = 1;
        group.index = 2;
        group.generation = 1;
        group.identifier = {9, 0};
        group.active = true;
        group.mode = 3;
        group.count = 2;
        group.commands[0] = {{10, 0}, 18, true};
        group.commands[1] = {{11, 0}, 21, true};
        auto names = [](CueName name) { return std::string("tag") + std::to_string(name.index); };
        reader.apply(icon("LDown", "tag10", 1));
        reader.apply(group, 2, names);
        check(reader.take().empty(), "never speak half a simultaneous request");
        reader.apply(icon("RUp", "tag11", 3));
        check(reader.take().empty(), "old snapshot cannot authorize a newly created icon");
        reader.apply(group, 4, names);
        auto lines = reader.take();
        check(lines.size() == 1 && lines[0] == "Move left stick down and right stick up together.",
              "paired tilts retain request grouping");
        check(reader.take().empty(), "unchanged active QTE is quiet");
        QteReader aliased([](std::string_view) { return std::string("K"); });
        aliased.apply(icon("A", "tag10", 1));
        aliased.apply(icon("X", "tag11", 2));
        aliased.apply(group, 3, names);
        check(aliased.repeat() == "Press K.",
              "shared keyboard binding does not invent a double-key chord");
        group.active = false;
        reader.apply(group, 5, names);
        check(reader.take().empty() && reader.repeat().empty(),
              "end/cancel does not produce a new cue or stale repeat");
        group.active = true;
        reader.apply(group, 6, names);
        check(reader.take().size() == 1, "reactivated request can speak again");
        reader.reset();
        group.mode = 0;
        group.count = 2;
        group.commands[0].visible = true;
        group.commands[1].visible = false;
        reader.apply(icon("A4", "tag10", 7));
        reader.apply(icon("Barrage_Y", "tag11", 8));
        reader.apply(group, 9, names);
        lines = reader.take();
        check(lines.size() == 1 && lines[0] == "Press cross.",
              "ordered mode reads the current resolved icon only");
        group.commands[0].visible = false;
        group.commands[1].visible = true;
        reader.apply(group, 10, names);
        lines = reader.take();
        check(lines.size() == 1 && lines[0] == "Press triangle.",
              "judge mode overrides icon styling prefixes");
        auto finish = icon("", "tag11", 11);
        finish.kind = PresentationKind::button_success;
        reader.apply(finish);
        check(reader.take().empty() && reader.repeat().empty(),
              "completed icon is not a current QTE");
        reader.apply(icon("Barrage_X", "tag11", 12));
        group.mode = 1;
        group.required = 15;
        reader.apply(group, 13, names);
        lines = reader.take();
        check(lines.size() == 1 && lines[0] == "Mash square, 15 times.",
              "resolved remapped mash button and count");
        group.mode = 2;
        group.hold_seconds = 2;
        group.required = 0;
        reader.apply(group, 14, names);
        check(reader.repeat() == "Hold square for 2 seconds, then release.",
              "release-required hold differs from hold-until-duration");
        group.required = 1;
        reader.apply(group, 15, names);
        check(reader.repeat() == "Hold square for 2 seconds.",
              "held-success variant does not instruct release");
        group.mode = 4;
        reader.apply(group, 16, names);
        check(reader.repeat() == "Keep holding square.",
              "continuous hold has no invented deadline");
        reader.reset();
        group.mode = 0;
        group.count = 1;
        group.commands[0].visible = true;
        reader.apply(icon("RRotateL", "tag10", 17));
        reader.apply(group, 18, names);
        check(reader.repeat() == "Rotate left stick clockwise.",
              "misleading native rotation names use verified physical stick");
        reader.apply(icon("LRotateSet", "tag10", 19));
        reader.apply(group, 20, names);
        check(reader.repeat() == "Rotate sticks counterclockwise.",
              "paired rotation does not claim both channels must succeed");
        auto hidden = icon("", "tag10", 21);
        hidden.kind = PresentationKind::button_visible;
        hidden.flag = false;
        reader.apply(hidden);
        check(reader.repeat().empty(), "hidden feedback is not an instruction");
        reader.closed({100, 10});
        check(reader.movies().empty(), "closing movie drops retained icons");
        reader.apply(icon("future unknown glyph", "tag10", 22));
        reader.apply(group, 23, names);
        check(reader.take().empty(), "unknown icon cannot invent an input");
        auto reused = icon("X", "tag10", 24);
        reused.revision = 10;
        reader.apply(reused);
        group.visual_revision = 9;
        reader.apply(group, 25, names);
        check(reader.take().empty(),
              "a newer judge snapshot cannot authorize an icon the source action has not observed");
        group.visual_revision = 10;
        reader.apply(group, 26, names);
        check(reader.take().size() == 1,
              "the actual action boundary authorizes its current presentation revision");
        std::vector<uint8_t> data(0x10000);
        auto word = [&](uint32_t at, uint32_t value) {
            for (unsigned i = 0; i < 4; ++i)
                data[at + i] = uint8_t(value >> (24 - 8 * i));
        };
        auto read = [&](uint32_t at, void *out, size_t n) {
            if (uint64_t(at) + n > data.size())
                return false;
            std::memcpy(out, data.data() + at, n);
            return true;
        };
        Memory memory(read);
        const uint32_t actor = 0x1000, pc = 0x2000, judge = 0x3000, row = 0x4000, requests = 0x5000,
                       commands = 0x6000, info = 0x7000;
        word(actor + 0x20, 7);
        word(actor + 0xc4, 0x18800000);
        word(actor + 0xe4, 9);
        word(actor + 0xf0, requests);
        word(actor + 0xf4, 2);
        word(actor + 0xf8, 2);
        data[actor + 0x114] = 3;
        word(actor + 0x118, 1);
        word(pc + 0x760, judge);
        word(judge + 0x20, 8);
        word(judge + 0x1c4, row);
        word(judge + 0x1c8, 1);
        word(judge + 0x1cc, 1);
        word(row, commands);
        word(row + 4, 2);
        word(row + 8, 2);
        data[row + 0xc] = 3;
        word(row + 0x20, info);
        word(row + 0x24, 2);
        word(row + 0x28, 2);
        word(row + 0x48, 0x80000000);
        word(row + 0x50, 9);
        for (unsigned i = 0; i < 2; ++i) {
            data[commands + i] = data[requests + i * 0x30] = uint8_t(17 + i);
            word(requests + i * 0x30 + 0x24, 10 + i);
            word(requests + i * 0x30 + 0x2c, 0x80000000);
        }
        word(info, 0x40000000);
        QteGroup copied;
        check(capture_qte_group(memory, actor, pc, copied) && copied.active && copied.count == 2 &&
                  copied.commands[0].visible && copied.commands[1].visible,
              "simultaneous visibility survives judge's first-entry-only show initialization");
        word(requests + 0x20, 0x80000000);
        check(capture_qte_group(memory, actor, pc, copied) && !copied.commands[0].visible,
              "authored hidden command excluded");
        word(requests + 0x20, 0);
        word(info + 12, 0x80000000);
        check(capture_qte_group(memory, actor, pc, copied) && !copied.commands[1].visible,
              "completed command excluded even before icon cleanup");
        word(row + 0x58, 0x80000000);
        copied = {};
        check(capture_qte_group(memory, actor, pc, copied) && !copied.active,
              "paused judge cannot authorize input");
        word(row + 0x58, 0);
        word(actor + 0xf4, 33);
        check(!capture_qte_group(memory, actor, pc, copied), "oversized request fails closed");
        word(actor + 0xf4, 2);
        word(actor + 0xc4, 0x1c800000);
        copied = {};
        check(capture_qte_group(memory, actor, pc, copied) && !copied.active,
              "end flag suppresses retained request data");
        word(actor + 0x11c, 0x20000000);
        data[actor + 0xc4] = 30;
        word(actor + 0xc4 + 0x24, 10);
        word(actor + 0xc4 + 0x2c, 0);
        check(capture_animation_qte(memory, actor, QteSchema::animation_mash, copied) &&
                  copied.active && copied.mode == 1 && copied.commands[0].visible,
              "animation mash reads enabled command despite no input-judge request or show flag");
        word(actor + 0x11c, 0x60000000);
        check(capture_animation_qte(memory, actor, QteSchema::animation_mash, copied) &&
                  !copied.active,
              "successful animation stops requesting input");
        word(actor + 0xf0, 0x20000000);
        word(actor + 0xc4, requests);
        word(actor + 0xc8, 2);
        word(actor + 0xcc, 2);
        word(requests + 0x2c, 0x80000000);
        word(requests + 0x30 + 0x2c, 0x80000000);
        check(
            capture_animation_qte(memory, actor, QteSchema::animation_hold, copied) &&
                copied.active && copied.count == 2 && copied.mode == 4 && copied.hold_seconds == 0,
            "animation hold groups its list without treating the movie's cosmetic one-second hold count as a deadline");
        word(actor + 0xf0, 0x30000000);
        check(capture_animation_qte(memory, actor, QteSchema::animation_hold, copied) &&
                  !copied.active,
              "animation output invalidates its group");
        std::cout << "QTE snapshot, grouping and narration tests passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
