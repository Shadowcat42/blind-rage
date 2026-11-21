#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace a11y {
struct RecordStat {
    std::string label, value, rank;
};
inline std::optional<std::array<RecordStat, 3>> parse_episode_record(std::string_view text) {
    std::array<RecordStat, 3> stats;
    for (unsigned i = 0; i < stats.size(); ++i) {
        auto end = text.find('/');
        if ((i < 2) != (end != std::string_view::npos))
            return {};
        auto row = text.substr(0, end);
        auto first = row.find('|'), last = row.rfind('|');
        if (first == std::string_view::npos || first == last || first == 0 || last == first + 1 ||
            last + 2 != row.size())
            return {};
        auto rank = row.substr(last + 1);
        if (rank.find_first_not_of("SABCD-") != std::string_view::npos)
            return {};
        stats[i] = {std::string(row.substr(0, first)),
                    std::string(row.substr(first + 1, last - first - 1)),
                    rank == "-" ? "Unranked" : std::string(rank)};
        if (end != std::string_view::npos)
            text.remove_prefix(end + 1);
    }
    return stats;
}
struct RecordBrowse {
    uint32_t movie{}, index{}, panel{}, root{};
    std::string story;
    int difficulty = 1, stat = 0;
    std::array<std::optional<std::array<RecordStat, 3>>, 3> scores;
    void capture(uint32_t m, uint32_t idx, std::string tag, int diff, std::string_view text) {
        if (diff < 0 || diff >= 3)
            return;
        auto parsed = parse_episode_record(text);
        if (!parsed || tag.empty())
            return;
        if (m != movie || idx != index || tag != story) {
            *this = {};
            movie = m;
            index = idx;
            story = std::move(tag);
        }
        scores[diff] = std::move(parsed);
    }
    void move(int command) {
        if (command == 1 || command == 2)
            difficulty = (difficulty + (command == 1 ? 2 : 1)) % 3;
        else if (command == 3 || command == 4)
            stat = (stat + (command == 3 ? 3 : 1)) % 4;
    }
};
}
