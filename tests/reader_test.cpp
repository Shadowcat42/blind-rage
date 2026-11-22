#include "../src/menu_reader.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <limits>
using namespace a11y;
static void check(bool v, const char *message) {
    if (!v)
        throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    try {
        {
            Menu question;
            question.modal = true;
            question.items = {{"Yes", "", true}, {"No", "", true}};
            auto dialog = [&](DialogReturn &returns, int cursor) {
                question.cursor = cursor;
                returns.observe("dialog:" + std::to_string(cursor), &question);
            };
            DialogReturn cancelled;
            cancelled.observe("title:quit", nullptr);
            check(!cancelled.quiet("title:quit"), "initial focus is announced");
            dialog(cancelled, 1);
            dialog(cancelled, 0);
            dialog(cancelled, 1);
            cancelled.observe("", nullptr);
            cancelled.observe("title:quit", nullptr);
            check(!cancelled.quiet("title:quit"),
                  "cancelling a question re-announces the unchanged item");
            DialogReturn confirmed;
            confirmed.observe("title:quit", nullptr);
            dialog(confirmed, 1);
            dialog(confirmed, 0);
            confirmed.observe("title:quit", nullptr);
            check(confirmed.quiet("title:quit"),
                  "confirming with choice 0 keeps the briefly revealed item quiet");
            confirmed.observe("title:options", nullptr);
            check(!confirmed.quiet("title:options") && !confirmed.quiet("title:quit"),
                  "moving after a confirmation announces normally again");
            DialogReturn changed;
            changed.observe("options:volume:100", nullptr);
            dialog(changed, 0);
            changed.observe("options:volume:90", nullptr);
            check(!changed.quiet("options:volume:90"), "a changed value is always announced");
            Menu message;
            message.modal = true;
            message.cursor = 0;
            message.items = {{"Save successful", "", true}};
            DialogReturn acknowledged;
            acknowledged.observe("options:difficulty", nullptr);
            acknowledged.observe("dialog:saved", &message);
            acknowledged.observe("options:difficulty", nullptr);
            check(acknowledged.quiet("options:difficulty"),
                  "a one-choice acknowledgement keeps the revealed item quiet");
        }
        Menu declared, choices;
        declared.movie = choices.movie = 1;
        declared.choices_only = true;
        declared.items =
            choices.items = {{"Next episode", "", true}, {"Return to main menu", "", true}};
        choices.cursor = 0;
        check(declared_choices_match(declared, choices),
              "explicit blank-title action menu can omit a question");
        declared.pending = true;
        check(!declared_choices_match(declared, choices),
              "disabled action owner cannot authorize a dialog");
        declared.pending = false;
        choices.items = {{"Yes", "", true}, {"No", "", true}};
        check(
            !declared_choices_match(declared, choices),
            "an unfinished confirmation cannot borrow permission from the underlying action menu");
        auto record = parse_episode_record(
            "Time score|00:02:39|B/Battle points|6149|A/Synchronic rate|71%|S");
        check(record && (*record)[0].rank == "B" && (*record)[1].value == "6149" &&
                  (*record)[2].rank == "S",
              "record callback preserves each category rank and score");
        check(!parse_episode_record("Time|1|A/Battle|2|B") &&
                  !parse_episode_record("Time|1|Z/Battle|2|A/Sync|3|B"),
              "partial or unknown record grades cannot become plausible speech");
        RecordBrowse browse;
        browse.capture(1, 2, "000", 1, "Time|1|A/Battle|2|B/Sync|3|-");
        check(browse.scores[1] && (*browse.scores[1])[2].rank == "Unranked",
              "unplayed grade is explicit");
        browse.stat = 2;
        browse.move(2);
        check(browse.difficulty == 2 && browse.stat == 2,
              "difficulty changes preserve the selected statistic");
        browse.move(4);
        browse.move(4);
        check(browse.stat == 0, "statistic browsing wraps independently");
        browse.capture(1, 3, "010", 0, "Time|4|A/Battle|5|B/Sync|6|C");
        check(!browse.scores[1] && browse.story == "010",
              "new movie identity or episode clears stale grades");
        Menu recordMenu;
        recordMenu.movie = 1;
        recordMenu.selector = 2;
        recordMenu.records = recordMenu.readout = true;
        recordMenu.record_difficulty = "NORMAL";
        recordMenu.cursor = 0;
        recordMenu.items = {{"Battle points: 6149. Rank A.", "", true}};
        check(record_navigation_text(recordMenu, false) == "Battle points: 6149. Rank A.",
              "stat navigation omits repeated difficulty");
        check(record_navigation_text(recordMenu, true) == "NORMAL. Battle points: 6149. Rank A.",
              "difficulty navigation announces its new difficulty and current stat");
        auto recordState = menu_selection_state(recordMenu);
        recordMenu.selection_key = 1;
        check(menu_selection_state(recordMenu) != recordState,
              "difficulty changes remain distinct even when both scores are identical");
        check(clean_text(" <font color='#fff'>Sound &amp; Music</font>\n  80 ") ==
                  "Sound & Music 80",
              "text cleanup");
        Menu menu;
        menu.movie = 1;
        menu.selector = 2;
        menu.heading = "Options";
        menu.cursor = 0;
        menu.items = {{"Sound volume, 80", "Adjust volume.", true}, {"Locked", "", false}};
        check(describe(menu, true, true) == "Options. Sound volume, 80. 1 of 2. Adjust volume.",
              "description includes value and help");
        menu.cursor = 1;
        check(describe(menu, false, false) == "Locked. Unavailable. 2 of 2", "disabled control");
        menu.cursor = -1;
        check(describe(menu, true, true).empty(), "invalid cursor");
        std::vector<unsigned char> data(0x10000);
        auto put = [&](uint32_t at, uint32_t v) {
            for (int i = 0; i < 4; i++)
                data[at + i] = v >> (24 - 8 * i);
        };
        Memory fake([&](uint32_t at, void *out, size_t n) {
            if (uint64_t(at) + n > data.size())
                return false;
            memcpy(out, data.data() + at, n);
            return true;
        });
        Reader reader;
        put(0x100 + 0x1c, 0x200);
        put(0x200, 1);
        put(0x204, 1);
        put(0x208, 0xffffffff);
        put(0x210, 0x300);
        put(0x300, 0x400);
        memcpy(data.data() + 0x400, "m_cursor", 9);
        data[0x218] = 3;
        put(0x220, 0x40000000);
        put(0x228, 0xfffffffe);
        auto members = reader.members(fake, 0x100);
        check(members.size() == 1 && members.at("m_cursor").number == 2,
              "occupied -1 bucket and big-endian number");
        put(0x204, 0x100000);
        check(reader.members(fake, 0x100).empty(), "reject corrupt hash capacity");
        put(0x500 + 0xbc, 0x600);
        put(0x608, 0x700);
        put(0x70c, 0x800);
        put(0x710, 1);
        put(0x714, 1);
        put(0x800, 0x900);
        put(0x900, 0xa00);
        put(0x904, 3);
        put(0x908, 3);
        data[0xa00] = 0;
        data[0xa01] = 'O';
        data[0xa02] = 0;
        data[0xa03] = 'K';
        check(reader.native_text(fake, 0x500) == "OK", "native UTF-16 text paragraph");
        put(0x904, 4097);
        check(reader.native_text(fake, 0x500).empty(), "reject corrupt native text length");
        put(0xb00 + 0x20, 0xc00);
        put(0xb00 + 0xa0, 1);
        put(0xc00 + 0xa0, 3);
        check(!reader.visible(fake, 0xb00), "degenerate ancestor excludes selector");
        put(0xc00 + 0xa0, 1);
        check(reader.visible(fake, 0xb00), "visible ancestor chain");
        put(0xc00 + 0xa0, 0);
        check(!reader.visible(fake, 0xb00), "explicitly invisible ancestor excludes selector");
        put(0xc00 + 0xa0, 1);
        put(0x4020, 0x5000);
        put(0x41c8, 0x6000);
        put(0x51c8, 0x7000);
        put(0x601c, 0x8000);
        put(0x701c, 0x8100);
        put(0x8000, 1);
        put(0x8004, 3);
        put(0x8100, 2);
        put(0x8104, 3);
        for (uint32_t table : {0x8000u, 0x8100u})
            for (unsigned i = 0; i < 4; i++)
                put(table + 8 + i * 32, 0xfffffffe);
        put(0x9000, 0x9100);
        memcpy(data.data() + 0x9100, "m_input", 8);
        put(0x9010, 0x9120);
        memcpy(data.data() + 0x9120, "m_gameDiffHelp", 15);
        put(0x9020, 0x9140);
        memcpy(data.data() + 0x9140, "tl_opend", 9);
        auto boolSlot = [&](uint32_t entry, uint32_t key, bool value) {
            put(entry, 0xffffffff);
            put(entry + 8, key);
            data[entry + 16] = 2;
            data[entry + 20] = value;
        };
        boolSlot(0x8008, 0x9000, true);
        boolSlot(0x8108, 0x9000, false);
        boolSlot(0x8128, 0x9010, true);
        check(!reader.selector_input_active(fake, 0x4000),
              "New Game handoff suppresses retained child selector");
        data[0x813c] = 0;
        check(reader.selector_input_active(fake, 0x4000),
              "ordinary delegated input remains available without handoff phase");
        data[0x813c] = 1;
        boolSlot(0x8028, 0x9020, false);
        put(0x8000, 2);
        check(reader.selector_input_active(fake, 0x4000),
              "dialog choices retain their own pending lifecycle during handoff");
        put(0x8028, 0xfffffffe);
        put(0x8000, 1);
        memcpy(data.data() + 0x9120, "m_decide", 9);
        check(!reader.selector_input_active(fake, 0x4000),
              "confirmed action cannot revive its retained child selector");
        data[0x813c] = 0;
        check(reader.selector_input_active(fake, 0x4000),
              "cancelled decision restores delegated menu input");
        memcpy(data.data() + 0x9120, "m_closeOperation", 17);
        put(0x9030, 0x9160);
        memcpy(data.data() + 0x9160, "m_proc", 7);
        boolSlot(0x8148, 0x9030, false);
        put(0x8100, 3);
        check(!reader.selector_input_active(fake, 0x4000),
              "disabled pause owner suppresses child speech during exit");
        data[0x811c] = 1;
        check(reader.selector_input_active(fake, 0x4000),
              "reactivated pause owner allows normal return announcements");
        // A delegated controller owns a selector whose original input handler
        // remains disabled/decided, including after returning from a submenu.
        memcpy(data.data() + 0x9120, "m_decide", 9);
        boolSlot(0x8028, 0x9010, true);
        put(0x8000, 2);
        data[0x801c] = 0;
        put(0x8100, 7);
        put(0x8104, 7);
        put(0x81e8, 0xfffffffe);
        constexpr std::array<const char *, 4> delegatedKeys{"m_mainItems", "m_subItems",
                                                            "m_mainIdx", "m_subIdx"};
        for (unsigned i = 0; i < delegatedKeys.size(); ++i) {
            auto handle = 0x9040 + i * 0x10, label = 0x9180 + i * 0x20;
            auto entry = 0x8168 + i * 32;
            put(handle, label);
            memcpy(data.data() + label, delegatedKeys[i], strlen(delegatedKeys[i]) + 1);
            put(entry, 0xffffffff);
            put(entry + 8, handle);
            data[entry + 16] = i < 2 ? 6 : 3;
            put(entry + 24, 0);
            put(entry + 28, 0);
        }
        data[0x8158] = 3;
        put(0x8160, 0);
        put(0x8164, 0);
        check(reader.selector_input_active(fake, 0x4000),
              "active delegated owner authorizes its retained decided child");
        put(0x8160, 0x3ff00000);
        check(!reader.selector_input_active(fake, 0x4000),
              "delegated dialog phase cannot authorize retained child selection");
        data[0x803c] = 0;
        check(!reader.selector_input_active(fake, 0x4000),
              "delegated non-menu phase blocks independently of the child's decision flag");
        put(0x8160, 0);
        data[0x811c] = 0;
        data[0x803c] = 0;
        check(!reader.selector_input_active(fake, 0x4000),
              "disabled delegated owner blocks even a child without a decided flag");
        data[0x811c] = 1;
        data[0x803c] = 1;
        check(reader.selector_input_active(fake, 0x4000),
              "return to delegated menu restores child selection");
        {
            std::vector<uint8_t> lifecycle(0x4000);
            auto stateWord = [&](uint32_t at, uint32_t value) {
                for (unsigned i = 0; i < 4; ++i)
                    lifecycle[at + i] = uint8_t(value >> (24 - 8 * i));
            };
            ReaderLayout layout;
            layout.names_global = 0x100;
            Reader lifecycleReader(layout);
            Memory memory([&](uint32_t at, void *out, size_t n) {
                if (uint64_t(at) + n > lifecycle.size())
                    return false;
                std::memcpy(out, lifecycle.data() + at, n);
                return true;
            });
            stateWord(0x100, 0x200);
            stateWord(0x104, 3);
            for (unsigned i = 0; i < 3; ++i)
                stateWord(0x200 + i * 4, 0x300 + i * 0x80);
            std::strcpy(reinterpret_cast<char *>(lifecycle.data() + 0x308), "BCGFxMovieMenu");
            std::strcpy(reinterpret_cast<char *>(lifecycle.data() + 0x388), "BCGFxUI_Common");
            std::strcpy(reinterpret_cast<char *>(lifecycle.data() + 0x408), "AnyMenuSubclass");
            stateWord(0x1034, 0x1400);
            stateWord(0x142c, 2);
            stateWord(0x1440, 0x1500);
            stateWord(0x152c, 0);
            stateWord(0x10c8, 0x80000000);
            for (uint8_t state : {uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(4)}) {
                lifecycle[0x1174] = state;
                check(lifecycleReader.menu_movie_active(memory, 0x1000) == (state < 2),
                      "every menu subclass obeys the shared open/closing lifecycle");
            }
            stateWord(0x142c, 1);
            lifecycle[0x1174] = 3;
            check(lifecycleReader.menu_movie_active(memory, 0x1000),
                  "common dialog keeps its separate lifecycle even with native state closed");
            stateWord(0x10c8, 0);
            check(!lifecycleReader.menu_movie_active(memory, 0x1000),
                  "unloaded common dialog remains inactive");
            stateWord(0x20c, 0x480);
            std::strcpy(reinterpret_cast<char *>(lifecycle.data() + 0x488),
                        "BCGFxUI_IntervalDrama");
            stateWord(0x104, 4);
            stateWord(0x142c, 3);
            stateWord(0x1020, 7);
            stateWord(0x10c8, 0x80000000);
            check(!lifecycleReader.menu_movie_active(memory, 0x1000),
                  "script-managed scene does not bypass closed state without an observed start");
            lifecycleReader.script_scene(memory, 0x1000, 7, true);
            check(lifecycleReader.menu_movie_active(memory, 0x1000),
                  "observed script start enables a scene whose native UI state remains closed");
            lifecycle[0x1174] = 2;
            check(!lifecycleReader.menu_movie_active(memory, 0x1000),
                  "explicit native closing state still excludes a script-managed scene");
            lifecycle[0x1174] = 3;
            lifecycleReader.script_scene(memory, 0x1000, 7, false);
            check(!lifecycleReader.menu_movie_active(memory, 0x1000),
                  "script end excludes retained dialog and text before display flags clear");
            lifecycleReader.script_scene(memory, 0x1000, 7, true);
            stateWord(0x1020, 8);
            check(!lifecycleReader.menu_movie_active(memory, 0x1000),
                  "reused movie identity cannot inherit an old scene start");
            lifecycleReader.script_scene(memory, 0x1000, 8, true);
            lifecycleReader.clear_script_scenes();
            check(!lifecycleReader.menu_movie_active(memory, 0x1000),
                  "discarded event batch fails closed instead of retaining script scene state");
        }
        Menu status;
        status.message = true;
        status.cursor = 0;
        status.items.push_back({"Press Start", "", true});
        check(describe(status, true, true) == "Press Start",
              "screen messages have no menu numbering");
        Menu confirmation;
        confirmation.modal = true;
        confirmation.heading = "Restart the game now?";
        confirmation.cursor = 1;
        confirmation.items = {{"Yes", "", true}, {"No", "", true}};
        check(describe(confirmation, true, true) == "Restart the game now? No",
              "confirmation includes prompt and selected answer on entry");
        confirmation.cursor = 0;
        check(describe(confirmation, false, true) == "Yes",
              "confirmation navigation excludes prompt and other answers");
        auto dialogContext = menu_context(confirmation);
        confirmation.cursor = 1;
        check(menu_context(confirmation) == dialogContext, "answer changes retain dialog context");
        auto nextQuestion = confirmation;
        nextQuestion.heading = "Start on Normal?";
        check(
            menu_context(nextQuestion) != dialogContext,
            "new question on a reused panel gets a new context even with the same selected answer");
        confirmation.pending = true;
        check(describe(confirmation, true, true).empty(),
              "wait for confirmation selector rather than reading all answers");
        Menu collection;
        collection.collection = true;
        collection.heading = "GAUGES";
        collection.acquired = "1/9";
        collection.cursor = 0;
        collection.items.push_back({"Avenger", "Asura's default gauge.", true});
        check(describe(collection, true, true) ==
                  "GAUGES. Acquired: 1/9. Avenger: Asura's default gauge.",
              "acquired total on collection entry");
        check(describe(collection, false, true) == "Avenger: Asura's default gauge.",
              "collection movement includes effect but excludes heading and acquired total");
        collection.heading.clear();
        check(describe(collection, true, true) == "Acquired: 1/9. Avenger: Asura's default gauge.",
              "empty collection heading adds no leading punctuation");
        collection.items[0] = {"???", "Complete the requirement.", false};
        check(describe(collection, false, true) == "Locked: Complete the requirement",
              "locked item announces requirement without placeholder or double punctuation");
        Value gridCursor;
        gridCursor.type = 3;
        gridCursor.number = 12;
        Value gridPage = gridCursor;
        gridPage.number = 1;
        Value pageSize = gridCursor;
        pageSize.type = 4;
        pageSize.number = 10;
        check(collection_grid_index(&gridCursor, &gridPage, &pageSize, 45) == 12,
              "paged collection retains its absolute cursor");
        gridCursor.type = 4;
        check(collection_grid_index(&gridCursor, &gridPage, &pageSize, 45) == 12,
              "integer collection cursor");
        gridPage.number = 4;
        gridCursor.number = 44;
        check(collection_grid_index(&gridCursor, &gridPage, &pageSize, 45) == 44,
              "partial last collection page");
        gridCursor.number = 45;
        check(collection_grid_index(&gridCursor, &gridPage, &pageSize, 45) == -1,
              "reject empty cell on partial collection page");
        gridPage.number = 1;
        gridCursor.number = 2;
        check(collection_grid_index(&gridCursor, &gridPage, &pageSize, 45) == -1,
              "wait for coherent page and absolute cursor during tab switching");
        gridCursor.number = 15;
        pageSize.number = 15;
        check(collection_grid_index(&gridCursor, &gridPage, &pageSize, 30) == 15,
              "movie second tab starts at absolute cursor fifteen");
        gridCursor.number = std::numeric_limits<double>::quiet_NaN();
        check(collection_grid_index(&gridCursor, &gridPage, &pageSize, 45) == -1,
              "reject invalid collection cursor");
        gridCursor.number = 2;
        pageSize.number = 0;
        check(collection_grid_index(&gridCursor, &gridPage, &pageSize, 45) == -1,
              "reject zero page size");
        Menu lockedGrid = collection;
        lockedGrid.items[0] = {"???", "", false};
        lockedGrid.collection_count = 45;
        lockedGrid.collection_position = 0;
        auto firstLocked = describe(lockedGrid, false, true);
        lockedGrid.collection_position = 1;
        check(firstLocked == "Locked. 1 of 45" &&
                  describe(lockedGrid, false, true) == "Locked. 2 of 45",
              "identical locked grid entries remain distinguishable on movement");
        int snapshotArg = 1;
        const BuildProfile *selectedProfile = &disc_profile;
        Menu result;
        result.movie = 50;
        result.selector = 51;
        result.heading = "Results";
        result.summary = "Overall rank B. Battle points: 6000 points. Rank A.";
        result.cursor = 0;
        result.items = {{"Next", "Continue", true}, {"Return", "", true}};
        check(menu_repeat_text({result}, 50) == describe(result, true, true),
              "result repeat includes summary and selected choice");
        result.cursor = 1;
        check(menu_repeat_text({result}, 50).find("Overall rank B") != std::string::npos &&
                  menu_repeat_text({result}, 50).find("Return") != std::string::npos,
              "result repeat retains scores after selector navigation");
        result.pending = true;
        check(menu_repeat_text({result}, 50).empty(), "result repeat waits for finished scores");
        result.pending = false;
        Menu breakdown;
        breakdown.movie = 50;
        breakdown.selector = 52;
        breakdown.message = breakdown.readout = true;
        breakdown.cursor = 0;
        breakdown.items = {{"Battle point breakdown. Shots: 10, 50 points.", "", true}};
        check(menu_repeat_text({breakdown}, 50) == breakdown.items[0].label,
              "detail repeat reads the currently active breakdown");
        check(menu_repeat_text({menu}, 50).empty() && menu_repeat_text({result}, 999).empty(),
              "repeat does not narrate another screen or movie");
        auto ordinary = menu;
        ordinary.movie = 50;
        check(menu_repeat_text({ordinary}, 50).empty(),
              "ordinary menus in the same movie do not acquire a results repeat action");
        Menu episode = result;
        episode.summary = "An episode introduction.";
        episode.repeat_text = episode.summary;
        episode.items = {{"Play", "Start episode.", true}, {"Record", "View ranks.", true}};
        check(menu_repeat_text({episode}, 50) == episode.summary,
              "episode repeat reads the introduction separately from the selected action");
        episode.pending = true;
        check(menu_repeat_text({episode}, 50).empty(),
              "episode repeat stays silent during an unfinished transition");
        if (argc > 1 && std::string_view(argv[1]).starts_with("--profile=")) {
            selectedProfile = profile_named(std::string_view(argv[1]).substr(10));
            check(selectedProfile != nullptr, "known snapshot profile");
            snapshotArg++;
        }
        if (argc > snapshotArg) {
            std::string root = argv[snapshotArg];
            auto load = [](const std::string &p) {
                std::ifstream in(p, std::ios::binary);
                if (!in)
                    throw std::runtime_error("Missing snapshot " + p);
                return std::vector<char>(std::istreambuf_iterator<char>(in), {});
            };
            auto low = load(root + "/heap-low.bin"), high = load(root + "/heap-high.bin"),
                 image = load(root + "/guest-image.bin");
            std::ifstream globals(root + "/globals.bin", std::ios::binary);
            if (globals)
                globals.read(image.data() + 0x12a9000, 0x2000);
            uint64_t reads = 0;
            Memory memory([&](uint32_t a, void *out, size_t n) {
                reads++;
                for (auto [base, buffer] : std::array<std::pair<uint32_t, std::vector<char> *>, 3>{
                         {{0, &low}, {0x40000000, &high}, {0x82000000, &image}}}) {
                    if (a >= base && uint64_t(a) - base + n <= buffer->size()) {
                        memcpy(out, buffer->data() + a - base, n);
                        return true;
                    }
                }
                return false;
            });
            Reader r(selectedProfile->layout);
            check(r.ready(memory), "snapshot Unreal globals");
            r.discover(memory);
            std::ofstream diagnostic(root + "/diagnostic.txt");
            r.diagnostic(memory, diagnostic);
            diagnostic.close();
            auto menus = r.menus(memory);
            check(!menus.empty(), "snapshot has a readable active menu");
            {
                auto movie = menus.back().movie;
                auto slot = memory.u32(r.layout().objects_global) + memory.u32(movie + 0x20) * 4;
                if (uint64_t(slot) + 4 <= low.size()) {
                    char saved[4];
                    memcpy(saved, low.data() + slot, 4);
                    memset(low.data() + slot, 0, 4);
                    Reader late(selectedProfile->layout);
                    late.discover(memory);
                    memcpy(low.data() + slot, saved, 4);
                    late.discover(memory);
                    auto published = late.menus(memory);
                    check(std::any_of(published.begin(), published.end(),
                                      [&](const Menu &menu) { return menu.movie == movie; }),
                          "rediscover menu published in an existing object slot");
                }
            }
            {
                auto movie = menus.back().movie;
                auto at = uint64_t(movie) + 0x174 - 0x40000000;
                if (at < high.size() && r.class_name(memory, movie) != "BCGFxUI_Common") {
                    auto saved = high[at];
                    for (char state : {char(2), char(3)}) {
                        high[at] = state;
                        auto closing = r.menus(memory);
                        check(std::none_of(closing.begin(), closing.end(),
                                           [&](const Menu &menu) { return menu.movie == movie; }),
                              "closing retained movie cannot narrate any of its decoded panels");
                    }
                    high[at] = saved;
                    check(r.menus(memory) == menus,
                          "reopened movie retains its original decoded menus");
                }
            }
            if (argc - snapshotArg == 2 && std::string(argv[snapshotArg + 1]) == "--bench") {
                reads = 0;
                auto start = std::chrono::steady_clock::now();
                for (int i = 0; i < 40; i++) {
                    auto result = r.menus(memory);
                    check(result == menus, "stable repeated snapshot result");
                }
                auto ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - start)
                              .count() /
                          40;
                std::cout << "POLL " << ms << " ms, " << reads / 40 << " reads\n";
                reads = 0;
                start = std::chrono::steady_clock::now();
                for (int i = 0; i < 10; i++)
                    r.discover(memory);
                ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                               start)
                         .count() /
                     10;
                std::cout << "DISCOVER " << ms << " ms, " << reads / 10 << " reads\n";
            } else if (argc - snapshotArg == 2)
                check(menus.back().items[menus.back().cursor].label == argv[snapshotArg + 1],
                      "snapshot selected item matches expected label");
            if (menus.back().collection) {
                auto object = memory.u32(menus.back().selector + 0x1c8),
                     table = memory.u32(object + 0x1c), mask = memory.u32(table + 4);
                for (uint32_t i = 0; i <= mask && mask < 4096; i++) {
                    auto entry = table + 8 + i * 32;
                    if (memory.u32(entry) == 0xfffffffe ||
                        memory.string(memory.u32(memory.u32(entry + 8))) != "m_input" ||
                        uint64_t(entry) + 32 > low.size())
                        continue;
                    char saved = low[entry + 20];
                    low[entry + 20] = 0;
                    auto background = r.menus(memory);
                    low[entry + 20] = saved;
                    check(std::none_of(background.begin(), background.end(),
                                       [](const Menu &menu) { return menu.collection; }),
                          "inactive retained collection panel cannot override its parent menu");
                    check(r.menus(memory) == menus,
                          "reactivated collection panel restores its selection");
                    break;
                }
            }
            if (r.class_name(memory, menus.back().movie) == "BCGFxUI_TitleMenu" &&
                !menus.back().message) {
                auto at = menus.back().movie + 0x208;
                check(uint64_t(at) + 4 <= high.size() + 0x40000000ull && at >= 0x40000000,
                      "title readiness flags are captured");
                auto offset = at - 0x40000000;
                char saved = high[offset];
                high[offset] = char(uint8_t(saved) & ~0x40u);
                auto disabled = r.menus(memory);
                high[offset] = saved;
                if (menus.back().modal)
                    check(disabled == menus, "startup readiness gate preserves modal choices");
                else
                    check(
                        std::none_of(disabled.begin(), disabled.end(),
                                     [&](const Menu &menu) {
                                         return menu.movie == menus.back().movie && !menu.modal;
                                     }),
                        "startup cannot announce the title selector before native input is enabled");
                check(r.menus(memory) == menus,
                      "title readiness restores selection without navigation");
            }
            if (menus.back().modal && !menus.back().message) {
                check(!menus.back().heading.empty(),
                      "confirmation retains the question as well as the selected button");
                std::vector<uint32_t> todo{menus.back().selector};
                std::set<uint32_t> seen;
                bool tested = false;
                while (!todo.empty() && !tested) {
                    auto c = todo.back();
                    todo.pop_back();
                    if (!seen.insert(c).second)
                        continue;
                    auto fields = r.members(memory, memory.u32(c + 0x1c8));
                    auto items = Reader::get(fields, "m_items");
                    if (items && items->type == 6 && memory.u32(items->pointer + 0x3c) > 0) {
                        auto at = items->pointer + 0x3c;
                        if (uint64_t(at) + 4 <= low.size()) {
                            char saved[4];
                            memcpy(saved, low.data() + at, 4);
                            memset(low.data() + at, 0, 4);
                            auto opening = r.menus(memory);
                            memcpy(low.data() + at, saved, 4);
                            check(
                                !opening.empty() && opening.back().modal && opening.back().pending,
                                "opening choice dialog blocks background menu until selector is published");
                            check(describe(opening.back(), true, true).empty(),
                                  "opening dialog cannot announce all choices");
                            auto ready = r.menus(memory);
                            check(ready == menus,
                                  "published confirmation restores selected answer");
                            tested = true;
                        }
                    }
                    auto kids = r.children(memory, c);
                    todo.insert(todo.end(), kids.begin(), kids.end());
                }
                check(tested, "confirmation opening-state regression exercised");
            }
            if (!menus.back().message && !menus.back().collection && !menus.back().settings) {
                auto target = menus.back();
                std::vector<uint32_t> todo{target.selector};
                std::set<uint32_t> seen;
                bool tested = false;
                while (!todo.empty() && !tested) {
                    auto c = todo.back();
                    todo.pop_back();
                    if (!seen.insert(c).second)
                        continue;
                    auto object = memory.u32(c + 0x1c8);
                    auto fields = r.members(memory, object);
                    auto items = Reader::get(fields, "m_items"),
                         cursor = Reader::get(fields, "m_cursor");
                    if (items && cursor &&
                        memory.u32(items->pointer + 0x3c) == target.items.size()) {
                        auto table = memory.u32(object + 0x1c), mask = memory.u32(table + 4);
                        for (uint32_t i = 0; i <= mask && mask < 4096; i++) {
                            auto e = table + 8 + i * 32;
                            if (memory.u32(e) == 0xfffffffe ||
                                memory.string(memory.u32(memory.u32(e + 8))) != "m_cursor")
                                continue;
                            if (uint64_t(e) + 32 > low.size())
                                continue;
                            char saved[16];
                            memcpy(saved, low.data() + e + 16, 16);
                            low[e + 16] = 4;
                            for (int j = 0; j < 4; j++)
                                low[e + 24 + j] = uint32_t(target.cursor) >> (24 - 8 * j);
                            auto integerSelection = r.menus(memory);
                            memcpy(low.data() + e + 16, saved, 16);
                            check(
                                integerSelection == menus,
                                "initial integer cursor produces the same menu and focused choice as a floating-point cursor");
                            tested = true;
                            break;
                        }
                    }
                    auto kids = r.children(memory, c);
                    todo.insert(todo.end(), kids.begin(), kids.end());
                }
                check(tested, "integer-cursor regression exercised");
            }
            if (menus.back().modal && !menus.back().message) {
                auto object = memory.u32(menus.back().selector + 0x1c8),
                     table = memory.u32(object + 0x1c), mask = memory.u32(table + 4);
                bool exercised = false;
                for (uint32_t i = 0; i <= mask && mask < 4096; i++) {
                    auto entry = table + 8 + i * 32;
                    if (memory.u32(entry) == 0xfffffffe ||
                        memory.string(memory.u32(memory.u32(entry + 8))) != "tl_opend" ||
                        uint64_t(entry) + 32 > low.size())
                        continue;
                    char saved = low[entry + 20];
                    low[entry + 20] = 0;
                    auto opening = r.menus(memory);
                    low[entry + 20] = saved;
                    check(!opening.empty() && opening.back().pending,
                          "dialog choices remain pending before the dialog opens");
                    exercised = true;
                    break;
                }
                check(exercised, "opening-dialog regression exercised");
            }
            if (menus.back().modal && !menus.back().message) {
                bool handoff = false;
                std::vector<uint32_t> nodes;
                auto movie = menus.back().movie;
                for (auto offset : {0x17cu, 0x180u}) {
                    auto gfx = memory.u32(movie + offset);
                    if (memory.u32(gfx + 0x40) == 0x48)
                        nodes.push_back(memory.u32(memory.u32(gfx + 0x44) + 4));
                }
                std::set<uint32_t> seen;
                while (!nodes.empty() && seen.size() < 3000) {
                    auto c = nodes.back();
                    nodes.pop_back();
                    if (!seen.insert(c).second)
                        continue;
                    auto fields = r.members(memory, memory.u32(c + 0x1c8));
                    auto help = Reader::get(fields, "m_gameDiffHelp"),
                         input = Reader::get(fields, "m_input");
                    if (help && help->type == 2 && help->boolean && input && input->type == 2 &&
                        !input->boolean)
                        handoff = true;
                    auto kids = r.children(memory, c);
                    nodes.insert(nodes.end(), kids.begin(), kids.end());
                }
                if (handoff) {
                    auto flags = menus.back().selector + 0xa0;
                    check(uint64_t(flags) + 4 <= low.size(), "handoff dialog flags are captured");
                    char saved = low[flags + 3];
                    low[flags + 3] = char(uint8_t(saved) & ~1u);
                    auto gap = r.menus(memory);
                    low[flags + 3] = saved;
                    check(
                        gap.empty(),
                        "difficulty-to-confirmation gap cannot announce the dormant title menu or fallback labels");
                    check(r.menus(memory) == menus, "dialog restored after handoff test");
                }
            }
            for (const auto &m : menus) {
                std::cout << describe(m, true, true) << "\n";
                for (const auto &i : m.items)
                    std::cout << "  " << (i.enabled ? "" : "[disabled] ") << i.label << "\n";
            }
        }
        std::cout << "Reader checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
