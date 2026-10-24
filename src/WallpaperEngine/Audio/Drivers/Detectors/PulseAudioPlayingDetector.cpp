#include "PulseAudioPlayingDetector.h"
#include "WallpaperEngine/Logging/Log.h"

#include <cstring>
#include <unistd.h>

namespace {
// per frame this was ~60 blocking round-trips/s per instance, enough to stall pipewire-pulse
constexpr auto CHECK_INTERVAL = std::chrono::milliseconds (500);
constexpr auto RECONNECT_INTERVAL = std::chrono::seconds (5);
} // namespace

namespace WallpaperEngine::Audio::Drivers::Detectors {
void sinkInputInfoCallback (pa_context* context, const pa_sink_input_info* info, int eol, void* userdata) {
    auto* detector = static_cast<PulseAudioPlayingDetector*> (userdata);

    if (eol) {
	detector->finishCheck ();
	return;
    }

    if (info == nullptr) {
	return;
    }

    if (info->proplist == nullptr) {
	return;
    }

    // get processid
    const char* value = pa_proplist_gets (info->proplist, PA_PROP_APPLICATION_PROCESS_ID);

    // paused streams, other wallpaperengine instances (one per screen), waywallen and speech-dispatcher's always-open idle
    // stream are not "other audio playing"; counting them muted every wallpaper forever
    // shortcut: idle-but-uncorked streams are matched by name, upgrade to PipeWire node state if more show up
    const char* name = pa_proplist_gets (info->proplist, PA_PROP_APPLICATION_NAME);
    if (info->corked || info->mute
	|| (name
	    && (strcmp (name, "linux-wallpaperengine") == 0 || strncmp (name, "speech-dispatcher", 17) == 0
		// waywallen's renderers: another wallpaper on another screen, not "other audio playing"
		|| strcmp (name, "Waywallen Renderer") == 0))) {
	return;
    }

    if (value && strtol (value, nullptr, 10) != getpid () && pa_cvolume_avg (&info->volume) != PA_VOLUME_MUTED) {
	detector->foundPlaying ();
    }
}

PulseAudioPlayingDetector::PulseAudioPlayingDetector (
    Application::ApplicationContext& appContext,
    const Render::Drivers::Detectors::FullScreenDetector& fullscreenDetector
) : AudioPlayingDetector (appContext, fullscreenDetector) {
    this->m_mainloop = pa_mainloop_new ();
    this->m_mainloopApi = pa_mainloop_get_api (this->m_mainloop);
    this->connect ();
}

void PulseAudioPlayingDetector::connect () {
    if (this->m_operation) {
	pa_operation_cancel (this->m_operation);
	pa_operation_unref (this->m_operation);
	this->m_operation = nullptr;
    }
    if (this->m_context) {
	pa_context_disconnect (this->m_context);
	pa_context_unref (this->m_context);
    }

    this->m_lastConnect = std::chrono::steady_clock::now ();
    this->m_context = pa_context_new (this->m_mainloopApi, "wallpaperengine");
    // not waited for: a stuck or restarting audio server must not hold the wallpaper up
    if (pa_context_connect (this->m_context, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
	sLog.error ("PulseAudio connection failed, retrying in 5s");
    }
}

void PulseAudioPlayingDetector::foundPlaying () { this->m_found = true; }

void PulseAudioPlayingDetector::finishCheck () { this->setIsPlaying (this->m_found); }

PulseAudioPlayingDetector::~PulseAudioPlayingDetector () {
    if (this->m_operation) {
	pa_operation_cancel (this->m_operation);
	pa_operation_unref (this->m_operation);
    }

    if (this->m_context) {
	pa_context_disconnect (this->m_context);
	pa_context_unref (this->m_context);
    }

    if (this->m_mainloop) {
	pa_mainloop_free (this->m_mainloop);
    }
}

void PulseAudioPlayingDetector::update () {
    if (!this->getApplicationContext ().settings.audio.automute) {
	return this->setIsPlaying (false);
    }
    if (this->getFullscreenDetector ().anythingFullscreen ()) {
	return this->setIsPlaying (true);
    }

    // dispatch whatever arrived, never block the render loop on the audio server
    while (pa_mainloop_iterate (this->m_mainloop, 0, nullptr) > 0) { }

    const auto now = std::chrono::steady_clock::now ();
    const auto state = pa_context_get_state (this->m_context);

    if (!PA_CONTEXT_IS_GOOD (state)) {
	// audio server gone or restarted: nothing to detect until we are back
	this->setIsPlaying (false);
	if (now - this->m_lastConnect >= RECONNECT_INTERVAL) {
	    this->connect ();
	}
	return;
    }

    if (this->m_operation) {
	if (pa_operation_get_state (this->m_operation) == PA_OPERATION_RUNNING) {
	    return;
	}
	pa_operation_unref (this->m_operation);
	this->m_operation = nullptr;
    }

    if (state != PA_CONTEXT_READY || now - this->m_lastCheck < CHECK_INTERVAL) {
	return;
    }

    // the previous state stays until this listing ends (finishCheck), so it does not flicker
    this->m_lastCheck = now;
    this->m_found = false;
    this->m_operation = pa_context_get_sink_input_info_list (this->m_context, sinkInputInfoCallback, this);
}
} // namespace WallpaperEngine::Audio::Drivers::Detectors