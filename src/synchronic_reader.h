#pragma once
#include "gameplay_events.h"
#include "qte_reader.h"

namespace a11y {
class SynchronicReader {
  public:
    using Movie = QteReader::Movie;
    explicit SynchronicReader(ButtonLabel buttons) : buttons_(std::move(buttons)) {}
    void reset() {
        views_.clear();
        tracks_.clear();
        visibility_.clear();
        grades_.clear();
        count_in_ = {};
        count_in_changed_ = false;
    }
    // The grade text the HUD shows when a key is judged.
    void grade(int result) {
        static constexpr std::array<const char *, 4> labels{"", "Good.", "Great.", "Excellent."};
        if (result >= 1 && result <= 3 && grades_.size() < 8)
            grades_.push_back(labels[size_t(result)]);
    }
    void apply(const PresentationEvent &event) {
        Movie movie{event.movie, event.index};
        auto tag = event.tag.utf8();
        std::erase_if(views_, [&](const View &view) {
            return view.movie.pointer == movie.pointer && view.movie.index != movie.index;
        });
        if (event.kind == PresentationKind::sync_clear) {
            closed(movie);
            return;
        }
        if (event.kind == PresentationKind::sync_start) {
            std::erase_if(views_,
                          [&](const View &view) { return view.movie == movie && view.tag == tag; });
            if (views_.size() == 64)
                views_.erase(views_.begin());
            views_.push_back({movie, tag, true, event.order, ++serial_});
            return;
        }
        if (event.kind == PresentationKind::sync_end) {
            std::erase_if(views_,
                          [&](const View &view) { return view.movie == movie && view.tag == tag; });
            return;
        }
        if (event.kind == PresentationKind::sync_visible)
            for (auto &view : views_)
                if (view.movie == movie && view.tag == tag)
                    view.visible = event.flag;
    }
    void apply(const TimingState &state, uint64_t order, const QteReader::NameLookup &name) {
        std::erase_if(tracks_, [&](const Track &track) {
            return track.data.source == state.source && track.data.index != state.index;
        });
        auto it = std::find_if(tracks_.begin(), tracks_.end(), [&](const Track &track) {
            return track.data.source == state.source && track.data.index == state.index &&
                   track.data.track == state.track;
        });
        if (it == tracks_.end()) {
            if (tracks_.size() == 32)
                tracks_.erase(tracks_.begin());
            tracks_.push_back({});
            it = std::prev(tracks_.end());
        }
        it->data = state;
        it->order = order;
        it->tag = name(state.name) + std::to_string(state.track);
        // Each reading of the track clock reschedules the count-in, so it follows
        // the game's timing; it starts once the key is the active one, which for a
        // chained key is after the previous key has been judged.
        if (!state.active || state.mash || !std::isfinite(state.until) || !state.stamp)
            return;
        auto perfect = state.stamp + int64_t(double(state.until) * 1e9);
        if (count_in_.generation != state.generation || !count_in_.active)
            count_in_ = {perfect, state.stamp, state.generation, true};
        else
            count_in_.perfect = perfect;
        count_in_changed_ = true;
    }
    // The newest count-in schedule, once per change; a later key replaces it.
    std::optional<CountInCue> take_count_in() {
        if (!count_in_changed_)
            return {};
        count_in_changed_ = false;
        return count_in_;
    }
    std::vector<std::string> take() {
        std::vector<std::string> out = std::move(grades_);
        grades_.clear();
        for (auto &track : tracks_) {
            auto cue = current(track);
            if (cue.first.empty()) {
                track.last.clear();
                continue;
            }
            if (cue.first == track.last)
                continue;
            track.last = cue.first;
            out.push_back(std::move(cue.second));
        }
        return out;
    }
    std::string repeat() const {
        uint64_t order = 0;
        std::string text;
        for (const auto &track : tracks_) {
            auto cue = current(track);
            if (track.order >= order && !cue.first.empty()) {
                order = track.order;
                text = std::move(cue.second);
            }
        }
        return text;
    }
    std::vector<Movie> movies() const {
        std::vector<Movie> out;
        for (const auto &view : views_)
            if (std::find(out.begin(), out.end(), view.movie) == out.end())
                out.push_back(view.movie);
        return out;
    }
    void closed(Movie movie) {
        std::erase_if(views_, [&](const View &view) { return view.movie == movie; });
        visibility_.erase({movie.pointer, movie.index});
    }
    void visibility(Movie movie, bool visible) {
        visibility_[{movie.pointer, movie.index}] = visible;
    }

  private:
    struct View {
        Movie movie;
        std::string tag;
        bool visible{};
        uint64_t order{}, serial{};
    };
    struct Track {
        TimingState data;
        std::string tag, last;
        uint64_t order{};
    };
    std::pair<std::string, std::string> current(const Track &track) const {
        if (!track.data.active)
            return {};
        const View *selected = nullptr;
        for (const auto &view : views_) {
            auto visible = visibility_.find({view.movie.pointer, view.movie.index});
            if (visible != visibility_.end() && !visible->second)
                continue;
            if (view.tag == track.tag && view.visible && view.order <= track.order &&
                (!selected || view.serial > selected->serial))
                selected = &view;
        }
        auto label = buttons_ ? buttons_("Y") : "Y";
        if (!selected || label.empty())
            return {};
        // The prompt appears with the HUD element. A tap's press moment is the
        // count-in's high beat, not speech; a mash key says when to start.
        bool cue = track.data.mash && track.data.cue;
        auto key = std::to_string(track.data.generation) + ":" + std::to_string(selected->serial) +
                   ":" + std::to_string(cue);
        std::string text =
            cue ? "Mash " + label + " now."
                : (track.data.mash ? "Prepare to mash " + label + "." : "Timing: " + label + ".");
        if (!text.empty())
            text[0] = char(std::toupper(static_cast<unsigned char>(text[0])));
        return {key, std::move(text)};
    }
    ButtonLabel buttons_;
    uint64_t serial_{};
    std::vector<View> views_;
    std::vector<Track> tracks_;
    std::vector<std::string> grades_;
    CountInCue count_in_;
    bool count_in_changed_{};
    std::map<std::pair<uint32_t, uint32_t>, bool> visibility_;
};
}
