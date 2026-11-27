#pragma once
#include "gameplay_text.h"
#include <optional>

namespace a11y {
class GameplayStatus {
  public:
    using Movie = GameplayText::Movie;
    explicit GameplayStatus(ButtonLabel label = {}, int lowHealth = 25)
        : label_(std::move(label)), low_(std::clamp(lowHealth, 1, 90)) {}
    void low_health(int percent) {
        low_ = std::clamp(percent, 1, 90);
    }
    void reset() {
        gauge_ = {};
        health_.reset();
        burst_.reset();
        unlimited_ = {};
        visible_ = false;
        lowAnnounced_ = false;
        ready_ = {};
        pending_.clear();
    }
    void apply(const PresentationEvent &event) {
        const Movie movie{event.movie, event.index};
        switch (event.kind) {
            case PresentationKind::health:
            case PresentationKind::burst:
            case PresentationKind::gauge_visible:
                if (gauge_ != movie) {
                    gauge_ = movie;
                    health_.reset();
                    burst_.reset();
                    visible_ = false;
                    lowAnnounced_ = false;
                }
                if (event.kind == PresentationKind::gauge_visible) {
                    visible_ = event.flag;
                    if (!visible_)
                        lowAnnounced_ = false;
                } else {
                    visible_ = event.flag;
                    if (event.kind == PresentationKind::health) {
                        health_ = percent(event.value);
                        if (*health_ > std::min(low_ + 10, 99))
                            lowAnnounced_ = false;
                    } else
                        burst_ = percent(event.value);
                }
                if (visible_ && health_ && *health_ <= low_ && !lowAnnounced_) {
                    pending_.push_back({Notice::low_health, movie, {}});
                    lowAnnounced_ = true;
                }
                break;
            case PresentationKind::unlimited:
            case PresentationKind::unlimited_mode:
            case PresentationKind::unlimited_enabled: {
                if (gauge_ != movie) {
                    gauge_ = movie;
                    health_.reset();
                    burst_.reset();
                    unlimited_ = {};
                    visible_ = false;
                    lowAnnounced_ = false;
                }
                auto &u = unlimited_;
                if (event.kind == PresentationKind::unlimited_enabled)
                    u.enabled = event.flag;
                else if (event.kind == PresentationKind::unlimited_mode) {
                    if (event.flag != u.mode)
                        pending_.push_back(
                            {event.flag ? Notice::mode_start : Notice::mode_end, movie, {}});
                    u.mode = event.flag;
                    u.full = false;
                } else {
                    u.value = percent(event.value);
                    // Ready once the gauge fills, outside the mode (whose value is
                    // the time left) and only while Unlimited is available.
                    bool full = !u.mode && *u.value >= 100;
                    if (full && !u.full && u.enabled)
                        pending_.push_back({Notice::unlimited_ready, movie, {}});
                    u.full = full;
                }
                break;
            }
            case PresentationKind::gauge_end:
                closed(movie);
                break;
            case PresentationKind::button_create:
                // This named player-controller button is created only after
                // CheckVisibleBurstButton/IsCanBurst succeed. A scripted Burst QTE uses
                // its own InputDivergence tag and is not a readiness notification.
                if (event.type.utf8() == "BURST" && event.tag.utf8().starts_with("BurstButton")) {
                    if (ready_.movie != movie || ready_.tag != event.tag.utf8()) {
                        ready_ = {movie, event.tag.utf8(), true, true};
                        pending_.push_back({Notice::burst_ready, movie, ready_.tag});
                    }
                }
                break;
            case PresentationKind::button_visible:
            case PresentationKind::button_active:
                if (ready_.movie == movie && ready_.tag == event.tag.utf8()) {
                    bool was = ready_.available();
                    if (event.kind == PresentationKind::button_visible)
                        ready_.visible = event.flag;
                    else
                        ready_.enabled = event.flag;
                    if (!was && ready_.available())
                        pending_.push_back({Notice::burst_ready, movie, ready_.tag});
                }
                break;
            case PresentationKind::button_success:
            case PresentationKind::button_failure:
            case PresentationKind::button_finish:
                if (ready_.movie == movie && ready_.tag == event.tag.utf8())
                    ready_ = {};
                break;
            case PresentationKind::button_end:
                closed(movie);
                break;
            default:
                break;
        }
    }
    std::string status() const {
        // Visibility gates automatic warnings, not an explicit status request. The
        // native gauge's TickGauge continues supplying current hidden values.
        if (!health_ || !burst_)
            return std::string("Health and Burst status is not available.") +
                   (ready_.available() ? " Burst ready." : "");
        std::string unlimited;
        if (unlimited_.enabled && unlimited_.value)
            unlimited =
                unlimited_.mode
                    ? " Unlimited Mode, " + std::to_string(*unlimited_.value) + " percent left."
                    : " Unlimited " + std::to_string(*unlimited_.value) + " percent.";
        return "Health " + std::to_string(*health_) + " percent. Burst " + std::to_string(*burst_) +
               " percent." + unlimited + (ready_.available() ? " Burst ready." : "");
    }
    std::string status(Movie movie) const {
        return movie == gauge_ ? status() : std::string{};
    }
    std::vector<Movie> movies() const {
        std::vector<Movie> out;
        if (gauge_.pointer)
            out.push_back(gauge_);
        if (ready_.movie.pointer && ready_.movie != gauge_)
            out.push_back(ready_.movie);
        return out;
    }
    void closed(Movie movie) {
        if (gauge_ == movie) {
            gauge_ = {};
            health_.reset();
            burst_.reset();
            unlimited_ = {};
            visible_ = false;
            lowAnnounced_ = false;
        }
        if (ready_.movie == movie)
            ready_ = {};
    }
    std::vector<std::string> take() {
        std::vector<std::string> out;
        for (const auto &pending : pending_) {
            std::string text;
            if (pending.kind == Notice::burst_ready) {
                if (ready_.movie != pending.movie || ready_.tag != pending.tag ||
                    !ready_.available())
                    continue;
                auto label = label_ ? label_("RT") : "RT";
                text = "Burst ready" + (label.empty() ? std::string(".") : ". " + label + ".");
            } else if (pending.kind == Notice::unlimited_ready) {
                if (gauge_ != pending.movie || !unlimited_.full || !unlimited_.enabled)
                    continue;
                // The tutorial: "Unlimited Mode can be activated by pressing <LT>".
                auto label = label_ ? label_("LT") : "LT";
                text = "Unlimited ready" + (label.empty() ? std::string(".") : ". " + label + ".");
            } else if (pending.kind == Notice::mode_start || pending.kind == Notice::mode_end) {
                if (gauge_ != pending.movie)
                    continue;
                text = pending.kind == Notice::mode_start ? "Unlimited Mode."
                                                          : "Unlimited Mode ended.";
            } else {
                if (gauge_ != pending.movie || !visible_ || !health_ || *health_ > low_)
                    continue;
                text = "Low health. " + std::to_string(*health_) + " percent.";
            }
            if (std::find(out.begin(), out.end(), text) == out.end())
                out.push_back(std::move(text));
        }
        pending_.clear();
        return out;
    }

  private:
    static int percent(float value) {
        return int(std::lround(std::clamp(value, 0.f, 1.f) * 100));
    }
    struct Ready {
        Movie movie{};
        std::string tag;
        bool visible{}, enabled{};
        bool available() const {
            return movie.pointer && visible && enabled;
        }
    };
    struct Notice {
        enum Kind { low_health, burst_ready, unlimited_ready, mode_start, mode_end } kind{};
        Movie movie{};
        std::string tag;
    };
    struct Unlimited {
        std::optional<int> value;
        bool enabled{}, mode{}, full{};
    };
    Unlimited unlimited_;
    ButtonLabel label_;
    int low_;
    Movie gauge_{};
    std::optional<int> health_, burst_;
    bool visible_{}, lowAnnounced_{};
    Ready ready_;
    std::vector<Notice> pending_;
};
}
