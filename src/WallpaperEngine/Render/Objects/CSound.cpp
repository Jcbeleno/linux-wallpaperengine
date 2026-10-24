#include <SDL.h>

#include "CSound.h"

#include <algorithm>
#include <random>
#include <ranges>

#include "WallpaperEngine/FileSystem/Container.h"

using namespace WallpaperEngine::Render::Objects;

CSound::CSound (Wallpapers::CScene& scene, const Sound& sound) :
    CObject (scene, sound), m_sound (sound), m_playing (!sound.startsilent) {
    if (this->getContext ().getApp ().getContext ().settings.audio.enabled) {
	this->load ();
    }
}

CSound::~CSound () {
    // free all the sound buffers and streams
    for (const auto& stream : this->m_audioStreams) {
	this->getScene ().getAudioContext ().removeStream (stream.first);
	delete stream.second;
    }

    this->m_audioStreams.clear ();
}

void CSound::load () {
    for (const auto& cur : this->m_sound.sounds) {
	auto stream
	    = new Audio::AudioStream (this->getScene ().getAudioContext (), this->getAssetLocator ().read (cur));

	stream->setRepeat (this->m_sound.playbackmode.has_value () && this->m_sound.playbackmode == "loop");
	stream->setVolume (this->m_sound.volume);
	// "random" is a playlist: render() starts one track at a time (all at once drowned each other out)
	stream->setPaused (this->m_sound.startsilent || this->isRandom ());
	this->m_tracks.push_back (stream);

	// add the stream to the context so it can be played
	this->m_audioStreams.insert_or_assign (this->getScene ().getAudioContext ().addStream (stream), stream);
    }
}

bool CSound::isRandom () const {
    return this->m_sound.playbackmode.has_value () && this->m_sound.playbackmode == "random";
}

void CSound::render () {
    if (!this->isRandom () || !this->m_playing || this->m_tracks.empty ()) {
	return;
    }

    static thread_local std::mt19937 rng { std::random_device {}() };
    const auto now = std::chrono::steady_clock::now ();

    if (this->m_current >= 0) {
	if (this->m_tracks [this->m_current]->isPlaying ()) {
	    return;
	}
	// track ended: silence for [mintime, maxtime] seconds, like Wallpaper Engine
	const float lo = std::max (0.0f, this->m_sound.mintime);
	const float hi = std::max (lo, this->m_sound.maxtime);
	const auto pause = std::uniform_real_distribution<float> (lo, hi) (rng);
	this->m_nextTrack = now + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
				      std::chrono::duration<float> (pause));
	this->m_tracks [this->m_current]->setPaused (true);
	this->m_last = this->m_current;
	this->m_current = -1;
    }

    if (now < this->m_nextTrack) {
	return;
    }

    // a different track than the last one whenever there is a choice
    const int count = static_cast<int> (this->m_tracks.size ());
    int next = std::uniform_int_distribution<int> (0, count - 1) (rng);
    if (count > 1 && next == this->m_last) {
	next = (next + 1) % count;
    }
    this->m_current = next;
    this->m_tracks [next]->play ();
}

void CSound::play () {
    this->m_playing = true;
    if (this->isRandom ()) {
	// resume the current track; render() picks one if none is playing
	if (this->m_current >= 0) {
	    this->m_tracks [this->m_current]->play ();
	}
	return;
    }
    for (auto* stream : this->m_audioStreams | std::views::values) {
	stream->play ();
    }
}

void CSound::pause () {
    this->m_playing = false;
    for (auto* stream : this->m_audioStreams | std::views::values) {
	stream->setPaused (true);
    }
}

void CSound::stop () { this->pause (); }

void CSound::setVolume (float volume) {
    for (auto* stream : this->m_audioStreams | std::views::values) {
	stream->setVolume (volume);
    }
}

bool CSound::isPlaying () const {
    if (this->m_audioStreams.empty ()) {
	return this->m_playing;
    }
    return std::ranges::any_of (this->m_audioStreams | std::views::values, [] (const auto* stream) {
	return stream->isPlaying ();
    });
}
