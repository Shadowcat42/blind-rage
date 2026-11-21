#pragma once
#include <array>
#include <functional>
#include <string>
#include <string_view>

namespace a11y {
enum class PromptDevice { unknown, xbox, playstation, keyboard };
using ButtonLabel = std::function<std::string(std::string_view)>;

// These are the recomp's prompt-kind IDs, not BCGame's command/button enums.
// In particular X is kind 3, Y is kind 4, and shoulders precede triggers.
inline int recomp_prompt_kind(std::string_view button) {
    constexpr std::array<std::string_view, 8> kinds{"A", "B", "X", "Y", "LB", "RB", "LT", "RT"};
    for (size_t i = 0; i < kinds.size(); ++i)
        if (kinds[i] == button)
            return int(i + 1);
    if (button == "START")
        return 12;
    if (button == "BACK")
        return 13;
    return 0;
}
inline std::string controller_button(std::string_view button, PromptDevice device) {
    if (device != PromptDevice::xbox && device != PromptDevice::playstation)
        return {};
    constexpr std::array<std::string_view, 10> xbox{"A",  "B",  "X",  "Y",     "LB",
                                                    "RB", "LT", "RT", "START", "BACK"};
    constexpr std::array<std::string_view, 10> ps{
        "cross", "circle", "square", "triangle", "L1", "R1", "L2", "R2", "Start", "Select"};
    for (size_t i = 0; i < xbox.size(); ++i)
        if (xbox[i] == button)
            return std::string(device == PromptDevice::playstation ? ps[i] : xbox[i]);
    return {};
}
inline std::string spoken_key(std::string value) {
    // PromptKeyName already selects the key currently drawn, including the rail
    // shooting swap. Expand its abbreviations only after obtaining that value.
    if (value == "LMB")
        return "left mouse button";
    if (value == "RMB")
        return "right mouse button";
    if (value == "MMB")
        return "middle mouse button";
    if (value == "ARROWS")
        return "arrow keys";
    if (value == "WASD")
        return "W A S D";
    if (value == "MOUSE")
        return "mouse";
    if (value == "MOUSE_BODY")
        return "move the mouse";
    if (value == "MOUSE_SPIN_CW")
        return "rotate the mouse clockwise";
    if (value == "MOUSE_SPIN_CCW")
        return "rotate the mouse counterclockwise";
    return value;
}
}
