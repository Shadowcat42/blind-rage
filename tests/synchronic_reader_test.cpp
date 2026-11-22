#include "../src/synchronic_reader.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace a11y;
static void check(bool pass, const char *message) {
    if (!pass)
        throw std::runtime_error(message);
}
static PresentationEvent event(PresentationKind kind, uint64_t order, bool flag = true) {
    PresentationEvent e;
    e.kind = kind;
    e.order = order;
    e.movie = 100;
    e.index = 10;
    e.flag = flag;
    e.tag.length = 6;
    const char *tag = "Track0";
    for (unsigned i = 0; i < 6; ++i)
        e.tag.bytes[i * 2 + 1] = uint8_t(tag[i]);
    return e;
}
int main() {
    try {
        SynchronicReader reader([](std::string_view button) {
            return controller_button(button, PromptDevice::playstation);
        });
        TimingState state;
        state.source = 1;
        state.index = 2;
        state.generation = 3;
        state.active = true;
        state.name = {4, 0};
        auto name = [](CueName) { return std::string("Track"); };
        reader.apply(state, 1, name);
        check(reader.take().empty(), "a track without a displayed prompt is quiet");
        reader.apply(event(PresentationKind::sync_start, 2));
        check(reader.take().empty(), "retained track data cannot authorize a new prompt lifecycle");
        reader.apply(state, 3, name);
        auto lines = reader.take();
        check(lines.size() == 1 && lines[0] == "Timing: triangle.",
              "prepare before the accepted window");
        state.window = true;
        reader.apply(state, 4, name);
        check(reader.take().empty(), "the opening window alone does not cue the press");
        state.cue = true;
        reader.apply(state, 4, name);
        check(reader.take().empty(), "a tap's press moment is the count-in, not speech");
        check(reader.repeat() == "Timing: triangle.", "repeat still names the button");
        check(!reader.take_count_in(), "no count-in before the track clock is read");
        state.until = 1.f;
        state.stamp = 10'000'000'000;
        reader.apply(state, 4, name);
        auto count = reader.take_count_in();
        check(count && count->active && count->perfect == 11'000'000'000 &&
                  count->from == 10'000'000'000,
              "count-in lands its high beat on the key's perfect time");
        check(!reader.take_count_in(), "a schedule is taken once");
        state.until = .2f;
        state.stamp = 10'810'000'000;
        reader.apply(state, 4, name);
        count = reader.take_count_in();
        check(count && std::llabs(count->perfect - 11'010'000'000) < 1000 &&
                  count->from == 10'000'000'000,
              "later clock readings resync the same count-in");
        CountInSynth synth;
        std::vector<float> samples(48000 * 2 * 2);
        synth.render(*count, .2f, samples.data(), 96000, 10'000'000'000);
        auto peak = [&](double from, double to) {
            float best = 0;
            for (auto i = size_t(from * 48000); i < size_t(to * 48000); ++i)
                best = std::max(best, std::abs(samples[i * 2]));
            return best;
        };
        auto crossings = [&](double from) {
            int n = 0;
            for (auto i = size_t(from * 48000) + 1; i < size_t((from + .04) * 48000); ++i)
                n += (samples[i * 2 - 2] < 0) != (samples[i * 2] < 0);
            return n;
        };
        check(peak(.03, .11) > .1f && peak(.36, .44) > .1f && peak(.69, .77) > .1f &&
                  peak(1.02, 1.1) > .1f,
              "four beats 0.33 s apart, the last on the perfect time");
        check(peak(0, .015) == 0 && peak(.08, .34) == 0 && peak(.46, .67) == 0 &&
                  peak(.06, .065) > .1f && peak(1.12, 2) == 0,
              "50-ms beats with silence between and after them");
        auto played = synth.played();
        check(played.generation == count->generation &&
                  std::llabs(played.at[0] - (count->perfect - 990'000'000)) < 100'000 &&
                  std::llabs(played.at[3] - count->perfect) < 100'000,
              "each rendered beat records when it is due to play");
        check(crossings(1.011) > crossings(.681) * 3 / 2,
              "the final beat is an octave above the others");
        CountInSynth streamed;
        std::vector<float> block(960), joined;
        for (int n = 0; n < 120; ++n) {
            // Wall-clock estimates that wobble by up to 10 ms, as SamplesPlayed does.
            auto wobble = int64_t((n * 7) % 11 - 5) * 1'000'000;
            streamed.render_stream(*count, .2f, block.data(), 480,
                                   10'000'000'000 + int64_t(n) * 10'000'000 + wobble);
            joined.insert(joined.end(), block.begin(), block.end());
        }
        float jump = 0;
        for (size_t i = 2; i < joined.size(); i += 2)
            jump = std::max(jump, std::abs(joined[i] - joined[i - 2]));
        check(jump < .03f, "streamed blocks keep the tone continuous across block edges");
        CountInCue late{11'000'000'000, 10'500'000'000, 9, true};
        std::fill(samples.begin(), samples.end(), 0.f);
        synth.render(late, .2f, samples.data(), 96000, 10'000'000'000);
        check(peak(.02, .13) == 0 && peak(.35, .46) == 0 && peak(.69, .77) > .1f &&
                  peak(1.01, 1.09) > .1f,
              "beats due before the schedule began are skipped, not cut off");
        state.until = NAN;
        state.stamp = 0;
        state.active = false;
        state.window = false;
        reader.apply(state, 5, name);
        check(reader.take().empty() && reader.repeat().empty(),
              "judge completion invalidates an instruction without speech stop");
        state.active = state.window = state.cue = true;
        state.mash = true;
        state.generation++;
        reader.apply(state, 6, name);
        lines = reader.take();
        check(lines.size() == 1 && lines[0] == "Mash triangle now.",
              "barrage and normal timing have distinct instructions");
        reader.apply(event(PresentationKind::sync_visible, 7, false));
        check(reader.repeat().empty(), "hidden prompt does not speak");
        reader.apply(event(PresentationKind::sync_end, 8));
        check(reader.movies().empty(), "end removes its exact prompt lifecycle");
        reader.apply(event(PresentationKind::sync_start, 9));
        reader.apply(state, 10, name);
        reader.apply(event(PresentationKind::sync_end, 11));
        check(reader.take().empty(), "dismissal in a queued batch suppresses an unsent cue");
        reader.grade(3);
        reader.grade(0);
        reader.grade(1);
        lines = reader.take();
        check(lines.size() == 2 && lines[0] == "Excellent." && lines[1] == "Good.",
              "displayed grades are read and a miss shows no grade");
        check(reader.take().empty(), "a grade is read once");
        std::vector<uint8_t> data(0x8000);
        auto word = [&](uint32_t at, uint32_t value) {
            for (unsigned i = 0; i < 4; ++i)
                data[at + i] = uint8_t(value >> (24 - i * 8));
        };
        Memory memory([&](uint32_t at, void *out, size_t n) {
            if (uint64_t(at) + n > data.size())
                return false;
            std::memcpy(out, data.data() + at, n);
            return true;
        });
        word(0x1020, 2);
        word(0x102c, 4);
        word(0x108c, 0x2000);
        word(0x1090, 2);
        word(0x1094, 2);
        word(0x2000 + 20, 0x40000000);
        word(0x2000 + 24 + 20, 0x40000000);
        TimingState copied;
        check(capture_timing(memory, 0x1000, 0, true, copied) && copied.active && copied.window,
              "first active unjudged track can accept native window");
        // Key 0's perfect time (BCSynchronicTrackKey.StartTime, +8) is 5.0 s.
        word(0x2000 + 8, std::bit_cast<uint32_t>(5.f));
        check(capture_timing(memory, 0x1000, 0, true, copied, 4.5f) && copied.window && !copied.cue,
              "no press cue earlier than 0.2 s before the perfect time");
        check(capture_timing(memory, 0x1000, 0, true, copied, 4.81f) && copied.cue &&
                  std::abs(copied.until - .19f) < 1e-4f && copied.stamp,
              "press cue 0.2 s before the perfect time");
        check(capture_timing(memory, 0x1000, 0, false, copied, 3.9f) && !copied.window &&
                  std::abs(copied.until - 1.1f) < 1e-4f,
              "the track clock is read before the input window opens");
        TimingState unread = copied;
        unread.until = NAN;
        check(unread != copied, "the first clock reading publishes a change");
        TimingState later = copied;
        later.until = .5f;
        later.stamp++;
        check(later == copied, "later clock readings alone do not publish");
        check(capture_timing(memory, 0x1000, 0, false, copied, 4.9f) && !copied.cue,
              "no press cue outside the native input window");
        check(capture_timing(memory, 0x1000, 1, true, copied) && !copied.active && !copied.window,
              "future track waits for predecessor judgment");
        word(0x2000 + 20, 0xc0000000);
        check(capture_timing(memory, 0x1000, 1, true, copied) && copied.active,
              "predecessor judgment enables the next track");
        word(0x2000 + 24 + 20, 0xc0000000);
        check(capture_timing(memory, 0x1000, 1, true, copied) && !copied.active,
              "judged animation cannot remain actionable");
        check(!capture_timing(memory, 0x1000, 2, true, copied) &&
                  !capture_timing(memory, 0x1000, -1, true, copied),
              "track bounds enforced");
        std::cout << "Synchronic lifecycle and native-window tests passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
