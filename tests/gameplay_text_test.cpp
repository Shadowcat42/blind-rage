#include "../src/gameplay_text.h"
#include <iostream>
#include <stdexcept>
using namespace a11y;
static void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <size_t N> static void set(CapturedString<N> &out, const std::string &text) {
    check(text.size() < N, "fixture length");
    out.length = uint16_t(text.size());
    for (size_t i = 0; i < text.size(); i++) {
        out.bytes[i * 2] = 0;
        out.bytes[i * 2 + 1] = uint8_t(text[i]);
    }
}
static PresentationEvent event(PresentationKind kind, std::string text = "",
                               std::string tag = "line", std::string type = "subtitle",
                               uint32_t movie = 1, uint32_t index = 5) {
    PresentationEvent e;
    e.kind = kind;
    e.movie = movie;
    e.index = index;
    set(e.text, text);
    set(e.tag, tag);
    set(e.type, type);
    return e;
}
int main() {
    try {
        check(gameplay_text("<font color='#fff'>Press <A> &amp; hold <Y>.</font>") ==
                  "Press A button & hold Y button.",
              "preserve remapped button glyphs while removing HTML");
        check(gameplay_text("Press &lt;X&gt;") == "Press X button", "escaped button glyph");
        auto ps = [](std::string_view button) {
            if (button == "RS")
                return std::string("Right stick");
            if (button == "LS")
                return std::string("Left stick");
            return controller_button(button, PromptDevice::playstation);
        };
        check(gameplay_text("Switch targets! <RS> Toggle between targets", ps) ==
                  "Switch targets! Right stick Toggle between targets",
              "right stick tag survives markup cleanup");
        check(gameplay_text("Use <LS> to aim reticle.", ps) == "Use Left stick to aim reticle.",
              "left stick tag survives markup cleanup");
        check(gameplay_text("<Back> Skip tutorial", ps) == "Select Skip tutorial",
              "mixed-case Back tag resolves to the back button");
        unsigned lookups = 0;
        check(gameplay_text("No glyph here.",
                            [&](std::string_view) {
                                ++lookups;
                                return std::string("unused");
                            }) == "No glyph here." &&
                  lookups == 0,
              "unrelated gauge/text events never query ten runtime button bindings");
        CapturedString<8> unicode;
        unicode.length = 2;
        unicode.bytes[0] = 0xd8;
        unicode.bytes[1] = 0x3d;
        unicode.bytes[2] = 0xde;
        unicode.bytes[3] = 0x00;
        check(unicode.utf8() == "\xf0\x9f\x98\x80", "UTF-16 surrogate pair");
        unicode.length = 1;
        check(unicode.utf8().empty(), "reject incomplete surrogate");
        GameplayText reader({});
        auto start = event(PresentationKind::text_start, "A synthetic subtitle.");
        reader.apply(start);
        auto spoken = reader.take();
        check(spoken.size() == 1 && spoken[0].channel == "subtitle", "subtitle start speaks");
        reader.apply(start);
        check(reader.take().empty(), "active duplicate is quiet");
        reader.apply(event(PresentationKind::text_end));
        check(reader.repeat().empty(), "end removes repeat target");
        reader.apply(start);
        check(reader.take().size() == 1, "same text after a new lifecycle speaks again");
        reader.apply(event(PresentationKind::text_start, "Replaced subtitle."));
        reader.apply(event(PresentationKind::text_start, "Final subtitle."));
        spoken = reader.take();
        check(spoken.size() == 1 && spoken[0].text == "Final subtitle.",
              "coalesce replacements without stale queued text");
        reader.apply(event(PresentationKind::text_start, "Too brief to remain active."));
        reader.apply(event(PresentationKind::text_end));
        check(reader.take().empty(), "end invalidates unsent text");
        reader.apply(start);
        reader.apply(event(PresentationKind::text_end_type, "", "", "subtitle"));
        check(reader.take().empty(), "type dismissal invalidates unsent text");
        reader.apply(start);
        reader.apply(event(PresentationKind::text_clear));
        check(reader.take().empty(), "clear invalidates unsent text");
        reader.apply(event(PresentationKind::text_start, "Old object."));
        reader.apply(event(PresentationKind::text_start, "New object.", "other", "subtitle", 1, 6));
        spoken = reader.take();
        check(spoken.size() == 1 && spoken[0].text == "New object.",
              "object slot reuse discards old text");
        reader.reset();
        reader.apply(event(PresentationKind::tutorial_create, "Move with the stick."));
        reader.apply(event(PresentationKind::tutorial_create, "Press <A> to jump."));
        check(reader.take().empty(), "prepared tutorial is not narrated before display");
        reader.apply(event(PresentationKind::tutorial_start));
        spoken = reader.take();
        check(spoken.size() == 1 &&
                  spoken[0].text.find("Move with the stick") != std::string::npos &&
                  spoken[0].text.find("A button") != std::string::npos,
              "tutorial entry includes each prepared line");
        reader.apply(event(PresentationKind::tutorial_start));
        check(reader.take().empty(), "repeated visible start is quiet");
        reader.apply(event(PresentationKind::tutorial_end));
        check(reader.repeat().empty(), "dismissed tutorial does not remain repeatable");
        reader.apply(event(PresentationKind::tutorial_start));
        check(reader.take().empty(), "a start without newly created lines displays nothing new");
        reader.apply(event(PresentationKind::tutorial_create, "Press <A> to jump."));
        reader.apply(event(PresentationKind::tutorial_start));
        check(reader.take().size() == 1, "a line created again for a new start speaks again");
        // Episode 3 (BC-S020_L01_PLY_01): each mission creates its lines and
        // starts; the movie's Start empties its queue, so earlier steps are not
        // part of the next one.
        reader.reset();
        reader.apply(event(PresentationKind::tutorial_create, "Skip tutorial"));
        reader.apply(event(PresentationKind::tutorial_start));
        spoken = reader.take();
        check(spoken.size() == 1 && spoken[0].text == "Skip tutorial", "first step speaks");
        reader.apply(event(PresentationKind::tutorial_create, "Perform a combo attack!"));
        reader.apply(event(PresentationKind::tutorial_create, "<B> Attack"));
        check(reader.take().empty(), "created lines wait for their start");
        reader.apply(event(PresentationKind::tutorial_start));
        spoken = reader.take();
        check(spoken.size() == 1 && spoken[0].text == "Perform a combo attack! B button Attack",
              "a new step speaks only its own lines");
        check(reader.repeat() == spoken[0].text, "repeat reads the current step");
        reader.apply(event(PresentationKind::tutorial_create, "Perform a heavy attack!"));
        reader.apply(event(PresentationKind::tutorial_start));
        spoken = reader.take();
        check(spoken.size() == 1 && spoken[0].text == "Perform a heavy attack!",
              "the next step does not reread earlier steps");
        reader.apply(event(PresentationKind::tutorial_end));
        reader.apply(event(PresentationKind::tutorial_start));
        check(reader.take().empty(), "redisplay without new lines does not reread a step");
        reader.apply(event(PresentationKind::tutorial_clear));
        reader.apply(event(PresentationKind::tutorial_box, "", "", "health"));
        reader.apply(event(PresentationKind::tutorial_start));
        check(reader.boxes().size() == 1 && reader.take().empty(),
              "box tutorial awaits visible text snapshot");
        reader.box_text({1, 5}, "Health instructions");
        check(reader.take().size() == 1, "box visible text speaks");
        reader.box_text({1, 5}, "Health instructions");
        check(reader.take().empty(), "unchanged box text stays quiet");
        reader.closed({1, 5});
        check(reader.repeat().empty() && reader.boxes().empty(),
              "closed movie cannot retain tutorial text");
        // Episode 3 (BC-S020_L01_PLY_05): the Unlimited step creates its text, then
        // SetBox selects the gauge picture. The created line, with its button, is
        // what is spoken; the drawn text (button shown as an image) is not.
        reader.apply(event(PresentationKind::tutorial_create,
                           "Unlimited Mode can be activated by pressing <LT>."));
        reader.apply(event(PresentationKind::tutorial_box, "", "", "unlimited"));
        reader.apply(event(PresentationKind::tutorial_start));
        spoken = reader.take();
        check(spoken.size() == 1 &&
                  spoken[0].text == "Unlimited Mode can be activated by pressing LT button.",
              "box tutorial speaks its created line with the button");
        reader.box_text({1, 5}, "Unlimited Mode can be activated by pressing.");
        check(reader.take().empty() && reader.repeat() == spoken[0].text,
              "drawn box text does not replace a created line");
        reader.closed({1, 5});
        GameplayText disabled({false, false});
        disabled.apply(start);
        disabled.apply(event(PresentationKind::tutorial_create, "Hidden by user preference"));
        disabled.apply(event(PresentationKind::tutorial_start));
        check(disabled.take().empty() && disabled.repeat().empty(),
              "separate narration preferences");
        PromptDevice device = PromptDevice::xbox;
        GameplayText dynamic(
            {}, [&](std::string_view button) { return controller_button(button, device); });
        dynamic.apply(event(PresentationKind::tutorial_create, "<font>Press <B>.</font>"));
        device = PromptDevice::playstation;
        dynamic.apply(event(PresentationKind::tutorial_start));
        spoken = dynamic.take();
        check(spoken.size() == 1 && spoken[0].text == "Press circle.",
              "prepared tutorial resolves device at narration time");
        device = PromptDevice::xbox;
        check(dynamic.repeat() == "Press B.",
              "repeat resolves the current effective prompt device");
        dynamic.apply(event(PresentationKind::tutorial_end));
        dynamic.apply(event(PresentationKind::tutorial_clear));
        dynamic.apply(event(PresentationKind::text_start, "<font color='red'>Press <A>.</font>"));
        dynamic.take();
        dynamic.apply(event(PresentationKind::text_start, "<font color='blue'>Press <A>.</font>"));
        check(dynamic.take().empty(), "cosmetic markup changes do not repeat a subtitle");
        PresentationQueue queue;
        bool overflow = false;
        for (unsigned i = 0; i < 65; i++)
            queue.push(start);
        auto batch = queue.take(overflow);
        check(overflow && batch.size() == 64, "bounded queue reports overflow");
        check(queue.take(overflow).empty() && !overflow, "draining resets overflow");
        std::vector<uint8_t> data(0x8000);
        auto word = [&](uint32_t at, uint32_t value) {
            for (unsigned i = 0; i < 4; i++)
                data[at + i] = uint8_t(value >> (24 - 8 * i));
        };
        auto read = [&](uint32_t at, void *out, size_t n) {
            if (uint64_t(at) + n > data.size())
                return false;
            memcpy(out, data.data() + at, n);
            return true;
        };
        word(0x114, 0x200);
        word(0x11c, 0x400);
        word(0x2c8, 0x80000000);
        word(0x220, 7);
        auto fstring = [&](uint32_t header, uint32_t at, const std::string &text) {
            word(header, at);
            word(header + 4, uint32_t(text.size() + 1));
            word(header + 8, uint32_t(text.size() + 1));
            for (size_t i = 0; i < text.size(); i++)
                data[at + i * 2 + 1] = uint8_t(text[i]);
        };
        fstring(0x400, 0x1000, "id");
        fstring(0x40c, 0x1200, "Copied text");
        fstring(0x418, 0x1400, "subtitle");
        PresentationEvent captured;
        check(capture_presentation(read, 0x100, PresentationKind::text_start, captured) &&
                  captured.movie == 0x200 && captured.index == 7 &&
                  captured.text.utf8() == "Copied text",
              "capture frame locals and instance identity");
        data[0x1201] = 'X';
        check(captured.text.utf8() == "Copied text",
              "captured text owns its bytes after script locals change");
        word(0x410, 5000);
        check(!capture_presentation(read, 0x100, PresentationKind::text_start, captured),
              "oversized strings rejected");
        word(0x410, 12);
        word(0x414, 12);
        data[0x1200 + 22] = 1;
        check(!capture_presentation(read, 0x100, PresentationKind::text_start, captured),
              "unterminated strings rejected");
        check(presentation_route("BCGFxMovieButton", "CallCreate") ==
                  PresentationKind::button_create,
              "button events stay distinct from tutorial text");
        std::cout << "Gameplay text capture and lifecycle tests passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
