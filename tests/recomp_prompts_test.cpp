#include "../src/recomp_prompts.h"
#include "../src/gameplay_text.h"
#include <iostream>
#include <stdexcept>
using namespace a11y;
static void check(bool pass, const char *message) {
    if (!pass)
        throw std::runtime_error(message);
}
static int requested{}, destroyed{};
static std::string binding = "LMB";
static std::map<std::string, std::string> bindings;
static void *key_name(RecompPrompts::NativeString *result, int kind) {
    requested = kind;
    result->size = binding.size();
    result->capacity = binding.size() < 16 ? 15 : binding.size();
    if (result->capacity == 15)
        std::memcpy(result->text, binding.data(), binding.size());
    else
        result->pointer = binding.data();
    return result;
}
static void destroy(RecompPrompts::NativeString *) {
    ++destroyed;
}
static void *bind_value(RecompPrompts::NativeString *result,
                        const RecompPrompts::NativeView *view) {
    auto &text = bindings[std::string(view->data, size_t(view->size))];
    result->size = text.size();
    result->capacity = text.size() < 16 ? 15 : text.size();
    if (result->capacity < 16)
        std::memcpy(result->text, text.data(), text.size());
    else
        result->pointer = text.data();
    return result;
}
int main() {
    try {
        std::array<uint8_t, 16> flags{};
        RecompPrompts prompts;
        check(prompts.initialize_targets(reinterpret_cast<uintptr_t>(flags.data()), {4, 5},
                                         key_name, destroy),
              "initialize verified adapter targets");
        check(prompts.button("X") == "X" && requested == 0,
              "Xbox labels do not query keyboard bindings");
        flags[4] = 1;
        check(prompts.button("A") == "cross" && prompts.button("B") == "circle" &&
                  prompts.button("X") == "square" && prompts.button("Y") == "triangle",
              "PlayStation face positions");
        check(prompts.button("LT") == "L2" && prompts.button("LB") == "L1" &&
                  prompts.button("BACK") == "Select",
              "PlayStation triggers, shoulders and Select");
        flags[5] = 1;
        check(prompts.device() == PromptDevice::keyboard &&
                  prompts.button("X") == "left mouse button" && requested == 3,
              "effective keyboard mode takes precedence and uses runtime prompt kind");
        check(prompts.button("Y") == "left mouse button" && requested == 4,
              "X and Y must not be interchanged");
        check(prompts.button("LB") == "left mouse button" && requested == 5,
              "shoulders precede triggers in runtime prompt kinds");
        prompts.button("LT");
        check(requested == 7, "left trigger mapping");
        prompts.button("RT");
        check(requested == 8, "right trigger mapping");
        binding = "RMB";
        check(prompts.button("B") == "right mouse button" && requested == 2,
              "query fresh runtime label after rail swap or rebinding");
        binding = "a synthetic long key binding";
        check(prompts.button("A") == binding,
              "copy heap-backed native string through bounded adapter");
        check(destroyed == 7, "native strings use the runtime's own destructor");
        prompts.initialize_targets(reinterpret_cast<uintptr_t>(flags.data()), {4, 5}, key_name,
                                   destroy, bind_value);
        bindings["keybind_lstick_down"] = "K,Mouse4";
        check(prompts.gesture("LDown").label == "K" &&
                  prompts.gesture("LDown").action == InputAction::press,
              "keyboard tilt uses current binding rather than a controller-stick instruction");
        bindings["keybind_lstick_right"] = "L";
        check(prompts.gesture("LRightDown").label == "L and K together", "diagonal key chord");
        binding = "ARROWS";
        check(prompts.gesture("RUp").label == "up arrow",
              "complete default-arrow aggregate establishes default key");
        binding = "custom binding set";
        check(prompts.gesture("RUp").label.empty(),
              "empty custom binding does not fabricate default direction");
        bindings["mnk_mouse"] = "false";
        bindings["keybind_lstick_up"] = "I";
        bindings["keybind_lstick_left"] = "J";
        check(prompts.gesture("RRotateL").label == "I, L, K, J in order" &&
                  prompts.gesture("RRotateL").action == InputAction::sequence,
              "mouse-disabled rotation gives a complete remapped circular key sequence");
        bindings["mnk_mouse"] = "true";
        check(prompts.gesture("RRotateR").label == "mouse clockwise" &&
                  prompts.gesture("LRotateR").label == "mouse counterclockwise",
              "with mouse control, right-stick rotations are mouse rotations");
        check(prompts.gesture("RRotateL").label == "I, L, K, J in order",
              "left-stick rotations stay on the left-stick keys with mouse control on");
        check(prompts.gesture("LRotateSet").label ==
                  "I, J, K, L in order, together with mouse counterclockwise",
              "both-stick rotations combine the left keys with the mouse");
        bindings["mnk_mouse"] = "false";
        {
            // Keyboard stick tags in tutorials: the left-stick keys, and the mouse or
            // the right-stick keys depending on the runtime's mouse control.
            binding = "WASD";
            check(prompts.keyboard_stick("LS") == "W A S D", "left stick names its keys");
            bindings["mnk_mouse"] = "true";
            check(prompts.keyboard_stick("RS") == "mouse",
                  "with mouse control the right stick is the mouse");
            bindings["mnk_mouse"] = "false";
            binding = "ARROWS";
            check(prompts.keyboard_stick("RS") == "arrow keys",
                  "without mouse control the right stick is its keys");
            bindings["keybind_rstick_press"] = "C,MMB";
            check(prompts.keyboard_stick("R3") == "C", "right stick press names its first key");
            check(prompts.keyboard_stick("A").empty(), "other tags are not stick names");
            // Custom controls editor: the selected key code, named as the runtime
            // binds it, with the controls screen's action for each bound input.
            std::map<std::string, std::string> actions{
                {"LT", "Unlimited Mode"}, {"A", "Jump"}, {"LS", "Move/Reticle"}};
            bindings["keybind_left_trigger"] = "R";
            bindings["keybind_a"] = "Space,Return";
            bindings["keybind_lstick_up"] = "W";
            bindings["keybind_dpad_up"] = "Up";
            check(prompts.describe_key(82, actions) == "R: Unlimited Mode.",
                  "a key names the action of the button it is bound to");
            check(prompts.describe_key(13, actions) == "Enter: Jump.",
                  "a second key in a binding list is found");
            check(prompts.describe_key(87, actions) == "W: Move/Reticle up.",
                  "stick directions name the stick's action and direction");
            check(prompts.describe_key(38, actions) == "Up: D-pad up.",
                  "D-pad keys name the D-pad");
            check(prompts.describe_key(90, actions) == "Z: unbound.", "unbound keys say so");
            check(prompts.describe_key(7, actions).empty(), "unknown key codes are not guessed");
            // Triggers: named from their bindings, not the runtime's fixed LMB/RMB override.
            binding = "LMB";
            bindings["keybind_right_trigger"] = "F";
            check(prompts.button("LT") == "R" && prompts.button("RT") == "F",
                  "keyboard triggers use their bound keys");
            // Editor rows: the input whose keys are exactly the row's selected keys.
            bindings["keybind_left_shoulder"] = "Q,MMB";
            actions["LB"] = "Lock on target";
            check(prompts.describe_row({4, 81}, actions) ==
                      "Lock on target: middle mouse button, Q.",
                  "a row names the input bound to exactly its keys");
            check(prompts.describe_row({82}, actions) == "Unlimited Mode: R.",
                  "a single-key row names its input");
            check(prompts.describe_row({}, actions) == "No keys bound.", "an empty row says so");
            bindings["keybind_x"] = "E,Mouse4";
            actions["X"] = "Fire";
            check(prompts.describe_row({69}, actions) == "Fire: E.",
                  "a row showing only some of an input's keys still names it");
            bindings["keybind_lstick_down"] = "S";
            bindings["keybind_lstick_left"] = "A";
            bindings["keybind_lstick_right"] = "D";
            check(prompts.describe_row({65, 68, 83, 87}, actions) == "Move/Reticle: A, D, S, W.",
                  "a stick row names the stick across its direction bindings");
            check(prompts.describe_key(5, actions) == "mouse button 4: Fire.",
                  "extra mouse buttons are named");
            check(prompts.describe_row({90}, actions) == "Keys: Z.",
                  "keys matching no input are still listed");
        }
        ButtonLabel label = [&](std::string_view button) { return prompts.button(button); };
        flags[5] = 0;
        check(gameplay_text("Press <X> and &lt;Y&gt;.", label) == "Press square and triangle.",
              "tutorial glyphs use effective device before HTML cleanup");
        flags[4] = 0;
        check(gameplay_text("Press <X>.", label) == "Press X.",
              "device changes are observed on the next announcement");
        flags[5] = 2;
        check(prompts.button("A").empty(), "invalid effective flags never guess an input");
        check(prompts.button("future unknown button").empty(),
              "unsupported commands remain unknown");
        RecompPrompts unavailable;
        check(unavailable.button("A").empty(),
              "uninitialized runtime has no fabricated Xbox fallback");
        std::cout << "Recomp prompt label tests passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
