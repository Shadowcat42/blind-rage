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
        put(pc + 0x7d4, 3.6f);
        put(pc + 0x7d8, 50);
        {
            AcquisitionScope scope(memory, write, pc, 20, 7);
            check(scope.changed() && std::bit_cast<float>(memory.u32(pc + 0x7d4)) == 7 &&
                      std::bit_cast<float>(memory.u32(pc + 0x7d8)) == 7,
                  "cap also constrains native 50-degree near cone before acquisition");
        }
        check(std::bit_cast<float>(memory.u32(pc + 0x7d8)) == 50,
              "candidate cap restores native fields");
        {
            AcquisitionScope scope(memory, write, pc, 20, 0);
            check(std::bit_cast<float>(memory.u32(pc + 0x7d8)) == 0,
                  "zero radius rejects acquisition without post-filtering side effects");
            put(pc + 0x7d8, 60);
        }
        check(std::bit_cast<float>(memory.u32(pc + 0x7d8)) == 60,
              "legitimate in-call game change survives cap restoration");
        {
            AcquisitionScope scope(memory, write, pc, 0, 7);
            check(!scope.changed() && std::bit_cast<float>(memory.u32(pc + 0x7d8)) == 60,
                  "aid off never caps native acquisition");
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
        trace.configure(false);
        check(!trace.begin(1000), "disabled trace never starts");
        trace.push(record, 1001);
        check(trace.take().records.empty(), "disabled callbacks do not record");
        trace.configure(true);
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
        check(!trace.begin(31000), "wall-clock capture expires without a polling timer");
        batch = trace.take();
        check(batch.done && std::string(batch.reason) == "time_limit", "time limit is explicit");
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
