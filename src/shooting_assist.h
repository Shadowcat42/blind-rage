#pragma once
#include "qte_events.h"

namespace a11y {
struct ConeBounds {
    float minimum{}, maximum{};
    bool operator==(const ConeBounds &) const = default;
};
// The native limit is clamp(Min + (Max - Min) * (1 - distance / 3000), Min, Max):
// LockOnDegMinVal applies at maximum range and LockOnDegMaxVal at point blank.
// Scale each by its multiplier, keep the cone ordered, and never narrow a native
// value. A safety bound caps only the added assistance.
inline std::optional<ConeBounds> boosted_cone(float minimum, float maximum, float pointBlank,
                                              float maxRange,
                                              std::optional<float> safeRadius = {}) {
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum < 0 || maximum < minimum ||
        maximum > 180 || !std::isfinite(pointBlank) || !std::isfinite(maxRange) || pointBlank < 1 ||
        maxRange < 1 || pointBlank > 10 || maxRange > 10 ||
        (safeRadius && (!std::isfinite(*safeRadius) || *safeRadius < 0 || *safeRadius > 180)))
        return {};
    auto low = std::min(minimum * maxRange, 89.f), high = std::min(maximum * pointBlank, 89.f);
    high = std::max(high, low);
    auto bound = [&](float native, float boosted) {
        return std::max(native, safeRadius ? std::min(boosted, *safeRadius) : boosted);
    };
    return ConeBounds{bound(minimum, low), bound(maximum, high)};
}
// Scope only the native game's angular test. Its range, candidate eligibility,
// occlusion trace, acquisition time, lock cap and projectile logic still run.
class AcquisitionScope {
  public:
    using Write = std::function<bool(uint32_t, const void *, size_t)>;
    AcquisitionScope(const Memory &memory, Write write, uint32_t controller, float pointBlank,
                     float maxRange, std::optional<float> safeRadius = {})
        : memory_(memory), write_(std::move(write)), at_(controller + 0x7d4) {
        if (!controller || controller > UINT32_MAX - 0x7dc || (pointBlank <= 1 && maxRange <= 1) ||
            !memory_.copy(at_, original_.data(), original_.size()))
            return;
        auto cone = boosted_cone(std::bit_cast<float>(cue_word(original_.data())),
                                 std::bit_cast<float>(cue_word(original_.data() + 4)), pointBlank,
                                 maxRange, safeRadius);
        if (!cone)
            return;
        auto put = [&](size_t offset, float value) {
            auto bits = std::bit_cast<uint32_t>(value);
            for (unsigned i = 0; i < 4; ++i)
                widened_[offset + i] = uint8_t(bits >> (24 - i * 8));
        };
        put(0, cone->minimum);
        put(4, cone->maximum);
        if (widened_ != original_) {
            changed_ = write_(at_, widened_.data(), widened_.size());
            if (!changed_)
                write_(at_, original_.data(), original_.size());
        }
    }
    ~AcquisitionScope() {
        if (!changed_)
            return;
        std::array<uint8_t, 8> current{};
        if (!memory_.copy(at_, current.data(), current.size()))
            return;
        // Preserve a legitimate game-side change made during the call instead of
        // overwriting it with the saved value.
        for (size_t offset : {size_t(0), size_t(4)})
            if (std::equal(current.begin() + offset, current.begin() + offset + 4,
                           widened_.begin() + offset))
                write_(at_ + uint32_t(offset), original_.data() + offset, 4);
    }
    bool changed() const {
        return changed_;
    }
    AcquisitionScope(const AcquisitionScope &) = delete;
    AcquisitionScope &operator=(const AcquisitionScope &) = delete;

  private:
    const Memory &memory_;
    Write write_;
    uint32_t at_;
    bool changed_{};
    std::array<uint8_t, 8> original_{}, widened_{};
};
}
