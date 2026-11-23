#pragma once
#include "menu_reader.h"
#include "presentation_events.h"
#include "input_labels.h"

namespace a11y {
inline std::string gameplay_button_token(std::string_view name) {
    return "\xee\x80\x80" + std::string(name) + "\xee\x80\x81";
}
// Button tags in the game's localized tutorial text (Coalesced_INT.bin), with the
// label name each resolves to. "<Back>" is written in mixed case ("<Back> Skip
// tutorial"); the sticks appear as <LS> and <RS> (aiming, switching targets).
inline constexpr std::array<std::pair<const char *, const char *>, 13> kGameplayButtonTags{{
    {"A", "A"},
    {"X", "X"},
    {"Y", "Y"},
    {"B", "B"},
    {"LT", "LT"},
    {"RT", "RT"},
    {"LB", "LB"},
    {"RB", "RB"},
    {"LS", "LS"},
    {"RS", "RS"},
    {"START", "START"},
    {"BACK", "BACK"},
    {"Back", "BACK"},
}};
inline std::string gameplay_template(std::string text) {
    // Advertise resolves remapped action tags before CallCreate. Preserve its
    // physical button glyphs before ordinary HTML cleanup removes angle tags.
    auto buttons = [&] {
        for (auto [glyph, name] : kGameplayButtonTags) {
            std::string tag = "<" + std::string(glyph) + ">",
                        replacement = " " + gameplay_button_token(name) + " ";
            size_t pos = 0;
            while ((pos = text.find(tag, pos)) != std::string::npos) {
                text.replace(pos, tag.size(), replacement);
                pos += replacement.size();
            }
        }
    };
    buttons();
    text = clean_text(text);
    buttons();
    text = clean_text(text);
    std::string out;
    for (char c : text) {
        if ((c == '.' || c == ',' || c == '?' || c == '!' || c == ':' || c == ';') &&
            !out.empty() && out.back() == ' ')
            out.pop_back();
        out += c;
    }
    return out;
}
inline std::string gameplay_text(std::string text, const ButtonLabel &label = {}) {
    text = gameplay_template(std::move(text));
    for (auto [glyph, name] : kGameplayButtonTags) {
        if (std::string_view(glyph) != name)
            continue;
        auto token = gameplay_button_token(name);
        auto pos = text.find(token);
        if (pos == std::string::npos)
            continue;
        auto button = label ? label(name) : std::string(name) + " button";
        if (button.empty())
            button = "unavailable button label";
        do {
            text.replace(pos, token.size(), button);
            pos = text.find(token, pos + button.size());
        } while (pos != std::string::npos);
    }
    return text;
}
struct TextAnnouncement {
    std::string channel, text;
    uint64_t serial{};
};
class GameplayText {
  public:
    struct Options {
        bool subtitles = true, tutorials = true;
    };
    struct Movie {
        uint32_t pointer{}, index{};
        bool operator==(const Movie &) const = default;
    };
    explicit GameplayText(Options options, ButtonLabel label = {})
        : options_(options), label_(std::move(label)) {}
    void reset() {
        active_.clear();
        tutorials_.clear();
        pending_.clear();
    }
    void apply(const PresentationEvent &event) {
        const Movie movie{event.movie, event.index};
        auto tag = event.tag.utf8(), type = event.type.utf8();
        auto text = gameplay_template(event.text.utf8());
        // An instance's index changes on slot reuse. Old content must not survive.
        std::erase_if(active_, [&](const Active &a) {
            return a.movie.pointer == movie.pointer && a.movie.index != movie.index;
        });
        std::erase_if(tutorials_, [&](const Tutorial &a) {
            return a.movie.pointer == movie.pointer && a.movie.index != movie.index;
        });
        switch (event.kind) {
            case PresentationKind::text_start:
                if (text.empty())
                    break;
                if (auto old = std::find_if(active_.begin(), active_.end(),
                                            [&](const Active &a) {
                                                return a.movie == movie && a.tag == tag &&
                                                       a.type == type;
                                            });
                    old != active_.end() && old->text == text)
                    break;
                erase(movie, [&](const Active &a) { return a.tag == tag; });
                if (active_.size() == 64)
                    active_.erase(active_.begin());
                active_.push_back({movie, tag, type, text, ++serial_});
                if (options_.subtitles)
                    pending_.push_back(
                        {type == "subtitle" ? "subtitle" : "screen text", text, serial_});
                break;
            case PresentationKind::text_end:
                erase(movie, [&](const Active &a) { return a.tag == tag; });
                break;
            case PresentationKind::text_end_type:
                erase(movie, [&](const Active &a) { return a.type == type; });
                break;
            case PresentationKind::text_clear:
                erase(movie, [](const Active &) { return true; });
                break;
            // The movie's Create only queues a line (tl_text); Start displays every
            // queued line and then empties the queue (SetupClear at the end of
            // Start). Each Start therefore shows only the lines added since the
            // previous one, and the reader announces exactly that step.
            case PresentationKind::tutorial_create: {
                auto &panel = tutorial(movie);
                panel.box = false;
                if (text.empty() || std::find(panel.prepared.begin(), panel.prepared.end(), text) !=
                                        panel.prepared.end())
                    break;
                if (panel.prepared.size() == 16)
                    panel.prepared.erase(panel.prepared.begin());
                panel.prepared.push_back(text);
                break;
            }
            // SetBox only selects a gauge picture (gauge, health, burst or
            // unlimited) shown beside the step; its text still arrives through
            // Create, often before SetBox (Episode 3's Burst and Unlimited steps).
            // Keep those lines; the drawn text is a fallback for a box without any.
            case PresentationKind::tutorial_box: {
                auto &panel = tutorial(movie);
                panel.box = true;
                panel.lines.clear();
                panel.box_text.clear();
                break;
            }
            case PresentationKind::tutorial_start: {
                auto &panel = tutorial(movie);
                panel.visible = true;
                if (!panel.prepared.empty()) {
                    panel.lines = std::move(panel.prepared);
                    announce(panel);
                }
                panel.prepared.clear();
                break;
            }
            case PresentationKind::tutorial_end: {
                auto &panel = tutorial(movie);
                panel.visible = false;
                panel.serial = 0;
                break;
            }
            case PresentationKind::tutorial_clear: {
                auto &panel = tutorial(movie);
                panel.lines.clear();
                panel.prepared.clear();
                panel.visible = false;
                panel.box = false;
                panel.box_text.clear();
                panel.serial = 0;
                break;
            }
            default:
                break;
        }
    }
    std::vector<TextAnnouncement> take() {
        std::vector<TextAnnouncement> out;
        for (const auto &candidate : pending_) {
            auto text = candidate;
            if (auto active =
                    std::find_if(active_.begin(), active_.end(),
                                 [&](const Active &a) { return a.serial == candidate.serial; });
                active != active_.end())
                text.text = gameplay_text(active->text, label_);
            else if (auto panel = std::find_if(tutorials_.begin(), tutorials_.end(),
                                               [&](const Tutorial &t) {
                                                   return t.visible && t.serial == candidate.serial;
                                               });
                     panel != tutorials_.end())
                text.text = render(*panel);
            else
                continue;
            if (!text.text.empty())
                out.push_back(std::move(text));
        }
        pending_.clear();
        return out;
    }
    std::vector<Movie> boxes() const {
        std::vector<Movie> out;
        for (const auto &panel : tutorials_)
            if (panel.visible && panel.box)
                out.push_back(panel.movie);
        return out;
    }
    std::vector<Movie> movies() const {
        std::vector<Movie> out;
        auto add = [&](Movie movie) {
            if (std::find(out.begin(), out.end(), movie) == out.end())
                out.push_back(movie);
        };
        for (const auto &text : active_)
            add(text.movie);
        for (const auto &panel : tutorials_)
            add(panel.movie);
        return out;
    }
    void box_text(Movie movie, std::string text) {
        auto &panel = tutorial(movie);
        text = gameplay_template(text);
        if (panel.visible && panel.box && panel.lines.empty() && !text.empty() &&
            panel.box_text != text) {
            panel.box_text = text;
            announce(panel);
        }
    }
    void closed(Movie movie) {
        erase(movie, [](const Active &) { return true; });
        std::erase_if(tutorials_, [&](const Tutorial &t) { return t.movie == movie; });
    }
    std::string repeat() const {
        uint64_t latest = 0;
        std::string text;
        if (options_.subtitles)
            for (const auto &active : active_)
                if (active.serial > latest) {
                    latest = active.serial;
                    text = gameplay_text(active.text, label_);
                }
        if (options_.tutorials)
            for (const auto &panel : tutorials_)
                if (panel.visible && panel.serial > latest) {
                    latest = panel.serial;
                    text = render(panel);
                }
        return text;
    }

  private:
    struct Active {
        Movie movie;
        std::string tag, type, text;
        uint64_t serial{};
    };
    struct Tutorial {
        Movie movie;
        bool visible = false, box = false;
        // lines: the step currently shown; prepared: lines created for the next Start.
        std::vector<std::string> lines, prepared;
        std::string box_text;
        uint64_t serial{};
    };
    template <class Predicate> void erase(Movie movie, Predicate predicate) {
        std::erase_if(active_, [&](const Active &a) { return a.movie == movie && predicate(a); });
    }
    Tutorial &tutorial(Movie movie) {
        auto i = std::find_if(tutorials_.begin(), tutorials_.end(),
                              [&](const Tutorial &t) { return t.movie == movie; });
        if (i != tutorials_.end())
            return *i;
        if (tutorials_.size() == 16)
            tutorials_.erase(tutorials_.begin());
        Tutorial panel;
        panel.movie = movie;
        tutorials_.push_back(std::move(panel));
        return tutorials_.back();
    }
    std::string render(const Tutorial &panel) const {
        if (panel.box && panel.lines.empty())
            return gameplay_text(panel.box_text, label_);
        std::string combined;
        for (const auto &line : panel.lines)
            append_phrase(combined, gameplay_text(line, label_));
        return combined;
    }
    void announce(Tutorial &panel) {
        if (panel.lines.empty() && (!panel.box || panel.box_text.empty()))
            return;
        panel.serial = ++serial_;
        if (options_.tutorials)
            pending_.push_back({"tutorial", {}, serial_});
    }
    Options options_;
    ButtonLabel label_;
    uint64_t serial_{};
    std::vector<Active> active_;
    std::vector<Tutorial> tutorials_;
    std::vector<TextAnnouncement> pending_;
};
}
