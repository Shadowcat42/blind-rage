#include "../src/ui_hook_source.h"
#include <cstdio>
#include <cstdlib>
static void require(bool pass, const char *message) {
    if (!pass) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
static void word(uint8_t *memory, uint32_t at, uint32_t value) {
    for (int i = 3; i >= 0; i--) {
        memory[at + i] = uint8_t(value);
        value >>= 8;
    }
}
static volatile unsigned lifecycle_calls;
extern "C" __declspec(noinline) void normal_target(void *c, uint8_t *b) {
    auto *ctx = static_cast<uint64_t *>(c);
    std::memcpy(b + ctx[0] + 0xf18, b + ctx[6], 16);
    ctx[0] = 0x777;
}
extern "C" __declspec(noinline) void raw_target(void *c, uint8_t *b) {
    auto *ctx = static_cast<uint64_t *>(c);
    std::memcpy(b + ctx[0] + 0xf18, b + ctx[6], 16);
    ctx[0] = 0x888;
}
extern "C" __declspec(noinline) void open_target(void *, uint8_t *) {
    lifecycle_calls = 1;
}
extern "C" __declspec(noinline) void close_target(void *, uint8_t *) {
    lifecycle_calls = 2;
}
extern "C" __declspec(noinline) void text_target(void *c, uint8_t *b) {
    auto *ctx = static_cast<uint64_t *>(c);
    b[ctx[0] + 0x701] = uint8_t(ctx[4]);
    ctx[0] = 1;
}
extern "C" __declspec(noinline) void display_target(void *c, uint8_t *b) {
    auto *ctx = static_cast<uint64_t *>(c);
    word(b, uint32_t(ctx[0]) + 0xa0, uint32_t(ctx[4]));
    ctx[0] = 2;
}
extern "C" __declspec(noinline) void script_target(void *c, uint8_t *b) {
    auto *ctx = static_cast<uint64_t *>(c);
    b[ctx[0] + 0x1f1] = uint8_t(ctx[4]);
    ctx[0] = 3;
}
extern "C" __declspec(noinline) void advance_target(void *c, uint8_t *b) {
    lifecycle_calls = 3 + uint32_t(static_cast<uint64_t *>(c)[0]) + b[0];
}
extern "C" __declspec(noinline) void visible_target(void *c, uint8_t *b) {
    auto *ctx = static_cast<uint64_t *>(c);
    word(b, uint32_t(ctx[0]) + 0xa0, uint32_t(ctx[4]));
    ctx[0] = 4;
}
extern "C" __declspec(noinline) void sprite_visible_target(void *c, uint8_t *b) {
    auto *ctx = static_cast<uint64_t *>(c);
    word(b, uint32_t(ctx[0]) + 0xa0, uint32_t(ctx[4]));
    ctx[0] = 5;
}
int main() {
    std::array<uint8_t, 8192> memory{};
    auto *b = memory.data();
    std::array<uint64_t, 40> ctx{};
    ctx[0] = 0x100;
    ctx[5] = 0x400;
    ctx[6] = 0x500;
    word(b, 0x10c, 0x1000);
    word(b, 0x1000, 1);
    word(b, 0x1004, 0);
    word(b, 0x1008, 0xffffffff);
    word(b, 0x1010, 0x200);
    word(b, 0x400, 0x200);
    word(b, 0x200, 0x300);
    std::memcpy(b + 0x300, "m_cursor", 9);
    b[0x1018] = 4;
    b[0x500] = 4;
    word(b, 0x508, 1);
    auto &source = a11y::UiHookSource::instance();
    require(
        source.install_targets({(void *)normal_target, (void *)raw_target, (void *)open_target,
                                (void *)close_target, (void *)text_target, (void *)display_target,
                                (void *)script_target, (void *)advance_target,
                                (void *)visible_target, (void *)sprite_visible_target}),
        "install all event hooks");
    auto event = [&] { return WaitForSingleObject(source.event(), 0) == WAIT_OBJECT_0; };
    source.capture(true);
    require(event(), "initial discovery notification");
    normal_target(ctx.data(), b);
    require(!event() && ctx[0] == 0x777,
            "assignment preserves original return register and waits for boundary");
    advance_target(ctx.data(), b);
    require(event(), "assignment event at boundary");
    ctx[0] = 0x100;
    normal_target(ctx.data(), b);
    advance_target(ctx.data(), b);
    require(!event(), "unchanged assignment stays quiet");
    ctx[0] = 0x100;
    word(b, 0x508, 2);
    raw_target(ctx.data(), b);
    require(!event() && ctx[0] == 0x888, "raw assignment preserves original behavior");
    advance_target(ctx.data(), b);
    require(event(), "raw assignment event");
    ctx[0] = 0x100;
    word(b, 0x508, 1);
    normal_target(ctx.data(), b);
    ctx[0] = 0x100;
    word(b, 0x508, 2);
    raw_target(ctx.data(), b);
    advance_target(ctx.data(), b);
    require(!event(), "temporary member writes restored before boundary stay quiet");
    open_target(ctx.data(), b);
    require(event() && lifecycle_calls == 1, "open notification");
    close_target(ctx.data(), b);
    require(event() && lifecycle_calls == 2, "close notification");
    // Native text layout matches the reader's UTF-16 paragraph decoder.
    word(b, 0x11bc, 0x1300);
    word(b, 0x1308, 0x1400);
    word(b, 0x140c, 0x1500);
    word(b, 0x1410, 1);
    word(b, 0x1500, 0x1600);
    word(b, 0x1600, 0x1800);
    word(b, 0x1604, 1);
    b[0x1801] = 'A';
    ctx[0] = 0x1100;
    ctx[4] = 'B';
    text_target(ctx.data(), b);
    require(event() && b[0x1801] == 'B', "changed native text event");
    ctx[0] = 0x1100;
    text_target(ctx.data(), b);
    require(!event(), "unchanged native text stays quiet");
    ctx[0] = 0x1100;
    ctx[4] = 2;
    display_target(ctx.data(), b);
    require(!event(), "display change waits for boundary");
    advance_target(ctx.data(), b);
    require(event(), "display bit transition event");
    ctx[0] = 0x1100;
    ctx[4] = 6;
    display_target(ctx.data(), b);
    advance_target(ctx.data(), b);
    require(!event(), "other display bits stay quiet");
    ctx[0] = 0x1100;
    ctx[4] = 1;
    visible_target(ctx.data(), b);
    advance_target(ctx.data(), b);
    require(event(), "explicit visibility event");
    ctx[0] = 0x1100;
    ctx[4] = 0;
    sprite_visible_target(ctx.data(), b);
    advance_target(ctx.data(), b);
    require(event(), "sprite visibility event");
    ctx[0] = 0x1100;
    ctx[4] = 1;
    visible_target(ctx.data(), b);
    ctx[0] = 0x1100;
    ctx[4] = 0;
    visible_target(ctx.data(), b);
    advance_target(ctx.data(), b);
    require(!event(), "temporary visibility toggles coalesce away");
    source.watch_title(0x1100);
    ctx[0] = 0x1100;
    ctx[4] = 4;
    script_target(ctx.data(), b);
    require(event() && ctx[0] == 3, "startup script assignment event");
    ctx[0] = 0x1100;
    script_target(ctx.data(), b);
    require(!event(), "unchanged startup state stays quiet");
    source.capture(false);
    ctx[0] = 0x100;
    word(b, 0x508, 3);
    normal_target(ctx.data(), b);
    require(!event() && ctx[0] == 0x777, "stop capture leaves original behavior intact");
    require(source.changes() == 9, "only actual changes or lifecycle calls notify");
    std::puts("UI hook source tests passed.");
}
