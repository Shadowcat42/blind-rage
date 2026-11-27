#pragma once
#include "shooting_beacons.h"
#include "fire_warning.h"
#include "synchronic_count_in.h"
#include <windows.h>
#include <xaudio2.h>
#include <atomic>
#include <thread>

namespace a11y {
// ahead: how long the audio already submitted ahead of this block will play.
inline void stream_render(BeaconSynth &synth, const BeaconCue &cue, float gain, float *out,
                          unsigned frames, int64_t) {
    synth.render(cue, gain, out, frames, GetTickCount64());
}
inline void stream_render(FireSynth &synth, const FireCue &cue, float gain, float *out,
                          unsigned frames, int64_t) {
    synth.render(cue, gain, out, frames, GetTickCount64());
}
inline void stream_render(CountInSynth &synth, const CountInCue &cue, float gain, float *out,
                          unsigned frames, int64_t ahead) {
    // The block plays after the audio already submitted; the device's own output
    // latency is not known here and is not compensated.
    synth.render_stream(cue, gain, out, frames, steady_ns() + ahead);
}
// Synthesizes only the mod's cue. It never touches native voices or NVDA.
// This thread services an audio buffer, not game memory or controller input.
template <class Synth, class Cue> class ToneStream {
  public:
    // True while the device is running; false after a critical engine error or a
    // failed buffer submission, until close() and start() rebuild it.
    bool healthy() const {
        return engine_ && !failed_.load();
    }
    ~ToneStream() {
        close();
    }
    bool start(float gain) {
        if (engine_)
            return healthy();
        failed_ = false;
        gain_ = std::clamp(gain, 0.f, .4f);
        module_ = LoadLibraryExW(L"xaudio2_9.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module_)
            return false;
        using Create = HRESULT(WINAPI *)(IXAudio2 **, UINT32, XAUDIO2_PROCESSOR);
        auto create = reinterpret_cast<Create>(GetProcAddress(module_, "XAudio2Create"));
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = 2;
        format.nSamplesPerSec = 48000;
        format.wBitsPerSample = 32;
        format.nBlockAlign = 8;
        format.nAvgBytesPerSec = 384000;
        if (!create || FAILED(create(&engine_, 0, XAUDIO2_DEFAULT_PROCESSOR)) ||
            FAILED(engine_->RegisterForCallbacks(&callback_)) ||
            FAILED(engine_->CreateMasteringVoice(&master_, 2, 48000)) ||
            FAILED(engine_->CreateSourceVoice(&voice_, &format)) || FAILED(voice_->Start())) {
            close();
            return false;
        }
        stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stop_) {
            close();
            return false;
        }
        try {
            thread_ = std::thread([this] { run(); });
        } catch (...) {
            close();
            return false;
        }
        return true;
    }
    void update(Cue cue) {
        std::lock_guard lock(mutex_);
        cue_ = cue;
    }
    void silence() {
        update({});
    }
    const Synth &synth() const {
        return synth_;
    }
    // Configuration before start() only; the audio thread owns the synth after.
    Synth &synth() {
        return synth_;
    }
    void close() {
        if (stop_)
            SetEvent(stop_);
        if (thread_.joinable())
            thread_.join();
        if (voice_) {
            voice_->DestroyVoice();
            voice_ = nullptr;
        }
        if (master_) {
            master_->DestroyVoice();
            master_ = nullptr;
        }
        if (engine_) {
            engine_->UnregisterForCallbacks(&callback_);
            engine_->Release();
            engine_ = nullptr;
        }
        if (module_) {
            FreeLibrary(module_);
            module_ = nullptr;
        }
        if (stop_) {
            CloseHandle(stop_);
            stop_ = nullptr;
        }
    }

  private:
    // A device change or driver reset invalidates the engine; flag it so the
    // owner can rebuild instead of leaving the cue silent for the session.
    struct Callback : IXAudio2EngineCallback {
        explicit Callback(std::atomic<bool> &failed) : failed_(failed) {}
        void STDMETHODCALLTYPE OnProcessingPassStart() override {}
        void STDMETHODCALLTYPE OnProcessingPassEnd() override {}
        void STDMETHODCALLTYPE OnCriticalError(HRESULT) override {
            failed_ = true;
        }
        std::atomic<bool> &failed_;
    };
    void run() {
        unsigned slot = 0;
        while (WaitForSingleObject(stop_, 5) == WAIT_TIMEOUT) {
            XAUDIO2_VOICE_STATE state{};
            voice_->GetState(&state, 0);
            if (state.BuffersQueued >= 3)
                continue;
            Cue cue;
            {
                std::lock_guard lock(mutex_);
                cue = cue_;
            }
            // Refill the queue after a coarse Windows wakeup too. One 10-ms
            // block per wakeup would underrun at the default timer resolution.
            for (unsigned n = state.BuffersQueued; n < 3; ++n) {
                auto &samples = buffers_[slot];
                // SamplesPlayed advances in 10-ms processing passes.
                auto ahead = int64_t(submitted_ - std::min(submitted_, state.SamplesPlayed)) *
                             1'000'000'000 / 48000;
                stream_render(synth_, cue, gain_, samples.data(), 480, ahead);
                XAUDIO2_BUFFER buffer{};
                buffer.AudioBytes = sizeof(samples);
                buffer.pAudioData = reinterpret_cast<const BYTE *>(samples.data());
                if (FAILED(voice_->SubmitSourceBuffer(&buffer))) {
                    failed_ = true;
                    return;
                }
                submitted_ += 480;
                slot = (slot + 1) % buffers_.size();
            }
        }
    }
    uint64_t submitted_{};
    std::atomic<bool> failed_{};
    Callback callback_{failed_};
    HMODULE module_{};
    IXAudio2 *engine_{};
    IXAudio2MasteringVoice *master_{};
    IXAudio2SourceVoice *voice_{};
    HANDLE stop_{};
    std::thread thread_;
    std::mutex mutex_;
    Cue cue_;
    Synth synth_;
    float gain_{};
    std::array<std::array<float, 960>, 8> buffers_{};
};
using BeaconAudio = ToneStream<BeaconSynth, BeaconCue>;
using CountInAudio = ToneStream<CountInSynth, CountInCue>;
using FireAudio = ToneStream<FireSynth, FireCue>;
}
