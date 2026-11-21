#include "../src/gameplay_status.h"
#include <iostream>
#include <stdexcept>
using namespace a11y;
static void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <size_t N> static void set(CapturedString<N> &out, std::string_view text) {
    out.length = uint16_t(text.size());
    for (size_t i = 0; i < text.size(); ++i)
        out.bytes[i * 2 + 1] = uint8_t(text[i]);
}
static PresentationEvent event(PresentationKind kind, float value = 0, bool flag = true,
                               uint32_t movie = 100, uint32_t index = 10) {
    PresentationEvent e;
    e.kind = kind;
    e.value = value;
    e.flag = flag;
    e.movie = movie;
    e.index = index;
    return e;
}
int main() {
    try {
        GameplayStatus status([](std::string_view button) {
            return controller_button(button, PromptDevice::playstation);
        });
        status.apply(event(PresentationKind::health, 1, false));
        status.apply(event(PresentationKind::burst, .5f, false));
        check(status.status() == "Health 100 percent. Burst 50 percent." &&
                  status.status({100, 10}) == status.status() && status.take().empty(),
              "hidden but live gauges answer explicit requests without automatic chatter");
        status.apply(event(PresentationKind::gauge_visible, 0, true));
        check(status.status() == "Health 100 percent. Burst 50 percent.",
              "status combines current gauge values without automatic percentage chatter");
        check(status.status({100, 10}) == status.status() && status.status({100, 11}).empty() &&
                  status.status({101, 10}).empty(),
              "contextual status requires the exact active gauge identity");
        check(status.take().empty(), "ordinary meter updates are quiet");
        status.apply(event(PresentationKind::health, .24f));
        auto lines = status.take();
        check(lines.size() == 1 && lines[0] == "Low health. 24 percent.", "one low-health warning");
        status.apply(event(PresentationKind::health, .23f));
        status.apply(event(PresentationKind::health, .28f));
        status.apply(event(PresentationKind::health, .2f));
        check(status.take().empty(), "hysteresis suppresses repeated health warnings");
        status.apply(event(PresentationKind::health, .36f));
        status.apply(event(PresentationKind::health, .2f));
        check(status.take().size() == 1, "recovery rearms low health");
        status.apply(event(PresentationKind::burst, 1));
        check(status.take().empty() && status.status().find("ready") == std::string::npos,
              "full gauge alone is not proof Burst can be used");
        auto create = event(PresentationKind::button_create, 0, false, 200, 20);
        set(create.type, "BURST");
        set(create.tag, "InputDivergence_0");
        status.apply(create);
        check(status.take().empty(), "authored Burst QTE is not a readiness notification");
        create.tag = {};
        set(create.tag, "BurstButton_1");
        status.apply(create);
        lines = status.take();
        check(lines.size() == 1 && lines[0] == "Burst ready. R2.",
              "native usable Burst button announces current displayed input");
        status.apply(create);
        check(status.take().empty(), "same Burst button is quiet");
        auto active = create;
        active.kind = PresentationKind::button_active;
        active.flag = false;
        status.apply(active);
        auto visible = active;
        visible.kind = PresentationKind::button_visible;
        visible.flag = true;
        status.apply(visible);
        check(status.take().empty(), "visibility does not reactivate disabled input");
        active.flag = true;
        status.apply(active);
        check(status.take().size() == 1, "reactivating usable button is announced");
        auto finish = create;
        finish.kind = PresentationKind::button_finish;
        status.apply(finish);
        check(status.status().find("ready") == std::string::npos,
              "finished button removes readiness");
        status.apply(create);
        status.apply(finish);
        check(status.take().empty(),
              "dismissal invalidates unsent Burst notification without speech cancellation");
        status.apply(event(PresentationKind::health, .5f));
        status.apply(event(PresentationKind::health, .2f));
        status.apply(event(PresentationKind::gauge_end));
        check(status.take().empty() && status.status().find("not available") != std::string::npos,
              "closed gauges invalidate pending warning and status");
        status.apply(event(PresentationKind::gauge_visible, 0, true));
        status.apply(event(PresentationKind::health, .8f));
        status.apply(event(PresentationKind::burst, .4f));
        status.apply(event(PresentationKind::health, .5f, false, 100, 11));
        check(status.status().find("not available") != std::string::npos,
              "slot reuse does not retain old visibility or Burst percentage");
        PresentationFilter filter;
        auto value = event(PresentationKind::health, .5f);
        check(filter.changed(value, 1), "initial meter update publishes");
        for (unsigned i = 0; i < 10000; ++i)
            check(!filter.changed(value, 1), "unchanged gauge frames never wake the worker");
        value.value = .501f;
        check(!filter.changed(value, 1), "subpercentage changes coalesce");
        value.value = .51f;
        check(filter.changed(value, 1), "new displayed percentage publishes");
        check(filter.changed(value, 2), "new capture epoch resynchronizes unchanged values");
        value.index++;
        check(filter.changed(value, 2), "slot identity resynchronizes values");
        check(filter.changed(active, 2), "initial active button publishes");
        for (unsigned i = 0; i < 10000; ++i)
            check(!filter.changed(active, 2), "unchanged button flags do not flood the queue");
        filter.changed(create, 2);
        check(filter.changed(active, 2),
              "recreated button cannot inherit prior cached active flag");
        std::vector<uint8_t> data(0x8000);
        auto word = [&](uint32_t at, uint32_t v) {
            for (unsigned i = 0; i < 4; ++i)
                data[at + i] = uint8_t(v >> (24 - 8 * i));
        };
        auto read = [&](uint32_t at, void *out, size_t n) {
            if (uint64_t(at) + n > data.size())
                return false;
            std::memcpy(out, data.data() + at, n);
            return true;
        };
        word(0x114, 0x200);
        word(0x11c, 0x400);
        word(0x2c8, 0x80000000);
        word(0x3e0, 0x80000000);
        word(0x220, 7);
        word(0x400, std::bit_cast<uint32_t>(.75f));
        PresentationEvent copied;
        check(capture_presentation(read, 0x100, PresentationKind::health, copied) &&
                  copied.value == .75f && copied.flag,
              "big-endian float and semantic visibility capture");
        word(0x400, 0x7fc00000);
        check(!capture_presentation(read, 0x100, PresentationKind::health, copied),
              "NaN gauge value cannot poison status");
        word(0x400, std::bit_cast<uint32_t>(1.01f));
        check(!capture_presentation(read, 0x100, PresentationKind::health, copied),
              "out-of-range gauge values rejected");
        word(0x400, 1);
        check(capture_presentation(read, 0x100, PresentationKind::gauge_visible, copied) &&
                  copied.flag,
              "presentation bool capture");
        word(0x11c, 0x9000);
        check(!capture_presentation(read, 0x100, PresentationKind::health, copied),
              "unreadable float is not treated as zero health");
        check(
            presentation_route("BCGFxUI_Gauge", "CallSetNextHealth") == PresentationKind::health &&
                presentation_route("BCGFxUI_Gauge", "CallSetNextBurst") == PresentationKind::burst,
            "per-frame Next setters carry the current pawn values");
        GameplayStatus high({}, 90);
        high.apply(event(PresentationKind::health, .9f));
        check(high.take().size() == 1, "custom high threshold warns");
        high.apply(event(PresentationKind::health, 1));
        high.apply(event(PresentationKind::health, .9f));
        check(high.take().size() == 1, "full recovery rearms even the highest supported threshold");
        std::cout << "Gameplay status and coalescing tests passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
