#include "../src/lock_trace.h"
#include "../src/shooting_assist.h"
#include <iostream>
#include <stdexcept>
using namespace a11y;
static void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main() {
    try {
        LockCamera camera{{0, 0, 0}, {1, 0, 0}, 70, true};
        auto radius = stable_lock_radius(camera, {0, 0, 0}, {1, 0, 0});
        check(radius && std::abs(*radius - 37) < .001f,
              "centered cone uses measured FOV and reserve");
        auto offset = stable_lock_radius(camera, {0, 0, 0}, {.8660254f, .5f, 0});
        check(offset && std::abs(*offset - 7) < .001f, "offset aim fits the camera cone");
        check(stable_lock_radius(camera, {0, 0, 0}, {0, 1, 0}) == 0,
              "aim outside camera gets zero safe radius");
        check(!stable_lock_radius(camera, {1, 0, 0}, {1, 0, 0}) &&
                  !stable_lock_radius(camera, {0, 0, 0}, {0, 0, 0}),
              "different origins and zero aim fail closed");
        camera.fov = std::numeric_limits<float>::quiet_NaN();
        camera.valid = false;
        check(!stable_lock_radius(camera, {}, {1, 0, 0}), "invalid camera has no cap");
        std::vector<uint8_t> bytes(0x5000);
        auto word = [&](uint32_t at, uint32_t value) {
            for (unsigned i = 0; i < 4; i++)
                bytes[at + i] = uint8_t(value >> (24 - 8 * i));
        };
        auto put = [&](uint32_t at, float value) { word(at, std::bit_cast<uint32_t>(value)); };
        Memory memory([&](uint32_t at, void *out, size_t n) {
            if (uint64_t(at) + n > bytes.size())
                return false;
            memcpy(out, bytes.data() + at, n);
            return true;
        });
        auto write = [&](uint32_t at, const void *data, size_t n) {
            if (uint64_t(at) + n > bytes.size())
                return false;
            memcpy(bytes.data() + at, data, n);
            return true;
        };
        const uint32_t pc = 0x1000;
        check(boosted_cone(3.6f, 14, 2, 3) == ConeBounds{3.6f * 3, 14.f * 2},
              "boost scales the far and near limits independently");
        check(boosted_cone(3.6f, 14, 1, 5) == ConeBounds{3.6f * 5, 3.6f * 5},
              "a far limit boosted past the near limit keeps the cone ordered");
        check(boosted_cone(30, 60, 2, 2) == ConeBounds{60, 89},
              "boosted limits never exceed 89 degrees");
        check(boosted_cone(3.6f, 14, 2, 3, 5.f) == ConeBounds{5, 14},
              "the safety cap limits added assistance but never narrows native limits");
        check(!boosted_cone(3.6f, 14, .5f, 3) && !boosted_cone(3.6f, 14, 2, 11),
              "multipliers below 1 or above 10 are rejected");
        put(pc + 0x7d4, 3.6f);
        put(pc + 0x7d8, 50);
        {
            AcquisitionScope scope(memory, write, pc, 1, 5, 7);
            check(scope.changed() && std::bit_cast<float>(memory.u32(pc + 0x7d4)) == 7 &&
                      std::bit_cast<float>(memory.u32(pc + 0x7d8)) == 50,
                  "cap limits added assistance without narrowing the native near cone");
        }
        check(std::bit_cast<float>(memory.u32(pc + 0x7d8)) == 50,
              "candidate cap restores native fields");
        {
            AcquisitionScope scope(memory, write, pc, 1, 5, 0);
            check(std::bit_cast<float>(memory.u32(pc + 0x7d8)) == 50,
                  "zero safe radius preserves native acquisition thresholds");
            put(pc + 0x7d8, 60);
        }
        check(std::bit_cast<float>(memory.u32(pc + 0x7d8)) == 60,
              "legitimate in-call game change survives cap restoration");
        {
            AcquisitionScope scope(memory, write, pc, 1, 1, 7);
            check(!scope.changed() && std::bit_cast<float>(memory.u32(pc + 0x7d8)) == 60,
                  "aid off never caps native acquisition");
        }
        {
            // Component world origin: fourth row of the reflected LocalToWorld matrix.
            const uint32_t component = 0x4000;
            put(component + 0x90 + 48, 1000.f);
            put(component + 0x90 + 52, 0.f);
            put(component + 0x90 + 56, 0.f);
            LockVector position{};
            check(lock_target_position(memory, component, position) && position.x == 1000.f,
                  "component world position is read from the matrix origin row");
            check(!lock_target_position(memory, 0, position),
                  "a missing component has no position");
            put(component + 0x90 + 52, std::numeric_limits<float>::quiet_NaN());
            check(!lock_target_position(memory, component, position),
                  "a non-finite position is rejected");
            put(component + 0x90 + 52, 0.f);
            LockCamera view{{0, 0, 0}, {1, 0, 0}, 65, true};
            auto angle = lock_direction_angle({0, 0, 0}, {1, 0, 0}, {866.0254f, 500.f, 0});
            check(angle && std::abs(*angle - 30.f) < .01f,
                  "direction angle measures from the supplied origin");
            auto margin = lock_view_margin(view, {866.0254f, 500.f, 0});
            check(margin && std::abs(*margin - 5.f) < .01f,
                  "margin is the native (FOV + 5) / 2 limit minus the camera angle");
            margin = lock_view_margin(view, {500.f, 866.0254f, 0});
            check(margin && *margin < 0, "a target past the native limit has negative margin");
            check(!lock_view_margin(view, {0, 0, 0}) &&
                      !lock_view_margin(LockCamera{{0, 0, 0}, {1, 0, 0}, 65, false}, {1, 0, 0}),
                  "degenerate geometry and an invalid camera give no margin");
        }
        word(pc + 0x370, 0x3000);
        put(0x330c, 0);
        put(0x3310, 0);
        put(0x3314, 0);
        word(0x3318, 0);
        word(0x331c, 16384);
        word(0x3320, 0);
        put(0x3324, 70);
        auto captured = lock_camera(memory, pc);
        check(captured.valid && std::abs(captured.forward.y - 1) < .00001f && captured.fov == 70,
              "reflected camera cache captures native FOV and rotation");
        LockTrace trace;
        LockTraceRecord record;
        record.kind = LockTraceKind::acquire;
        record.result = record.accepted = true;
        trace.configure(false);
        check(!trace.begin(1000), "disabled trace never starts");
        trace.push(record, 1001);
        check(trace.take().records.empty(), "disabled callbacks do not record");
        trace.configure(true);
        check(!trace.begin(500, false) && trace.take().started == 0,
              "idle shooting ticks cannot spend the capture window before a real lock");
        check(trace.begin(1000), "active shooting starts capture");
        for (unsigned i = 0; i < 600; i++)
            trace.push(record, 1001 + i);
        auto batch = trace.take();
        check(batch.records.size() == 512 && batch.attempted == 600 && batch.dropped == 88 &&
                  !batch.done,
              "diagnostic overflow is bounded and reported separately");
        for (unsigned i = 600; i < 8192; i++) {
            trace.push(record, 2000);
            if (i % 100 == 0)
                trace.take();
        }
        batch = trace.take();
        check(batch.done && batch.attempted == 8192 && !trace.armed(),
              "total record budget stops producer");
        trace.configure(true);
        trace.begin(1000);
        auto failed = record;
        failed.result = failed.accepted = false;
        for (unsigned i = 0; i < 10000; ++i)
            trace.push(failed, 1001);
        batch = trace.take();
        check(batch.records.size() == 1 && batch.attempted == 1 && batch.filtered == 9999 &&
                  trace.armed(),
              "ten thousand rejected candidates produce one sample without consuming the budget");
        failed.cap_applied = true;
        for (unsigned i = 0; i < 1000; ++i)
            trace.push(failed, 1002);
        check(trace.take().records.empty(),
              "capped and native rejection share a global sample budget");
        failed.kind = LockTraceKind::remove;
        failed.cap_applied = false;
        for (unsigned i = 0; i < 1000; ++i)
            trace.push(failed, 1003);
        check(trace.take().records.size() == 1, "retained targets are sampled separately");
        failed.result = true;
        trace.push(failed, 1004);
        trace.push(failed, 1004);
        check(trace.take().records.size() == 2, "actual removals are never coalesced");
        check(trace.begin(600000) && trace.armed(),
              "a long pause between active ticks does not spend the capture window");
        uint64_t now = 600000;
        bool running = true;
        for (unsigned i = 0; i < 2000 && running; ++i)
            running = trace.begin(now += 33);
        batch = trace.take();
        check(!running && batch.done && std::string(batch.reason) == "time_limit" &&
                  batch.active >= 30000 && batch.active < 30100,
              "time limit counts thirty seconds of active shooting ticks");
        trace.configure(true);
        trace.begin(0);
        trace.push(record, 0);
        batch = trace.take();
        check(batch.records.size() == 1 && batch.records[0].tick == 0,
              "zero test clock does not underflow");
        trace.stop();
        check(trace.take().done, "shutdown terminates partial capture");
        record.fov = std::numeric_limits<float>::quiet_NaN();
        auto line = lock_trace_line(record, "cue\"\n");
        check(line.find("\"fov\":null") != std::string::npos &&
                  line.find('\n') == std::string::npos,
              "invalid floats and unusual metadata cannot corrupt JSONL");
        auto path = std::filesystem::temp_directory_path() /
                    std::filesystem::path("blind-rage-lock-trace-test.jsonl");
        {
            LockTraceWriter writer(path);
            check(writer.valid(), "open isolated trace fixture");
            LockTraceBatch many;
            many.records.resize(8192, record);
            many.done = true;
            many.reason = "record_limit";
            many.attempted = 8192;
            writer.drain(std::move(many), [](CueName) { return std::string(128, 'Q'); });
            check(writer.finished(), "writer finishes at a bounded limit");
        }
        check(std::filesystem::file_size(path) <= 2 * 1024 * 1024,
              "trace output never exceeds two MiB");
        std::filesystem::remove(path);
        std::cout << "Lock trace bounds, camera geometry and gated acquisition tests passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
