#pragma once
// Calling a game script function from the mod, through the engine's own
// UObject::ProcessEvent (guest 0x82260f60 on both builds). Research support for
// mod tutorial boxes: see docs/MOD_TUTORIALS_RESEARCH.md.
//
// A call is made from inside a game-thread hook on ProcessEvent, after the
// game's own call has returned, through the hook's trampoline. The recompiled
// context keeps guest registers as 64-bit slots: r3 at 0x00, r1 (stack pointer)
// at 0x10, r4 at 0x20, r5 at 0x28, r6 at 0x30 and the link register at 0x100
// (read from the host code of the hooked functions). The parameter block and
// any string data go in guest stack space below the current r1, and every
// register the call changes is restored afterwards.
#include "menu_reader.h"
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace a11y {
struct ScriptFunction {
    uint32_t function{};
    uint16_t parms_size{};
    // Parameter name -> offset in the parameter block, and bool masks.
    std::map<std::string, uint32_t> offsets, masks;
    bool valid() const {
        return function && parms_size && parms_size <= 0x400;
    }
};
// One call's parameter block, big-endian like guest memory. Strings are
// appended after the parameters and linked by guest address when placed.
class ScriptCall {
  public:
    ScriptCall(uint32_t object, const ScriptFunction &function)
        : object_(object), function_(function.function), parms_(function.parms_size),
          block_(function.parms_size) {}
    uint32_t object() const {
        return object_;
    }
    uint32_t function() const {
        return function_;
    }
    void u32(uint32_t at, uint32_t value) {
        if (at + 4 <= parms_)
            for (unsigned i = 0; i < 4; ++i)
                block_[at + i] = uint8_t(value >> (24 - 8 * i));
    }
    void u8(uint32_t at, uint8_t value) {
        if (at < parms_)
            block_[at] = value;
    }
    // FString {data, count, capacity}; the count includes the terminator.
    void string(uint32_t at, std::string_view text) {
        if (at + 12 > parms_)
            return;
        if (text.empty()) {
            u32(at, 0), u32(at + 4, 0), u32(at + 8, 0);
            return;
        }
        while (block_.size() % 4)
            block_.push_back(0);
        auto data = uint32_t(block_.size());
        for (unsigned char c : text) {
            // ASCII only: anything else becomes a space rather than invalid UTF-16.
            uint16_t unit = c < 0x80 ? c : ' ';
            block_.push_back(uint8_t(unit >> 8));
            block_.push_back(uint8_t(unit));
        }
        block_.push_back(0);
        block_.push_back(0);
        links_.push_back({at, data});
        auto count = uint32_t(text.size() + 1);
        u32(at + 4, count), u32(at + 8, count);
    }
    // The block's bytes once placed at a guest address.
    std::vector<uint8_t> placed(uint32_t address) const {
        auto out = block_;
        for (auto [at, data] : links_) {
            auto value = address + data;
            for (unsigned i = 0; i < 4; ++i)
                out[at + i] = uint8_t(value >> (24 - 8 * i));
        }
        return out;
    }
    size_t size() const {
        return block_.size();
    }

  private:
    uint32_t object_{}, function_{}, parms_{};
    std::vector<uint8_t> block_;
    std::vector<std::pair<uint32_t, uint32_t>> links_;
};
namespace guest_context {
inline constexpr size_t r3 = 0x00, r1 = 0x10, r4 = 0x20, r5 = 0x28, r6 = 0x30, lr = 0x100;
inline uint64_t get(const void *context, size_t at) {
    uint64_t value{};
    std::memcpy(&value, static_cast<const uint8_t *>(context) + at, 8);
    return value;
}
inline void set(void *context, size_t at, uint64_t value) {
    std::memcpy(static_cast<uint8_t *>(context) + at, &value, 8);
}
} // namespace guest_context
// Where a call's frame goes below the current stack pointer: 0x100 bytes of
// linkage space for the callee, then the block, all 16-byte aligned.
inline std::optional<uint32_t> script_call_frame(uint32_t r1, size_t block) {
    uint64_t need = 0x100 + ((uint64_t(block) + 15) & ~uint64_t(15)) + 0x100;
    if (!r1 || r1 >= 0xe0000000u || block > 0x2000 || r1 < need + 0x10000)
        return std::nullopt;
    return uint32_t((r1 - need) & ~0xfu);
}
// Runs ProcessEvent(object, function, parms) through `process_event` with the
// given context, writing guest memory with `write`. Returns false without
// calling when the stack or write is unsuitable.
template <class ProcessEvent, class Write>
bool invoke_script(ProcessEvent process_event, void *context, uint8_t *base, const ScriptCall &call,
                   Write write) {
    using namespace guest_context;
    auto sp = uint32_t(get(context, r1));
    auto frame = script_call_frame(sp, call.size());
    if (!frame || !call.object() || !call.function())
        return false;
    auto parms = *frame + 0x100;
    std::vector<uint8_t> backchain{uint8_t(sp >> 24), uint8_t(sp >> 16), uint8_t(sp >> 8),
                                   uint8_t(sp)};
    auto block = call.placed(parms);
    if (!write(*frame, backchain.data(), backchain.size()) ||
        !write(parms, block.data(), block.size()))
        return false;
    uint64_t saved[6]{get(context, r1), get(context, r3), get(context, r4),
                      get(context, r5), get(context, r6), get(context, lr)};
    set(context, r1, *frame);
    set(context, r3, call.object());
    set(context, r4, call.function());
    set(context, r5, parms);
    set(context, r6, 0);
    process_event(context, base);
    set(context, r1, saved[0]);
    set(context, r3, saved[1]);
    set(context, r4, saved[2]);
    set(context, r5, saved[3]);
    set(context, r6, saved[4]);
    set(context, lr, saved[5]);
    return true;
}
// Reflection lookups, made on the worker thread before any call.
class ScriptLookup {
  public:
    // A loaded object by name, class name and (optionally) outer name.
    static uint32_t find(const Memory &m, Reader &reader, const ReaderLayout &layout,
                         std::string_view name, std::string_view cls, std::string_view outer = {}) {
        auto count = m.u32(layout.objects_global + 4), table = m.u32(layout.objects_global);
        if (!table || count > 250000)
            return 0;
        std::vector<unsigned char> pointers(size_t(count) * 4);
        if (!m.copy(table, pointers.data(), pointers.size()))
            return 0;
        for (uint32_t i = 0; i < count; ++i) {
            const auto *b = pointers.data() + i * 4;
            auto p = uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3];
            if (!p || reader.object_name(m, p) != name || reader.class_name(m, p) != cls)
                continue;
            if (!outer.empty() && reader.object_name(m, m.u32(p + 0x28)) != outer)
                continue;
            return p;
        }
        return 0;
    }
    // A property of `cls` or one of its superclasses: {offset, bool mask}.
    static std::pair<uint32_t, uint32_t> property(const Memory &m, Reader &reader, uint32_t cls,
                                                  std::string_view name) {
        for (unsigned depth = 0; cls && depth < 24; cls = m.u32(cls + 0x40), ++depth)
            for (auto field = m.u32(cls + 0x44), count = 0u; field && count < 4096;
                 field = m.u32(field + 0x3c), ++count)
                if (reader.object_name(m, field) == name) {
                    auto kind = reader.class_name(m, field);
                    return {m.u32(field + 0x54),
                            kind == "BoolProperty" ? m.u32(field + 0x64) : 0xffffffffu};
                }
        return {};
    }
    // A function of `cls` or one of its superclasses, with its parameters.
    static ScriptFunction function(const Memory &m, Reader &reader, uint32_t cls,
                                   std::string_view name) {
        ScriptFunction out;
        for (unsigned depth = 0; cls && depth < 24 && !out.function;
             cls = m.u32(cls + 0x40), ++depth)
            for (auto field = m.u32(cls + 0x44), count = 0u; field && count < 4096;
                 field = m.u32(field + 0x3c), ++count)
                if (reader.object_name(m, field) == name &&
                    reader.class_name(m, field) == "Function") {
                    out.function = field;
                    break;
                }
        if (!out.function)
            return {};
        uint16_t size{};
        uint8_t raw[2]{};
        if (m.copy(out.function + 0x7e, raw, 2))
            size = uint16_t(raw[0] << 8 | raw[1]);
        out.parms_size = size;
        for (auto field = m.u32(out.function + 0x44), count = 0u; field && count < 256;
             field = m.u32(field + 0x3c), ++count) {
            auto offset = m.u32(field + 0x54);
            if (offset >= size)
                continue;
            auto param = reader.object_name(m, field);
            out.offsets[param] = offset;
            if (reader.class_name(m, field) == "BoolProperty")
                out.masks[param] = m.u32(field + 0x64);
        }
        return out;
    }
};
} // namespace a11y
