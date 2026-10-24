#pragma once

#include "WallpaperEngine/Audio/AudioStream.h"
#include "WallpaperEngine/Render/CObject.h"

#include <chrono>
#include <vector>

using namespace WallpaperEngine;

namespace WallpaperEngine::Render::Wallpapers {
class CScene;
}

namespace WallpaperEngine::Render::Objects {
using namespace WallpaperEngine::Data::Model;

class CSound final : public CObject {
public:
    CSound (Wallpapers::CScene& scene, const Sound& sound);
    ~CSound () override;

    void render () override;

    /** ISoundLayer for scripts. Without audio (--silent) only the play state is kept. */
    void play ();
    void pause ();
    void stop ();
    void setVolume (float volume);
    [[nodiscard]] bool isPlaying () const;

protected:
    void load ();

private:
    std::map<int, Audio::AudioStream*> m_audioStreams = {};
    /** streams in the layer's order; "random" plays one of them at a time */
    std::vector<Audio::AudioStream*> m_tracks = {};
    int m_current = -1;
    int m_last = -1;
    std::chrono::steady_clock::time_point m_nextTrack {};

    [[nodiscard]] bool isRandom () const;

    const Sound& m_sound;
    bool m_playing = false;
};
} // namespace WallpaperEngine::Render::Objects
