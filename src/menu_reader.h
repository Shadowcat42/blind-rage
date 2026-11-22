#pragma once
#include "build_profile.h"
#include "episode_records.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace a11y {
// Layouts are selected by the fingerprinted build profile. Host pointers are
// never substituted for the game's big-endian, 32-bit guest pointers.
class Memory {
  public:
    using Read = std::function<bool(uint32_t, void *, size_t)>;
    explicit Memory(Read read) : read_(std::move(read)) {}
    bool copy(uint32_t at, void *out, size_t n) const {
        if (!at || n > 16 * 1024 * 1024 || uint64_t(at) + n > 0x100000000ull)
            return false;
        return read_(at, out, n);
    }
    uint32_t u32(uint32_t a) const {
        unsigned char b[4]{};
        if (!copy(a, b, 4))
            return 0;
        return uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3];
    }
    uint8_t u8(uint32_t a) const {
        uint8_t v{};
        copy(a, &v, 1);
        return v;
    }
    double number(uint32_t a) const {
        return std::bit_cast<double>(uint64_t(u32(a)) << 32 | u32(a + 4));
    }
    std::string string(uint32_t a, size_t max = 2048) const {
        std::string s;
        if (!a)
            return s;
        for (size_t i = 0; i < max;) {
            auto at = a + uint32_t(i);
            size_t n = std::min({size_t(64), max - i, size_t(4096 - (at & 4095))});
            char block[64];
            if (!copy(at, block, n))
                return {};
            for (size_t j = 0; j < n; j++) {
                if (!block[j])
                    return s;
                s += block[j];
            }
            i += n;
        }
        return {};
    }

  private:
    Read read_;
};

inline std::string clean_text(const std::string &input) {
    std::string s;
    bool tag = false;
    for (char c : input) {
        if (c == '<') {
            tag = true;
            continue;
        }
        if (c == '>') {
            tag = false;
            s += ' ';
            continue;
        }
        if (!tag)
            s += (static_cast<unsigned char>(c) < 32 ? ' ' : c);
    }
    for (const auto &[from, to] :
         std::array<std::pair<const char *, const char *>, 6>{{{"&amp;", "&"},
                                                               {"&lt;", "<"},
                                                               {"&gt;", ">"},
                                                               {"&quot;", "\""},
                                                               {"&apos;", "'"},
                                                               {"&nbsp;", " "}}}) {
        size_t p = 0;
        while ((p = s.find(from, p)) != std::string::npos) {
            s.replace(p, strlen(from), to);
            p += strlen(to);
        }
    }
    std::string out;
    bool space = true;
    for (char c : s) {
        if (c == ' ') {
            if (!space)
                out += ' ';
            space = true;
        } else {
            out += c;
            space = false;
        }
    }
    if (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}
struct Value {
    uint8_t type{};
    uint32_t pointer{};
    double number{};
    bool boolean{};
};
using Members = std::map<std::string, Value>;
struct Item {
    std::string label, help;
    bool enabled = true;
    bool operator==(const Item &) const = default;
};
struct Menu {
    uint32_t movie{}, selector{};
    std::string heading, path;
    int cursor = -1;
    std::vector<Item> items;
    bool message = false, modal = false, collection = false;
    std::string acquired;
    bool settings = false, pending = false;
    uint32_t selection_key{}, collection_count{};
    int collection_position = -1;
    std::string summary, repeat_text;
    bool readout = false;
    bool choices_only = false;
    bool records = false;
    std::string record_difficulty;
    bool operator==(const Menu &) const = default;
};
inline bool declared_choices_match(const Menu &declaration, const Menu &choice) {
    if (!declaration.choices_only || declaration.pending || declaration.movie != choice.movie ||
        choice.items.empty() || declaration.items.size() != choice.items.size() ||
        choice.cursor < 0 || size_t(choice.cursor) >= choice.items.size())
        return false;
    for (size_t i = 0; i < choice.items.size(); ++i)
        if (choice.items[i].label.empty() || declaration.items[i].label != choice.items[i].label)
            return false;
    return true;
}
// Decides whether the unchanged menu item revealed when a dialog closes should
// be announced. The game's question dialogs confirm with choice 0 and cancel with
// any other choice (BCDialog.GetCursorIdx() == 0 in PauseMenu, IsClosed() == 0 in
// Result). A cancel returns the player to the menu, so it is announced. A
// confirmation, a one-choice acknowledgement or a multi-choice selection leads
// onward (quit, return to title, save on exit), so the briefly revealed item is
// not. Any changed focus or value is announced as usual. Feed every decoded
// state so the dialog's last cursor position is known when it closes.
class DialogReturn {
  public:
    void observe(const std::string &selection, const Menu *top) {
        if (selection.empty())
            return;
        if (top && top->modal) {
            if (!modal_)
                covered_ = underlying_;
            modal_ = true;
            choices_ = top->items.size();
            cursor_ = top->cursor;
            return;
        }
        if (modal_) {
            const bool cancelled = choices_ == 2 && cursor_ != 0;
            quiet_ = !cancelled && selection == covered_ ? selection : std::string();
            modal_ = false;
            covered_.clear();
        } else if (selection != quiet_)
            quiet_.clear();
        underlying_ = selection;
    }
    bool quiet(const std::string &selection) const {
        return !quiet_.empty() && selection == quiet_;
    }

  private:
    std::string underlying_, covered_, quiet_;
    size_t choices_{};
    int cursor_ = -1;
    bool modal_{};
};
inline std::string result_rank(unsigned rank) {
    constexpr std::array<const char *, 6> labels{"Unranked", "S", "A", "B", "C", "D"};
    return rank < labels.size() ? labels[rank] : "";
}
inline std::string result_score(unsigned type, int score) {
    if (score < 0 || type > 2)
        return {};
    if (type == 0)
        return std::to_string(score) + " points";
    if (type == 2)
        return score <= 100 ? std::to_string(score) + " percent" : std::string{};
    auto minutes = score / 60, seconds = score % 60;
    std::string text;
    if (minutes)
        text = std::to_string(minutes) + (minutes == 1 ? " minute" : " minutes");
    if (seconds || !minutes) {
        if (!text.empty())
            text += ' ';
        text += std::to_string(seconds) + (seconds == 1 ? " second" : " seconds");
    }
    return text;
}

inline int collection_grid_index(const Value *cursor, const Value *page, const Value *perPage,
                                 uint32_t count) {
    // The cursor addresses the full category. Page metadata validates the visible
    // window; it is not an offset to add to the already absolute cursor.
    auto integer = [](const Value *value, int maximum) {
        return value && (value->type == 3 || value->type == 4) && std::isfinite(value->number) &&
               value->number >= 0 && value->number <= maximum &&
               std::floor(value->number) == value->number;
    };
    if (!count || count > 1000 || !integer(cursor, 999))
        return -1;
    int index = int(cursor->number);
    if (page || perPage) {
        if (!integer(page, 999) || !integer(perPage, 1000) || perPage->number < 1)
            return -1;
        if (index / int(perPage->number) != int(page->number))
            return -1;
    }
    return index < int(count) ? index : -1;
}

inline bool script_scene_protocol(std::string_view cls) {
    return cls == "BCGFxUI_IntervalDrama" || cls == "BCGFxUI_LastCheck";
}
class Reader {
  public:
    explicit Reader(ReaderLayout layout = disc_profile.layout) : layout_(layout) {}
    void button_labels(std::function<std::string(std::string_view)> labels) {
        button_labels_ = std::move(labels);
    }
    const ReaderLayout &layout() const {
        return layout_;
    }
    std::string name(const Memory &m, uint32_t index) {
        if (index >= m.u32(layout_.names_global + 4) || index > 250000)
            return {};
        auto it = names_.find(index);
        if (it != names_.end())
            return it->second;
        auto p = m.u32(m.u32(layout_.names_global) + index * 4);
        if (!p)
            return {};
        auto s = m.string(p + 8, 256);
        if (!s.empty())
            names_[index] = s;
        return s;
    }
    std::string object_name(const Memory &m, uint32_t p) {
        return p ? name(m, m.u32(p + 0x2c)) : "";
    }
    std::string class_name(const Memory &m, uint32_t p) {
        return object_name(m, m.u32(p + 0x34));
    }
    bool menu_movie_active(const Memory &m, uint32_t movie) {
        if (!(m.u32(movie + 0xc8) & 0x80000000))
            return false;
        if (auto it = script_scenes_.find(movie); it != script_scenes_.end() &&
                                                  it->second.index == m.u32(movie + 0x20) &&
                                                  it->second.cls == m.u32(movie + 0x34))
            return it->second.active && m.u8(movie + 0x174) != 2;
        std::set<uint32_t> seen;
        for (auto cls = m.u32(movie + 0x34); cls && seen.insert(cls).second && seen.size() < 20;
             cls = m.u32(cls + 0x40)) {
            // Common dialogs have their own ActionScript lifecycle. Only menu
            // movies promise to maintain BCGFxMoviePlayer.UIState (+0x174).
            if (object_name(m, cls) == "BCGFxMovieMenu") {
                uint8_t state = 255;
                return m.copy(movie + 0x174, &state, 1) && state < 2;
            }
            if (object_name(m, cls) == "BCGFxUI_Common")
                return true;
        }
        return false;
    }
    void script_scene(const Memory &m, uint32_t movie, uint32_t index, bool active) {
        if (!movie || !index || m.u32(movie + 0x20) != index ||
            !script_scene_protocol(class_name(m, movie)))
            return;
        if (script_scenes_.size() >= 32 && !script_scenes_.contains(movie))
            script_scenes_.erase(script_scenes_.begin());
        script_scenes_[movie] = {index, m.u32(movie + 0x34), active};
    }
    void clear_script_scenes() {
        script_scenes_.clear();
    }
    bool ready(const Memory &m) {
        return name(m, 0) == "None" && name(m, 1) == "ByteProperty" &&
               m.u32(layout_.objects_global + 4) > 1000 &&
               m.u32(layout_.objects_global + 4) < 250000;
    }
    void discover(const Memory &m) {
        movies_.clear();
        if (!ready(m)) {
            discovered_count_ = 0;
            discovered_table_ = 0;
            return;
        }
        auto count = m.u32(layout_.objects_global + 4), table = m.u32(layout_.objects_global);
        bool reset = discovered_table_ != table || count < discovered_count_;
        if (reset)
            menu_classes_.clear();
        std::vector<unsigned char> pointers(size_t(count) * 4);
        if (!m.copy(table, pointers.data(), pointers.size()))
            return;
        auto examine = [&](uint32_t i) {
            const auto *b = pointers.data() + i * 4;
            auto p = uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3];
            if (!p || m.u32(p + 0x20) != i)
                return;
            const auto cls = m.u32(p + 0x34);
            auto cached = menu_classes_.find(cls);
            bool is_menu;
            if (cached != menu_classes_.end())
                is_menu = cached->second;
            else {
                is_menu = false;
                std::set<uint32_t> seen;
                for (auto c = cls; c && seen.insert(c).second && seen.size() < 20;
                     c = m.u32(c + 0x40)) {
                    auto n = object_name(m, c);
                    if (n == "BCGFxMovieMenu" || n == "BCGFxUI_Common") {
                        is_menu = true;
                        break;
                    }
                }
                menu_classes_[cls] = is_menu;
            }
            if (!is_menu || object_name(m, p).starts_with("Default__"))
                return;
            if (std::none_of(movies_.begin(), movies_.end(), [&](const Movie &movie) {
                    return movie.p == p && movie.index == i && movie.cls == cls;
                }))
                movies_.push_back({p, i, cls});
        };
        // Publication and slot reuse do not necessarily change the array's size.
        // Refresh the complete registry; ordinary UI events reuse known movie trees.
        for (uint32_t i = 0; i < count; i++)
            examine(i);
        discovered_count_ = count;
        discovered_table_ = table;
    }
    Members members(const Memory &m, uint32_t object) const {
        Members out;
        auto table = m.u32(object + 0x1c);
        if (!table)
            return out;
        auto count = m.u32(table), mask = m.u32(table + 4);
        if (mask > 4095 || ((mask + 1) & mask) || count > mask + 1)
            return out;
        std::vector<unsigned char> raw(size_t(mask + 1) * 32);
        if (!m.copy(table + 8, raw.data(), raw.size()))
            return out;
        // -2 is a free bucket. -1 is an occupied bucket terminating a collision chain.
        auto r32 = [&](size_t o) {
            auto *b = raw.data() + o;
            return uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3];
        };
        for (uint32_t i = 0; i <= mask; i++) {
            auto off = size_t(i) * 32;
            if (r32(off) == 0xfffffffe)
                continue;
            auto node = r32(off + 8);
            auto key = m.string(m.u32(node), 128);
            if (key.empty())
                continue;
            Value v;
            v.type = raw[off + 16];
            v.pointer = r32(off + 20);
            v.boolean = raw[off + 20] != 0;
            v.number = v.type == 4
                           ? double(int32_t(r32(off + 24)))
                           : std::bit_cast<double>(uint64_t(r32(off + 24)) << 32 | r32(off + 28));
            out.emplace(std::move(key), v);
        }
        if (m.u32(object + 0x1c) != table || m.u32(table) != count || m.u32(table + 4) != mask)
            return {};
        return out;
    }
    static std::optional<Value> get(const Members &v, const std::string &key) {
        auto i = v.find(key);
        return i == v.end() ? std::nullopt : std::optional<Value>(i->second);
    }
    std::string value_text(const Memory &m, const std::optional<Value> &value) const {
        if (!value)
            return {};
        if (value->type == 5)
            return clean_text(m.string(m.u32(value->pointer)));
        if ((value->type == 3 || value->type == 4) && std::isfinite(value->number)) {
            char s[64];
            snprintf(s, sizeof(s), "%.6g", value->number);
            return s;
        }
        return {};
    }
    std::vector<uint32_t> children(const Memory &m, uint32_t c) const {
        if (m.u32(c) != layout_.sprite_vtable)
            return {};
        auto p = m.u32(c + 0xc4), n = m.u32(c + 0xc8), cap = m.u32(c + 0xcc);
        if (n > 2048 || n > cap || cap > 4096)
            return {};
        std::vector<uint32_t> out;
        for (uint32_t i = 0; i < n; i++) {
            auto child = m.u32(p + i * 4);
            if (child)
                out.push_back(child);
        }
        return out;
    }
    std::string character_name(const Memory &m, uint32_t c) const {
        auto h = m.u32(c + 0x94);
        return m.string(m.u32(m.u32(h + 0xc)), 512);
    }
    std::vector<std::string> text(const Memory &m, uint32_t root, size_t limit = 500) const {
        std::vector<std::string> out;
        std::vector<uint32_t> todo{root};
        std::set<uint32_t> seen;
        while (!todo.empty() && seen.size() < limit) {
            auto c = todo.back();
            todo.pop_back();
            if (!c || !seen.insert(c).second || !visible(m, c))
                continue;
            if (m.u32(c) == layout_.text_vtable) {
                auto s = native_text(m, c);
                if (!s.empty() && std::find(out.begin(), out.end(), s) == out.end())
                    out.push_back(std::move(s));
                continue;
            }
            if (m.u32(c) != layout_.sprite_vtable)
                continue;
            auto fields = members(m, m.u32(c + 0x1c8));
            auto v = get(fields, "m_text");
            if (!v)
                v = get(fields, "_text");
            auto s = value_text(m, v);
            if (!s.empty() && std::find(out.begin(), out.end(), s) == out.end())
                out.push_back(std::move(s));
            auto kids = children(m, c);
            todo.insert(todo.end(), kids.rbegin(), kids.rend());
        }
        return out;
    }
    bool visible(const Memory &m, uint32_t c) const {
        std::set<uint32_t> seen;
        while (c && seen.insert(c).second && seen.size() < 64) {
            auto flags = m.u32(c + 0xa0);
            if (!(flags & 1) || (flags & 2))
                return false;
            c = m.u32(c + 0x20);
        }
        return c == 0;
    }
    static bool delegated_selector_owner(const Members &fields) {
        auto main = get(fields, "m_mainItems"), sub = get(fields, "m_subItems");
        return main && main->type == 6 && sub && sub->type == 6 && get(fields, "m_mainIdx") &&
               get(fields, "m_subIdx") && get(fields, "m_proc");
    }
    static bool delegated_selector_ready(const Members &fields) {
        auto input = get(fields, "m_input"), phase = get(fields, "m_proc");
        return delegated_selector_owner(fields) && input && input->type == 2 && input->boolean &&
               phase && (phase->type == 3 || phase->type == 4) && phase->number == 0;
    }
    static bool selector_owner_inactive(const Members &fields, bool delegated = false) {
        if (delegated_selector_owner(fields) && !delegated_selector_ready(fields))
            return true;
        auto input = get(fields, "m_input");
        if (!input || input->type != 2 || input->boolean)
            return false;
        // Delegated controllers can keep m_input=false while a child owns input.
        // Only explicit handoff phases or owners whose whole handler uses this
        // flag block descendants. These apply within an open native movie too.
        const bool ownsInput = (get(fields, "m_closeOperation") && get(fields, "m_proc")) ||
                               (get(fields, "m_instBar") && get(fields, "m_data")) ||
                               delegated_selector_owner(fields);
        if (ownsInput)
            return true;
        for (auto key : {"m_decide", "m_gameDiffHelp"}) {
            // The recomp's delegated list controller can drive a child whose
            // original handler remains decided and disabled. That child flag is
            // not a handoff while its owner is accepting menu input.
            if (delegated && std::string_view(key) == "m_decide")
                continue;
            if (auto phase = get(fields, key); phase && phase->type == 2 && phase->boolean)
                return true;
        }
        return false;
    }
    bool selector_input_active(const Memory &m, uint32_t c) const {
        std::vector<Members> ancestors;
        std::set<uint32_t> seen;
        while (c && seen.insert(c).second && seen.size() < 64) {
            auto fields = members(m, m.u32(c + 0x1c8));
            // Dialog choices follow their own lifecycle, including pending entry.
            if (auto dialog = get(fields, "tl_opend"); dialog && dialog->type == 2)
                return true;
            ancestors.push_back(std::move(fields));
            c = m.u32(c + 0x20);
        }
        if (c)
            return false;
        bool delegated = false;
        for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
            if (delegated_selector_owner(*it))
                delegated = delegated_selector_ready(*it);
            if (selector_owner_inactive(*it, delegated))
                return false;
        }
        return true;
    }
    std::string native_text(const Memory &m, uint32_t field) const {
        auto view = m.u32(field + 0xbc), styled = m.u32(view + 8), paragraphs = m.u32(styled + 0xc),
             count = m.u32(styled + 0x10), capacity = m.u32(styled + 0x14);
        if (!view || !styled || !paragraphs || count > 128 || count > capacity || capacity > 512)
            return {};
        std::string out;
        for (uint32_t i = 0; i < count; i++) {
            auto paragraph = m.u32(paragraphs + i * 4), data = m.u32(paragraph),
                 length = m.u32(paragraph + 4), cap = m.u32(paragraph + 8);
            if (!data || length > 4096 || length > cap)
                return {};
            if (!out.empty())
                out += ' ';
            for (uint32_t j = 0; j < length; j++) {
                uint32_t u = uint32_t(m.u8(data + j * 2)) << 8 | m.u8(data + j * 2 + 1);
                if (!u)
                    break;
                if (u >= 0xd800 && u <= 0xdbff && j + 1 < length) {
                    auto v = uint32_t(m.u8(data + (j + 1) * 2)) << 8 | m.u8(data + (j + 1) * 2 + 1);
                    if (v >= 0xdc00 && v <= 0xdfff) {
                        u = 0x10000 + ((u - 0xd800) << 10) + (v - 0xdc00);
                        j++;
                    } else
                        return {};
                } else if (u >= 0xdc00 && u <= 0xdfff)
                    return {};
                if (u < 0x80)
                    out += char(u);
                else if (u < 0x800) {
                    out += char(0xc0 | (u >> 6));
                    out += char(0x80 | (u & 63));
                } else if (u < 0x10000) {
                    out += char(0xe0 | (u >> 12));
                    out += char(0x80 | ((u >> 6) & 63));
                    out += char(0x80 | (u & 63));
                } else {
                    out += char(0xf0 | (u >> 18));
                    out += char(0x80 | ((u >> 12) & 63));
                    out += char(0x80 | ((u >> 6) & 63));
                    out += char(0x80 | (u & 63));
                }
            }
        }
        return clean_text(out);
    }
    struct Results {
        uint32_t root{};
        bool main{}, detail{}, pending{};
        std::string summary, breakdown;
    };
    std::optional<Results> results(const Memory &m, uint32_t root, const Members &fields) const {
        auto control = [&](const Members &values, const char *key) {
            auto v = get(values, key);
            return v && v->type == 7 ? m.u32(v->pointer + 4) : 0;
        };
        auto estimates = control(fields, "m_instEstimList"),
             total = control(fields, "m_instTotalRank"), detail = control(fields, "m_instDetail");
        if (!estimates || !total || !detail)
            return {};
        Results out;
        out.root = root;
        out.main = visible(m, estimates) && visible(m, total);
        out.detail = visible(m, detail) && !out.main;
        auto input = get(fields, "m_input");
        out.pending = !input || input->type != 2 || !input->boolean;
        auto number = [&](const std::optional<Value> &value, int low,
                          int high) -> std::optional<int> {
            if (!value || (value->type != 3 && value->type != 4) || !std::isfinite(value->number) ||
                std::floor(value->number) != value->number || value->number < low ||
                value->number > high)
                return {};
            return int(value->number);
        };
        auto rank = [&](uint32_t node) -> std::string {
            std::vector<uint32_t> todo{node};
            std::set<uint32_t> seen;
            std::optional<int> found;
            while (!todo.empty() && seen.size() < 64) {
                auto c = todo.back();
                todo.pop_back();
                if (!c || !seen.insert(c).second)
                    continue;
                auto data = members(m, m.u32(c + 0x1c8));
                if (auto value = get(data, "m_rankIdx")) {
                    auto valueRank = number(value, 0, 5);
                    if (!valueRank || (found && found != valueRank))
                        return {};
                    found = valueRank;
                }
                auto kids = children(m, c);
                todo.insert(todo.end(), kids.begin(), kids.end());
            }
            return found ? result_rank(unsigned(*found)) : std::string{};
        };
        if (out.main) {
            auto totalData = members(m, m.u32(total + 0x1c8));
            auto finish = get(totalData, "m_finish");
            auto totalRank = rank(control(totalData, "m_rank"));
            if (!finish || finish->type != 2 || !finish->boolean || totalRank.empty())
                out.pending = true;
            else
                out.summary = "Overall rank " + totalRank + ".";
            unsigned count = 0;
            std::set<int> types;
            for (auto row : children(m, estimates)) {
                auto data = members(m, m.u32(row + 0x1c8));
                auto scoreClass = get(data, "m_scoreClass");
                if (!scoreClass || scoreClass->type != 6)
                    continue;
                auto done = get(data, "m_finish");
                auto type = number(get(data, "m_scoreType"), 0, 2);
                auto score = number(get(members(m, scoreClass->pointer), "m_score"), 0, INT32_MAX);
                auto grade = rank(control(data, "m_rank"));
                if (!visible(m, row) || !done || done->type != 2 || !done->boolean || !type ||
                    !score || grade.empty() || !types.insert(*type).second) {
                    out.pending = true;
                    continue;
                }
                auto value = result_score(unsigned(*type), *score);
                if (value.empty()) {
                    out.pending = true;
                    continue;
                }
                std::string label;
                for (const auto &line : text(m, control(data, "m_text"))) {
                    if (!label.empty())
                        label += ' ';
                    label += line;
                }
                if (label.empty())
                    label = *type == 0   ? "Battle points"
                            : *type == 1 ? "Clear time"
                                         : "Synchronic rate";
                if (!out.summary.empty())
                    out.summary += ' ';
                out.summary += label + ": " + value + ". Rank " + grade + ".";
                ++count;
            }
            if (count != 3)
                out.pending = true;
        }
        if (out.detail) {
            std::vector<uint32_t> todo{detail};
            std::set<uint32_t> seen;
            unsigned rows = 0;
            while (!todo.empty() && seen.size() < 512) {
                auto c = todo.back();
                todo.pop_back();
                if (!seen.insert(c).second || !visible(m, c))
                    continue;
                auto kids = children(m, c);
                std::string label, points, count;
                for (auto child : kids) {
                    auto path = character_name(m, child);
                    auto lines = text(m, child);
                    std::string value;
                    for (const auto &line : lines) {
                        if (!value.empty())
                            value += ' ';
                        value += line;
                    }
                    if (path.ends_with(".mcInst_Text"))
                        label = value;
                    else if (path.ends_with(".mcInst_Point"))
                        points = value;
                    else if (path.ends_with(".mcInst_Num"))
                        count = value;
                }
                if (!label.empty() && !points.empty()) {
                    if (++rows > 32) {
                        out.pending = true;
                        break;
                    }
                    if (!out.breakdown.empty())
                        out.breakdown += ' ';
                    out.breakdown += label + ": " + (count.empty() ? std::string{} : count + ", ") +
                                     points + " points.";
                } else
                    todo.insert(todo.end(), kids.rbegin(), kids.rend());
            }
            if (!rows)
                out.pending = true;
            else
                out.breakdown = "Battle point breakdown. " + out.breakdown;
        }
        return out;
    }
    void record_info(uint32_t movie, uint32_t index, std::string story, int difficulty,
                     std::string_view data) {
        record_browser_.capture(movie, index, std::move(story), difficulty, data);
    }
    std::optional<Menu> navigate_record(const Memory &m, uint32_t movie, int command) {
        if (command < 1 || command > 4 || record_browser_.movie != movie)
            return {};
        if (m.u32(movie + 0x20) != record_browser_.index || !record_browser_.panel ||
            !menu_movie_active(m, movie) || !visible(m, record_browser_.panel)) {
            record_browser_.panel = 0;
            return {};
        }
        // The accessibility cursor browses an immutable record table. Revalidate
        // the live episode identity, rather than decoding every widget per press.
        auto fields = members(m, m.u32(record_browser_.root + 0x1c8));
        auto data = get(fields, "m_data"), cursor = get(fields, "m_cursor");
        if (!data || data->type != 6 || !cursor || (cursor->type != 3 && cursor->type != 4) ||
            !std::isfinite(cursor->number) || cursor->number < 0 || cursor->number > 255 ||
            std::floor(cursor->number) != cursor->number)
            return {};
        auto count = m.u32(data->pointer + 0x3c), entries = m.u32(data->pointer + 0x38);
        if (!entries || count > 256 || uint32_t(cursor->number) >= count)
            return {};
        auto entry = m.u32(entries + uint32_t(cursor->number) * 4);
        if (m.u8(entry) != 6 ||
            value_text(m, get(members(m, m.u32(entry + 4)), "Tag")) != record_browser_.story)
            return {};
        record_browser_.move(command);
        auto menu = record_panels_[record_browser_.difficulty];
        menu.cursor = record_browser_.stat;
        if (!menu.records || menu.items.size() != 4)
            return {};
        menu.repeat_text = menu.items[menu.cursor].label;
        return menu;
    }
    bool move_record(const Memory &m, uint32_t movie, int command) {
        return navigate_record(m, movie, command).has_value();
    }
    std::optional<Menu> record_menu(const Memory &m, uint32_t movie, const Members &fields) {
        auto reference = get(fields, "m_instRecord"), data = get(fields, "m_data"),
             cursor = get(fields, "m_cursor");
        if (!reference || reference->type != 7 || !data || data->type != 6 || !cursor ||
            (cursor->type != 3 && cursor->type != 4) || !std::isfinite(cursor->number) ||
            cursor->number < 0 || cursor->number > 255 ||
            std::floor(cursor->number) != cursor->number)
            return {};
        auto panel = m.u32(reference->pointer + 4);
        if (!panel || !visible(m, panel))
            return {};
        auto count = m.u32(data->pointer + 0x3c), entries = m.u32(data->pointer + 0x38);
        if (!entries || count > 256 || uint32_t(cursor->number) >= count)
            return {};
        auto entry = m.u32(entries + uint32_t(cursor->number) * 4);
        if (m.u8(entry) != 6)
            return {};
        auto selected = members(m, m.u32(entry + 4));
        auto story = value_text(m, get(selected, "Tag"));
        auto ranks = value_text(m, get(selected, "Rank"));
        if (story.empty() || ranks.size() != 3 ||
            ranks.find_first_not_of("SABCD-") != std::string::npos)
            return {};
        auto index = m.u32(movie + 0x20);
        if (record_browser_.movie != movie || record_browser_.index != index ||
            record_browser_.story != story) {
            record_browser_ = {};
            record_browser_.movie = movie;
            record_browser_.index = index;
            record_browser_.story = story;
        }
        struct Column {
            std::string label;
            std::array<RecordStat, 3> stats;
        };
        std::array<Column, 3> columns;
        for (auto group : children(m, panel))
            for (auto column : children(m, group)) {
                auto path = character_name(m, column);
                int diff = path.ends_with(".mcInst_Diff1")   ? 0
                           : path.ends_with(".mcInst_Diff2") ? 1
                           : path.ends_with(".mcInst_Diff3") ? 2
                                                             : -1;
                if (diff < 0 || !visible(m, column))
                    continue;
                for (auto child : children(m, column)) {
                    auto name = character_name(m, child);
                    if (name.ends_with(".mcInst_Diff")) {
                        auto lines = text(m, child);
                        if (lines.size() == 1)
                            columns[diff].label = lines[0];
                    }
                    int stat = name.ends_with(".mcInst_Record1")   ? 0
                               : name.ends_with(".mcInst_Record2") ? 1
                               : name.ends_with(".mcInst_Record3") ? 2
                                                                   : -1;
                    if (stat < 0)
                        continue;
                    for (auto part : children(m, child)) {
                        auto lines = text(m, part);
                        if (lines.size() != 1)
                            continue;
                        auto partName = character_name(m, part);
                        if (partName.ends_with(".mcInst_Index"))
                            columns[diff].stats[stat].label = lines[0];
                        if (partName.ends_with(".mcInst_Param"))
                            columns[diff].stats[stat].value = lines[0];
                    }
                }
            }
        for (const auto &column : columns) {
            if (column.label.empty())
                return {};
            for (const auto &stat : column.stats)
                if (stat.label.empty() || stat.value.empty())
                    return {};
        }
        if (record_browser_.panel != panel) {
            record_browser_.panel = panel;
            record_browser_.difficulty = 1;
            record_browser_.stat = 0;
            auto base = get(fields, "m_instBase");
            if (base && base->type == 7)
                for (const auto &line : text(m, m.u32(base->pointer + 4)))
                    for (int i = 0; i < 3; ++i)
                        if (line == columns[i].label)
                            record_browser_.difficulty = i;
        }
        record_browser_.root = m.u32(panel + 0x20);
        for (int diff = 0; diff < 3; ++diff) {
            Menu menu;
            menu.movie = movie;
            menu.selector = panel;
            menu.records = menu.readout = true;
            menu.record_difficulty = columns[diff].label;
            menu.selection_key = uint32_t(diff);
            menu.heading = "Episode records";
            auto repeatButton = button_labels_ ? button_labels_("X") : std::string("Square/X");
            if (!repeatButton.empty())
                repeatButton[0] = char(std::toupper(static_cast<unsigned char>(repeatButton[0])));
            menu.summary =
                "Right stick left or right browses difficulties. Up or down browses stats.";
            if (!repeatButton.empty())
                menu.summary += " " + repeatButton + " repeats.";
            menu.cursor = record_browser_.stat;
            menu.items.push_back(
                {"Overall rank " +
                     (ranks[diff] == '-' ? std::string("Unranked") : std::string(1, ranks[diff])) +
                     ".",
                 "", true});
            for (unsigned i = 0; i < 3; ++i) {
                const auto &stat = columns[diff].stats[i];
                std::string line = stat.label + ": " + stat.value + ".";
                if (record_browser_.scores[diff]) {
                    const auto &cached = (*record_browser_.scores[diff])[i];
                    if (cached.label == stat.label && cached.value == stat.value)
                        line += " Rank " + cached.rank + ".";
                }
                menu.items.push_back({std::move(line), "", true});
            }
            menu.repeat_text = menu.items[menu.cursor].label;
            record_panels_[diff] = std::move(menu);
        }
        return record_panels_[record_browser_.difficulty];
    }
    std::vector<Menu> menus(const Memory &m) {
        std::vector<Menu> out;
        bool inactiveSelection = false;
        for (auto movie : movies_) {
            if (m.u32(movie.p + 0x20) != movie.index || m.u32(movie.p + 0x34) != movie.cls)
                continue;
            // Loaded movie trees outlive their interactive menu during closing.
            if (!menu_movie_active(m, movie.p))
                continue;
            const bool illustrationScene = class_name(m, movie.p) == "BCGFxUI_IntervalDrama";
            std::optional<Results> result;
            std::unordered_set<uint32_t> seen;
            for (auto field : {0x180u, 0x17cu}) {
                auto gfx = m.u32(movie.p + field);
                if (!gfx || m.u32(gfx + 0x40) != 0x48)
                    continue;
                auto handle = m.u32(gfx + 0x44), root = m.u32(handle + 4);
                std::vector<uint32_t> todo{root};
                while (!todo.empty() && seen.size() < 3000) {
                    auto c = todo.back();
                    todo.pop_back();
                    if (!c || !seen.insert(c).second || m.u32(c) != layout_.sprite_vtable ||
                        !visible(m, c))
                        continue;
                    auto fields = members(m, m.u32(c + 0x1c8));
                    // Read only the active illustration text container. AddText
                    // can preload future pages and is not a narration trigger.
                    if (illustrationScene) {
                        auto content = get(fields, "m_instText"), shown = get(fields, "m_visible");
                        if (content && content->type == 7 && shown && shown->type == 2 &&
                            shown->boolean) {
                            auto display = m.u32(content->pointer + 4);
                            std::string body;
                            for (const auto &line : text(m, display)) {
                                if (!body.empty())
                                    body += ' ';
                                body += line;
                            }
                            if (!body.empty()) {
                                Menu scene;
                                scene.movie = movie.p;
                                scene.selector = display;
                                scene.cursor = 0;
                                scene.message = scene.readout = true;
                                auto position = get(fields, "m_textIdx");
                                if (position && (position->type == 3 || position->type == 4) &&
                                    std::isfinite(position->number) && position->number >= 0 &&
                                    position->number <= 65535 &&
                                    std::floor(position->number) == position->number)
                                    scene.selection_key = uint32_t(position->number);
                                if (auto page = get(fields, "m_instPage"); page && page->type == 7)
                                    for (const auto &label : text(m, m.u32(page->pointer + 4)))
                                        scene.selection_key ^=
                                            uint32_t(std::hash<std::string>{}(label));
                                // ChangeText sets m_textIdx and disables input before
                                // the old words animate out; SetText only shows the new
                                // passage afterwards. Until input returns the visible
                                // text may be the previous passage, so it is not ready.
                                auto input = get(fields, "m_input");
                                scene.pending = input && input->type == 2 && !input->boolean;
                                scene.items.push_back({body, "", true});
                                scene.repeat_text = body;
                                out.push_back(std::move(scene));
                            }
                        }
                    }
                    if (auto record = record_menu(m, movie.p, fields))
                        out.push_back(std::move(*record));
                    auto controlsActive = get(fields, "m2active"),
                         controlsForm = get(fields, "m2op");
                    if (controlsActive && controlsActive->type == 2 && controlsActive->boolean &&
                        controlsForm && controlsForm->type == 2 && controlsForm->boolean) {
                        Menu controls;
                        controls.movie = movie.p;
                        controls.selector = c;
                        controls.cursor = 0;
                        controls.message = controls.readout = true;
                        std::string description = "Controls.";
                        auto type = get(fields, "m2type");
                        if (type && (type->type == 3 || type->type == 4) &&
                            std::isfinite(type->number) && type->number >= 0 && type->number <= 3 &&
                            std::floor(type->number) == type->number) {
                            constexpr const char *types[]{"Type A.", "Type B.", "Type C.",
                                                          "Custom."};
                            description += std::string(" ") + types[size_t(type->number)];
                        }
                        unsigned rows = 0;
                        for (auto child : children(m, c)) {
                            auto path = character_name(m, child);
                            auto suffix = path.substr(path.find_last_of('.') + 1);
                            if (!suffix.starts_with("mcInst_"))
                                continue;
                            auto token = suffix.substr(7);
                            if (token == "Start" || token == "Back")
                                std::transform(
                                    token.begin(), token.end(), token.begin(),
                                    [](unsigned char ch) { return char(std::toupper(ch)); });
                            if (token != "A" && token != "B" && token != "X" && token != "Y" &&
                                token != "LB" && token != "RB" && token != "LT" && token != "RT" &&
                                token != "LS" && token != "RS" && token != "R3" &&
                                token != "START" && token != "BACK")
                                continue;
                            auto labels = text(m, child);
                            if (labels.empty())
                                continue;
                            auto button = button_labels_ ? button_labels_(token) : token;
                            description += " ";
                            if (!button.empty())
                                description += button + ": ";
                            for (const auto &label : labels)
                                description += label + ". ";
                            ++rows;
                        }
                        if (rows) {
                            controls.items.push_back({clean_text(description), "", true});
                            out.push_back(std::move(controls));
                        }
                    }
                    auto cur = get(fields, "m_cursor"), items = get(fields, "m_items");
                    auto data = get(fields, "m_data"), bar = get(fields, "m_instBar");
                    auto phase = get(fields, "m_state"), ownerInput = get(fields, "m_input");
                    if (data && data->type == 6 && bar && bar->type == 7 && ownerInput &&
                        ownerInput->type == 2 && ownerInput->boolean && phase &&
                        (phase->type == 3 || phase->type == 4) && phase->number == 0) {
                        auto selector = m.u32(bar->pointer + 4);
                        auto barFields = members(m, m.u32(selector + 0x1c8));
                        auto selection = get(barFields, "m_select"),
                             cursor = get(barFields, "m_cursor");
                        if (selection && selection->type == 6 && cursor &&
                            (cursor->type == 3 || cursor->type == 4)) {
                            auto count = m.u32(selection->pointer + 0x3c);
                            auto records = m.u32(data->pointer + 0x3c);
                            if (count && count <= 256 && records && records <= 256 &&
                                std::isfinite(cursor->number) && cursor->number >= 0 &&
                                cursor->number < count &&
                                std::floor(cursor->number) == cursor->number) {
                                Menu episodes;
                                episodes.movie = movie.p;
                                episodes.selector = selector;
                                episodes.heading = "Episode Menu";
                                episodes.cursor = int(cursor->number);
                                auto indexes = m.u32(selection->pointer + 0x38);
                                auto entries = m.u32(data->pointer + 0x38);
                                for (uint32_t i = 0; i < count; ++i) {
                                    auto value = m.u32(indexes + i * 4);
                                    auto type = m.u8(value);
                                    double index = type == 4
                                                       ? double(int32_t(m.u32(value + 8)))
                                                       : std::bit_cast<double>(
                                                             (uint64_t(m.u32(value + 8)) << 32) |
                                                             m.u32(value + 12));
                                    if ((type != 3 && type != 4) || !std::isfinite(index) ||
                                        index < 0 || index >= records || std::floor(index) != index)
                                        break;
                                    auto record = m.u32(entries + uint32_t(index) * 4);
                                    if (m.u8(record) != 6)
                                        break;
                                    auto row = members(m, m.u32(record + 4));
                                    Item item;
                                    item.label = value_text(m, get(row, "No"));
                                    auto title = value_text(m, get(row, "Title"));
                                    if (item.label.empty() || title.empty())
                                        break;
                                    item.label += ": " + title;
                                    auto rank = value_text(m, get(row, "Rank"));
                                    std::erase(rank, '-');
                                    if (!rank.empty())
                                        item.help = "Rank " + rank;
                                    if (auto open = get(row, "Open"); open && open->type == 2)
                                        item.enabled = open->boolean;
                                    episodes.items.push_back(std::move(item));
                                }
                                if (episodes.items.size() == count && visible(m, selector))
                                    out.push_back(std::move(episodes));
                            }
                        }
                    }
                    auto details = get(fields, "m_details"), input = get(fields, "m_input");
                    auto customActive = get(fields, "m2on"), customRows = get(fields, "m2rows");
                    const bool customSettings = customActive && customActive->type == 2 &&
                                                customActive->boolean && customRows &&
                                                customRows->type == 6;
                    if (details && details->type == 6 && cur &&
                        (cur->type == 3 || cur->type == 4) &&
                        (customSettings || (input && input->type == 2 && input->boolean)) &&
                        visible(m, c)) {
                        auto ptr = m.u32(details->pointer + 0x38),
                             count = m.u32(details->pointer + 0x3c);
                        if (count && count < 128 && std::isfinite(cur->number) &&
                            cur->number >= 0 && cur->number < count &&
                            std::floor(cur->number) == cur->number) {
                            Menu settings;
                            settings.movie = movie.p;
                            settings.selector = c;
                            settings.cursor = int(cur->number);
                            settings.settings = true;
                            auto path = character_name(m, c);
                            settings.heading = path.substr(path.find_last_of('.') + 1);
                            if (settings.heading.starts_with("mcInst_"))
                                settings.heading.erase(0, 7);
                            if (settings.heading.starts_with("Mark2"))
                                settings.heading.erase(0, 5);
                            if (settings.heading == "Snd")
                                settings.heading = "Sound";
                            if (settings.heading == "Gfx")
                                settings.heading = "Graphics";
                            auto kids = children(m, c);
                            auto findChild = [&](const std::string &name) {
                                for (auto child : kids) {
                                    auto p = character_name(m, child);
                                    if (p.ends_with("." + name))
                                        return child;
                                }
                                return uint32_t(0);
                            };
                            auto headingText = text(m, findChild("mcInst_Title"));
                            if (headingText.size() == 1)
                                settings.heading = headingText.front();
                            for (uint32_t i = 0; i < count; i++) {
                                auto record = m.u32(ptr + i * 4);
                                if (m.u8(record) != 6) {
                                    settings.items.clear();
                                    break;
                                }
                                auto row = members(m, m.u32(record + 4));
                                Item item;
                                auto title = findChild(value_text(m, get(row, "Title"))),
                                     control = findChild(value_text(m, get(row, "Sel")));
                                // Recomp settings forms keep their display rows separately from
                                // the native help records. The title includes the current value.
                                if (customSettings && m.u32(customRows->pointer + 0x3c) == count) {
                                    auto rows = m.u32(customRows->pointer + 0x38);
                                    auto entry = m.u32(rows + i * 4);
                                    if (m.u8(entry) == 6) {
                                        auto display = get(members(m, m.u32(entry + 4)), "row");
                                        if (display && display->type == 7) {
                                            auto container = m.u32(display->pointer + 4);
                                            for (auto child : children(m, container))
                                                if (character_name(m, child).ends_with(
                                                        ".mcInst_Title"))
                                                    title = child;
                                        }
                                    }
                                }
                                for (auto &label : text(m, title)) {
                                    if (!item.label.empty())
                                        item.label += " ";
                                    item.label += label;
                                }
                                auto literal = value_text(m, get(row, "Title"));
                                if (item.label.empty() && !literal.starts_with("mcInst_"))
                                    item.label = literal;
                                if (item.label.empty())
                                    if (auto instanceName = get(row, "InstName");
                                        instanceName && instanceName->type == 5) {
                                        auto name = value_text(m, instanceName);
                                        uint32_t container = c;
                                        if (auto selector = get(fields, "m_instSelector");
                                            selector && selector->type == 7)
                                            container = m.u32(selector->pointer + 4);
                                        for (auto child : children(m, container))
                                            if (character_name(m, child).ends_with("." + name)) {
                                                for (auto &label : text(m, child)) {
                                                    if (!item.label.empty())
                                                        item.label += " ";
                                                    item.label += label;
                                                }
                                                break;
                                            }
                                    }
                                auto controlFields = members(m, m.u32(control + 0x1c8));
                                auto value = get(controlFields, "m_param");
                                std::string valueLabel;
                                if (get(controlFields, "m_move")) {
                                    if (item.label.find('%') == std::string::npos)
                                        valueLabel = value_text(m, value);
                                } else {
                                    std::vector<std::string> choices;
                                    for (auto suffix : {"mcInst_ChoiceL", "mcInst_ChoiceR"})
                                        for (auto child : children(m, control))
                                            if (character_name(m, child).ends_with(
                                                    std::string(".") + suffix)) {
                                                std::string option;
                                                for (auto &t : text(m, child)) {
                                                    if (!option.empty())
                                                        option += ' ';
                                                    option += t;
                                                }
                                                choices.push_back(option);
                                            }
                                    if (value && (value->type == 3 || value->type == 4) &&
                                        value->number >= 0 && value->number < choices.size() &&
                                        std::floor(value->number) == value->number)
                                        valueLabel = choices[size_t(value->number)];
                                    if (valueLabel.empty())
                                        valueLabel = value_text(m, value);
                                }
                                if (!valueLabel.empty())
                                    item.label += ", " + valueLabel;
                                item.help = value_text(m, get(row, "HelpText"));
                                if (item.help.empty())
                                    item.help = value_text(m, get(row, "Help"));
                                settings.items.push_back(std::move(item));
                            }
                            if (settings.items.size() == count &&
                                !settings.items[settings.cursor].label.empty())
                                out.push_back(std::move(settings));
                        }
                    }
                    auto info = get(fields, "m_info"), param = get(fields, "m_param");
                    if (info && info->type == 6 && visible(m, c) &&
                        (!input || (input->type == 2 && input->boolean))) {
                        auto data = members(m, info->pointer);
                        auto list = get(data, "m_items");
                        if (list && list->type == 6) {
                            auto array = m.u32(list->pointer + 0x38),
                                 count = m.u32(list->pointer + 0x3c),
                                 capacity = m.u32(list->pointer + 0x40);
                            if (count && count < 1000 && count <= capacity && capacity <= 4096) {
                                uint32_t record = param && param->type == 6 ? param->pointer : 0;
                                int position = -1;
                                // Resolve either an explicit selected record or the shared paged list.
                                if (!record)
                                    if (auto instance = get(fields, "m_instList");
                                        instance && instance->type == 7) {
                                        auto control = m.u32(instance->pointer + 4);
                                        auto grid = members(m, m.u32(control + 0x1c8));
                                        auto cursor = get(grid, "m_cursor"),
                                             page = get(grid, "m_pageNow"),
                                             perPage = get(grid, "m_pageItemMax");
                                        position = collection_grid_index(
                                            cursor ? &*cursor : nullptr, page ? &*page : nullptr,
                                            perPage ? &*perPage : nullptr, count);
                                        if (position >= 0) {
                                            auto value = m.u32(array + uint32_t(position) * 4);
                                            if (m.u8(value) == 6)
                                                record = m.u32(value + 4);
                                        }
                                    }
                                auto selected = members(m, record);
                                auto owned = get(selected, "Get");
                                if (record && owned && owned->type == 2) {
                                    Menu detail;
                                    detail.movie = movie.p;
                                    detail.selector = c;
                                    detail.cursor = 0;
                                    detail.collection = true;
                                    detail.selection_key = record;
                                    detail.collection_count = count;
                                    detail.collection_position = position;
                                    if (auto title = get(fields, "m_instTitle");
                                        title && title->type == 7) {
                                        auto root = m.u32(title->pointer + 4);
                                        for (auto child : children(m, root))
                                            if (character_name(m, child).ends_with(
                                                    ".mcInst_Title")) {
                                                root = child;
                                                break;
                                            }
                                        for (const auto &s : text(m, root)) {
                                            if (!detail.heading.empty())
                                                detail.heading += " ";
                                            detail.heading += s;
                                        }
                                    }
                                    auto total = get(data, "m_getItemNum");
                                    if (total && total->type == 3)
                                        detail.acquired = std::to_string(int(total->number)) + "/" +
                                                          std::to_string(count);
                                    Item item;
                                    item.enabled = owned->boolean;
                                    item.label = value_text(m, get(selected, "Title"));
                                    item.help = value_text(m, get(selected, "Text"));
                                    if (item.help == "???")
                                        item.help.clear();
                                    if (!item.label.empty() || !item.help.empty()) {
                                        detail.items.push_back(std::move(item));
                                        out.push_back(std::move(detail));
                                    }
                                }
                            }
                        }
                    }
                    auto opened = get(fields, "tl_opend");
                    if (opened && opened->type == 2 && opened->boolean && visible(m, c)) {
                        std::string body;
                        for (auto line : text(m, c)) {
                            if (line.find_first_not_of(". ") == std::string::npos)
                                continue;
                            while (!line.empty() && line.back() == '.')
                                line.pop_back();
                            if (!body.empty())
                                body += ". ";
                            body += line;
                        }
                        if (!body.empty()) {
                            Menu dialog;
                            dialog.movie = movie.p;
                            dialog.selector = c;
                            dialog.cursor = 0;
                            dialog.message = true;
                            dialog.modal = true;
                            auto type = get(fields, "tl_type");
                            dialog.pending =
                                type && (type->type == 3 || type->type == 4) && type->number == 2;
                            dialog.items.push_back({body, "", true});
                            out.push_back(std::move(dialog));
                        }
                    }
                    if (cur && (cur->type == 3 || cur->type == 4) && std::isfinite(cur->number) &&
                        items && items->type == 6 && visible(m, c)) {
                        auto a = items->pointer, ptr = m.u32(a + 0x38), count = m.u32(a + 0x3c),
                             cap = m.u32(a + 0x40);
                        if (count && count <= 256 && count <= cap && cap <= 1024 &&
                            cur->number >= 0 && cur->number < count &&
                            std::floor(cur->number) == cur->number) {
                            if (!selector_input_active(m, c))
                                inactiveSelection = true;
                            else {
                                Menu menu;
                                menu.movie = movie.p;
                                menu.selector = c;
                                menu.cursor = int(cur->number);
                                menu.path = character_name(m, c);
                                menu.heading = heading(class_name(m, movie.p));
                                auto declaredTitle = get(fields, "m_titleText"),
                                     acceptsInput = get(fields, "m_input");
                                menu.choices_only = declaredTitle && declaredTitle->type == 5 &&
                                                    value_text(m, declaredTitle).empty();
                                if (auto missions = get(fields, "m_missions");
                                    missions && missions->type == 6) {
                                    for (auto container : children(m, c))
                                        for (auto child : children(m, container))
                                            if (character_name(m, child).ends_with(
                                                    ".mcInst_Summary"))
                                                for (const auto &line : text(m, child)) {
                                                    if (!menu.summary.empty())
                                                        menu.summary += ' ';
                                                    menu.summary += line;
                                                }
                                    menu.repeat_text = menu.summary;
                                }
                                for (uint32_t i = 0; i < count; i++) {
                                    auto v = m.u32(ptr + i * 4);
                                    if (m.u8(v) != 6) {
                                        menu.items.clear();
                                        break;
                                    }
                                    auto itemFields = members(m, m.u32(v + 4));
                                    auto function = get(itemFields, "FuncName"),
                                         dialogType = get(itemFields, "DialogType"),
                                         dialogText = get(itemFields, "DialogText");
                                    menu.choices_only = menu.choices_only && function &&
                                                        function->type == 5 &&
                                                        !value_text(m, function).empty() &&
                                                        dialogType && dialogType->type == 5 &&
                                                        dialogText && dialogText->type == 5;
                                    Item item;
                                    item.help = value_text(m, get(itemFields, "Help"));
                                    if (item.help.empty())
                                        item.help = value_text(m, get(itemFields, "HelpText"));
                                    if (auto enabled = get(itemFields, "bActive");
                                        enabled && enabled->type == 2)
                                        item.enabled = enabled->boolean;
                                    if (auto inst = get(itemFields, "Inst");
                                        inst && inst->type == 7) {
                                        auto labels = text(m, m.u32(inst->pointer + 4));
                                        for (const auto &label : labels) {
                                            if (!item.label.empty())
                                                item.label += ", ";
                                            item.label += label;
                                        }
                                    }
                                    if (item.label.empty()) {
                                        for (auto key :
                                             {"Text", "Label", "Name", "m_text", "_text"}) {
                                            item.label = value_text(m, get(itemFields, key));
                                            if (!item.label.empty())
                                                break;
                                        }
                                    }
                                    menu.items.push_back(std::move(item));
                                }
                                if (menu.choices_only &&
                                    (!acceptsInput || acceptsInput->type != 2 ||
                                     !acceptsInput->boolean))
                                    menu.pending = true;
                                if (menu.items.size() == count &&
                                    !menu.items[menu.cursor].label.empty() &&
                                    m.u32(a + 0x38) == ptr && m.u32(a + 0x3c) == count)
                                    out.push_back(std::move(menu));
                            }
                        }
                    }
                    if (!result)
                        result = results(m, c, fields);
                    auto kids = children(m, c);
                    todo.insert(todo.end(), kids.rbegin(), kids.rend());
                }
            }
            if (result) {
                if (result->main)
                    for (auto &menu : out)
                        if (menu.movie == movie.p) {
                            menu.summary = result->summary;
                            menu.pending = menu.pending || result->pending;
                        }
                if (result->detail) {
                    Menu detail;
                    detail.movie = movie.p;
                    detail.selector = result->root;
                    detail.cursor = 0;
                    detail.message = detail.readout = true;
                    detail.pending = result->pending;
                    detail.items.push_back({result->breakdown, "", true});
                    out.push_back(std::move(detail));
                }
            }
        }
        std::set<std::pair<uint32_t, uint32_t>> seen;
        std::erase_if(out, [&](const Menu &menu) {
            return !seen.insert({menu.movie, menu.selector}).second;
        });
        if (std::none_of(out.begin(), out.end(), [](const Menu &menu) { return menu.modal; })) {
            for (const auto &menu : out) {
                if (menu.message || menu.settings || menu.collection)
                    continue;
                std::set<uint32_t> ancestors;
                for (auto c = menu.selector;
                     c && ancestors.insert(c).second && ancestors.size() < 64;
                     c = m.u32(c + 0x20)) {
                    auto fields = members(m, m.u32(c + 0x1c8));
                    auto opened = get(fields, "tl_opend");
                    if (opened && opened->type == 2) {
                        Menu pending;
                        pending.movie = menu.movie;
                        pending.selector = c;
                        pending.modal = true;
                        pending.pending = true;
                        return {pending};
                    }
                }
            }
        }
        if (std::any_of(out.begin(), out.end(), [](const Menu &menu) { return menu.modal; })) {
            for (auto &dialog : out)
                if (dialog.modal && dialog.message) {
                    for (const auto &choice : out) {
                        if (choice.modal || choice.movie != dialog.movie || choice.items.empty() ||
                            std::any_of(choice.items.begin(), choice.items.end(),
                                        [](const Item &item) { return item.label.empty(); }))
                            continue;
                        bool descendant = false;
                        std::set<uint32_t> ancestors;
                        for (auto c = choice.selector;
                             c && ancestors.insert(c).second && ancestors.size() < 64;
                             c = m.u32(c + 0x20))
                            if (c == dialog.selector) {
                                descendant = true;
                                break;
                            }
                        if (!descendant)
                            continue;
                        std::vector<std::string> promptLines;
                        std::vector<uint32_t> nodes{dialog.selector};
                        std::set<uint32_t> visited;
                        while (!nodes.empty() && visited.size() < 256) {
                            auto c = nodes.back();
                            nodes.pop_back();
                            if (!visited.insert(c).second || !visible(m, c))
                                continue;
                            if (character_name(m, c).ends_with(".mcInst_MainText")) {
                                auto lines = text(m, c);
                                promptLines.insert(promptLines.end(), lines.begin(), lines.end());
                                continue;
                            }
                            auto kids = children(m, c);
                            nodes.insert(nodes.end(), kids.begin(), kids.end());
                        }
                        if (promptLines.empty())
                            promptLines = text(m, dialog.selector);
                        std::string prompt;
                        for (auto line : promptLines) {
                            if (line.find_first_not_of(". ") == std::string::npos ||
                                std::any_of(choice.items.begin(), choice.items.end(),
                                            [&](const Item &item) { return item.label == line; }))
                                continue;
                            while (!line.empty() && line.back() == '.')
                                line.pop_back();
                            if (!prompt.empty())
                                prompt += ". ";
                            prompt += line;
                        }
                        if (prompt.empty())
                            for (auto line : text(m, dialog.selector)) {
                                if (line.find_first_not_of(". ") == std::string::npos ||
                                    std::any_of(
                                        choice.items.begin(), choice.items.end(),
                                        [&](const Item &item) { return item.label == line; }))
                                    continue;
                                while (!line.empty() && line.back() == '.')
                                    line.pop_back();
                                if (!prompt.empty()) {
                                    if (prompt.back() != '?' && prompt.back() != '!' &&
                                        prompt.back() != '.')
                                        prompt += '.';
                                    prompt += ' ';
                                }
                                prompt += line;
                            }
                        if (prompt.empty()) {
                            // Some scenes intentionally use the common dialog as
                            // a choice menu with no question. Accept that only
                            // when an active owner explicitly declares a blank
                            // title and the same ordered, named action choices.
                            auto input = get(members(m, m.u32(choice.selector + 0x1c8)), "m_input");
                            bool declared =
                                input && input->type == 2 && input->boolean &&
                                std::any_of(out.begin(), out.end(), [&](const Menu &owner) {
                                    return declared_choices_match(owner, choice);
                                });
                            if (declared) {
                                dialog.heading.clear();
                                dialog.items = choice.items;
                                dialog.cursor = choice.cursor;
                                dialog.message = false;
                                dialog.pending = false;
                                break;
                            }
                            dialog.pending = true;
                            continue;
                        }
                        dialog.heading = prompt;
                        dialog.items = choice.items;
                        dialog.cursor = choice.cursor;
                        dialog.message = false;
                        dialog.pending = false;
                        break;
                    }
                }
            std::erase_if(out, [](const Menu &menu) { return !menu.modal; });
            return out;
        }
        // Action declarations feed the common dialog; they are not independently
        // displayed controls and must not revive when that dialog closes.
        std::erase_if(out, [](const Menu &menu) { return menu.choices_only; });
        // The title selector is populated before save loading and the queued DLC
        // check finish. Native bEnableInput owns its readiness; visible labels and
        // delegated ActionScript m_input flags do not. Dialogs above keep priority.
        std::erase_if(out, [&](const Menu &menu) {
            bool blocked = !menu.message && !menu.settings && !menu.collection &&
                           class_name(m, menu.movie) == "BCGFxUI_TitleMenu" &&
                           !(m.u32(menu.movie + 0x208) & 0x40000000);
            inactiveSelection |= blocked;
            return blocked;
        });
        if (std::none_of(out.begin(), out.end(), [](const Menu &menu) { return menu.records; }))
            record_browser_.panel = 0;
        if (std::any_of(out.begin(), out.end(), [](const Menu &menu) { return menu.readout; })) {
            std::erase_if(out, [](const Menu &menu) { return !menu.readout; });
            return out;
        }
        if (std::any_of(out.begin(), out.end(), [](const Menu &menu) { return menu.collection; })) {
            std::erase_if(out, [](const Menu &menu) { return !menu.collection; });
            return out;
        }
        if (std::any_of(out.begin(), out.end(), [](const Menu &menu) { return menu.settings; })) {
            for (auto &panel : out)
                if (panel.settings)
                    for (const auto &parent : out)
                        if (parent.movie == panel.movie && !parent.settings && !parent.collection &&
                            !parent.message && parent.cursor >= 0 &&
                            size_t(parent.cursor) < parent.items.size()) {
                            panel.heading = parent.items[parent.cursor].label;
                            break;
                        }
            std::erase_if(out, [](const Menu &menu) { return !menu.settings; });
            return out;
        }
        if (out.empty() && !inactiveSelection) {
            for (auto movie : movies_) {
                if (m.u32(movie.p + 0x20) != movie.index || !menu_movie_active(m, movie.p))
                    continue;
                if (class_name(m, movie.p) != "BCGFxUI_TitleMenu")
                    continue;
                auto gfx = m.u32(movie.p + 0x180);
                if (m.u32(gfx + 0x40) != 0x48)
                    continue;
                auto root = m.u32(m.u32(gfx + 0x44) + 4);
                auto lines = text(m, root);
                std::string message;
                for (const auto &line : lines) {
                    if (line.find("CAPCOM") != std::string::npos ||
                        line.find("RIGHTS RESERVED") != std::string::npos)
                        continue;
                    if (!message.empty())
                        message += ". ";
                    message += line;
                }
                if (class_name(m, movie.p) == "BCGFxUI_TitleMenu" &&
                    m.u8(movie.p + layout_.title_process_offset) == 4)
                    message = "Checking downloadable content.";
                if (!message.empty()) {
                    Menu status;
                    status.movie = movie.p;
                    status.selector = root;
                    status.cursor = 0;
                    status.message = true;
                    status.items.push_back({message, "", true});
                    out.push_back(std::move(status));
                }
            }
        }
        return out;
    }
    void diagnostic(const Memory &m, std::ostream &out) {
        out << "Unreal ready=" << ready(m) << " movies=" << movies_.size() << "\n";
        for (auto movie : movies_) {
            out << "MOVIE " << std::hex << movie.p << " " << class_name(m, movie.p)
                << " flags=" << m.u32(movie.p + 0xc8) << std::dec << "\n";
            if (!(m.u32(movie.p + 0xc8) & 0x80000000))
                continue;
            std::set<uint32_t> seen;
            std::vector<std::pair<uint32_t, int>> todo;
            for (auto f : {0x17cu, 0x180u}) {
                auto gfx = m.u32(movie.p + f);
                out << " gfx=" << std::hex << gfx << " type=" << m.u32(gfx + 0x40) << std::dec
                    << "\n";
                if (gfx)
                    todo.push_back({m.u32(m.u32(gfx + 0x44) + 4), 0});
            }
            while (!todo.empty() && seen.size() < 3000) {
                auto [c, depth] = todo.back();
                todo.pop_back();
                if (!c || !seen.insert(c).second || m.u32(c) != layout_.sprite_vtable)
                    continue;
                auto object = m.u32(c + 0x1c8);
                out << "CHAR " << std::hex << c << " AS " << object << std::dec << " "
                    << character_name(m, c) << "\n";
                dump_members(m, object, out, 0);
                for (auto child : children(m, c))
                    todo.push_back({child, depth + 1});
            }
        }
    }
    void dump_members(const Memory &m, uint32_t object, std::ostream &out, int depth) const {
        if (depth > 2)
            return;
        for (const auto &[key, v] : members(m, object)) {
            if (key == "__proto__" || key == "constructor" || key == "__constructor__" ||
                v.type == 8 || v.type == 10)
                continue;
            out << std::string(depth * 2, ' ') << key << " type=" << int(v.type)
                << " ptr=" << std::hex << v.pointer << std::dec;
            if (v.type == 3)
                out << " number=" << v.number;
            if (v.type == 2)
                out << " bool=" << v.boolean;
            if (v.type == 5)
                out << " text=" << value_text(m, v);
            out << "\n";
            if (v.type == 6 && v.pointer && v.pointer != object) {
                dump_members(m, v.pointer, out, depth + 1);
                if (key == "m_items" || key == "m_details") {
                    auto p = m.u32(v.pointer + 0x38), n = m.u32(v.pointer + 0x3c);
                    out << " ARRAY count=" << n << "\n";
                    if (n < 40)
                        for (uint32_t i = 0; i < n; i++) {
                            auto val = m.u32(p + i * 4);
                            out << " ITEM " << i << " type=" << int(m.u8(val)) << "\n";
                            dump_members(m, m.u32(val + 4), out, depth + 1);
                        }
                }
            }
        }
    }
    static std::string heading(std::string cls) {
        const std::string prefix = "BCGFxUI_";
        if (cls.starts_with(prefix))
            cls.erase(0, prefix.size());
        if (cls == "ChapterMenu")
            return "Episode Menu";
        std::string out;
        for (size_t i = 0; i < cls.size(); i++) {
            if (cls[i] == '_') {
                out += ' ';
                continue;
            }
            if (i && cls[i] >= 'A' && cls[i] <= 'Z' && cls[i - 1] >= 'a' && cls[i - 1] <= 'z')
                out += ' ';
            out += cls[i];
        }
        return out;
    }

  private:
    ReaderLayout layout_;
    RecordBrowse record_browser_;
    struct ScriptScene {
        uint32_t index{}, cls{};
        bool active{};
    };
    std::map<uint32_t, ScriptScene> script_scenes_;
    std::array<Menu, 3> record_panels_;
    std::function<std::string(std::string_view)> button_labels_;
    struct Movie {
        uint32_t p, index, cls;
    };
    std::vector<Movie> movies_;
    std::unordered_map<uint32_t, std::string> names_;
    std::unordered_map<uint32_t, bool> menu_classes_;
    uint32_t discovered_count_{}, discovered_table_{};
};
inline void append_phrase(std::string &text, const std::string &phrase) {
    if (phrase.empty())
        return;
    if (!text.empty()) {
        if (text.back() != '.' && text.back() != '?' && text.back() != '!')
            text += '.';
        text += ' ';
    }
    text += phrase;
}
inline std::string menu_context(const Menu &menu) {
    auto key = std::to_string(menu.movie) + ":" + std::to_string(menu.selector);
    if (!menu.summary.empty())
        key += "|summary:" + menu.summary;
    // A single dialog object is reused for different questions. Selection changes
    // share context; a new question must announce its prompt again.
    if (menu.modal && !menu.message)
        key += "|dialog:" + menu.heading;
    return key;
}
inline std::string record_navigation_text(const Menu &menu, bool difficulty_changed) {
    if (!menu.records || menu.pending || menu.cursor < 0 ||
        size_t(menu.cursor) >= menu.items.size())
        return {};
    std::string line;
    if (difficulty_changed)
        append_phrase(line, menu.record_difficulty);
    append_phrase(line, menu.items[menu.cursor].label);
    return line;
}
inline std::string describe(const Menu &menu, bool heading, bool help) {
    if (menu.pending)
        return {};
    if (menu.cursor < 0 || size_t(menu.cursor) >= menu.items.size())
        return {};
    const auto &item = menu.items[menu.cursor];
    if (menu.records) {
        std::string line;
        if (heading) {
            append_phrase(line, menu.heading);
            append_phrase(line, menu.summary);
            append_phrase(line, menu.record_difficulty);
        }
        append_phrase(line, item.label);
        return line;
    }
    std::string s = heading ? menu.heading + ". " : "";
    s += item.label;
    if (heading && !menu.summary.empty()) {
        s = menu.heading;
        append_phrase(s, menu.summary);
        append_phrase(s, item.label);
    }
    if (menu.message)
        return item.label;
    if (menu.modal) {
        std::string prompt = heading ? menu.heading : "";
        if (!prompt.empty()) {
            if (prompt.back() != '.' && prompt.back() != '?' && prompt.back() != '!')
                prompt += '.';
            prompt += ' ';
        }
        return prompt + item.label;
    }
    if (menu.collection) {
        s.clear();
        if (heading) {
            append_phrase(s, menu.heading);
            if (!menu.acquired.empty())
                append_phrase(s, "Acquired: " + menu.acquired);
        }
        std::string selected;
        if (item.enabled) {
            selected = item.label;
            if (help && !item.help.empty()) {
                if (!selected.empty())
                    selected += ": ";
                selected += item.help;
            }
        } else {
            if (item.label != "???")
                append_phrase(selected, item.label);
            append_phrase(selected, "Locked");
            if (!item.help.empty()) {
                auto requirement = item.help;
                while (!requirement.empty() && requirement.back() == '.')
                    requirement.pop_back();
                selected += ": " + requirement;
            }
        }
        if (menu.collection_position >= 0 && menu.collection_count)
            append_phrase(selected, std::to_string(menu.collection_position + 1) + " of " +
                                        std::to_string(menu.collection_count));
        append_phrase(s, selected);
        return s;
    }
    if (!item.enabled)
        s += ". Unavailable";
    s += ". " + std::to_string(menu.cursor + 1) + " of " + std::to_string(menu.items.size());
    if (help && !item.help.empty())
        s += ". " + item.help;
    return s;
}
inline std::string menu_repeat_text(const std::vector<Menu> &menus, uint32_t movie) {
    for (auto it = menus.rbegin(); it != menus.rend(); ++it)
        if (it->movie == movie && (it->readout || !it->summary.empty())) {
            if (it->pending)
                return {};
            return it->repeat_text.empty() ? describe(*it, true, true) : it->repeat_text;
        }
    return {};
}
inline std::string menu_selection_state(const Menu &menu) {
    return menu_context(menu) + ":" + std::to_string(menu.selection_key) + "|" +
           describe(menu, false, true);
}
}
