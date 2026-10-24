#pragma once

#include "AudioPlayingDetector.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <pulse/pulseaudio.h>

namespace WallpaperEngine::Audio::Drivers::Detectors {
class PulseAudioPlayingDetector final : public AudioPlayingDetector {
public:
    explicit PulseAudioPlayingDetector (
	Application::ApplicationContext& appContext, const Render::Drivers::Detectors::FullScreenDetector&
    );
    ~PulseAudioPlayingDetector () override;

    void update () override;

    /** sink-input listing callbacks */
    void foundPlaying ();
    void finishCheck ();

private:
    void connect ();

    pa_mainloop* m_mainloop = nullptr;
    pa_mainloop_api* m_mainloopApi = nullptr;
    pa_context* m_context = nullptr;
    pa_operation* m_operation = nullptr;
    bool m_found = false;
    std::chrono::steady_clock::time_point m_lastCheck {};
    std::chrono::steady_clock::time_point m_lastConnect {};
};
} // namespace WallpaperEngine::Audio::Drivers::Detectors
