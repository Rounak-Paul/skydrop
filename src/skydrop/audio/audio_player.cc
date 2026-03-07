#include "audio_player.h"
#include "ui/music_events.h"
#include "event.h"

// Single-header audio decoders (each compiled once here)
#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>
#define DR_MP3_IMPLEMENTATION
#include <dr_mp3.h>
#define DR_FLAC_IMPLEMENTATION
#include <dr_flac.h>
// stb_vorbis for OGG (vendored via tinyvk)
#include <stb_vorbis.c>

#include <AL/al.h>
#include <AL/alc.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ---- Internal state -------------------------------------------------------

static ALCdevice*  s_Device     = nullptr;
static ALCcontext* s_Context    = nullptr;
static ALuint      s_Source     = 0;
static ALuint      s_Buffer     = 0;

// Playback state — written by audio thread, read by main thread
static std::mutex  s_StateMutex;
static float       s_Duration   = 0.0f;
static bool        s_WasPlaying = false;

static std::string s_Title;
static std::string s_Artist;
static std::string s_Album;

// ---- Audio loader thread --------------------------------------------------

static std::thread              s_LoadThread;
static std::mutex               s_QueueMutex;
static std::condition_variable  s_QueueCV;
static std::string              s_PendingPath;   // "" = no pending work
static bool                     s_StopThread = false;
// Increments each time Load() is called so in-flight stale jobs are discarded.
static std::atomic<uint64_t>    s_LoadGeneration{ 0 };

// ---- Helpers ---------------------------------------------------------------

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return s;
}

static void ClearSourceBuffer() {
    alSourceStop(s_Source);
    alSourcei(s_Source, AL_BUFFER, 0);
    if (s_Buffer) { alDeleteBuffers(1, &s_Buffer); s_Buffer = 0; }
}

// ---- Decoding (runs on audio thread) --------------------------------------

static bool DecodeFile(
    const std::string& path,
    std::vector<int16_t>& pcm,
    uint32_t& sampleRate,
    uint32_t& channels,
    std::string& title,
    std::string& artist,
    std::string& album)
{
    const std::string ext = ToLower(std::filesystem::path(path).extension().string());

    // ---- WAV ---------------------------------------------------------------
    if (ext == ".wav" || ext == ".wave") {
        drwav wav;
        if (!drwav_init_file(&wav, path.c_str(), nullptr)) return false;
        pcm.resize(static_cast<size_t>(wav.totalPCMFrameCount) * wav.channels);
        drwav_uint64 read = drwav_read_pcm_frames_s16(&wav, wav.totalPCMFrameCount, pcm.data());
        pcm.resize(static_cast<size_t>(read) * wav.channels);
        sampleRate = wav.sampleRate;
        channels   = wav.channels;
        for (drwav_uint32 i = 0; i < wav.metadataCount; ++i) {
            const drwav_metadata& m = wav.pMetadata[i];
            if (m.type == drwav_metadata_type_list_info_title  && m.data.infoText.pString)
                title  = std::string(m.data.infoText.pString, m.data.infoText.stringLength);
            else if (m.type == drwav_metadata_type_list_info_artist && m.data.infoText.pString)
                artist = std::string(m.data.infoText.pString, m.data.infoText.stringLength);
            else if (m.type == drwav_metadata_type_list_info_album  && m.data.infoText.pString)
                album  = std::string(m.data.infoText.pString, m.data.infoText.stringLength);
        }
        drwav_uninit(&wav);

    // ---- MP3 ---------------------------------------------------------------
    } else if (ext == ".mp3") {
        drmp3_config cfg = {};
        drmp3_uint64 frames = 0;
        int16_t* raw = drmp3_open_file_and_read_pcm_frames_s16(path.c_str(), &cfg, &frames, nullptr);
        if (!raw || frames == 0) return false;
        pcm.assign(raw, raw + static_cast<size_t>(frames) * cfg.channels);
        drmp3_free(raw, nullptr);
        sampleRate = cfg.sampleRate;
        channels   = cfg.channels;
        // ID3v1 fallback (last 128 bytes of file)
        if (FILE* fp = std::fopen(path.c_str(), "rb")) {
            if (std::fseek(fp, -128, SEEK_END) == 0) {
                char tag[128];
                if (std::fread(tag, 1, 128, fp) == 128 &&
                    tag[0] == 'T' && tag[1] == 'A' && tag[2] == 'G') {
                    auto trim = [](const char* s, int n) {
                        std::string r(s, n);
                        size_t e = r.find_last_not_of(std::string("\x00 ", 2));
                        return (e != std::string::npos) ? r.substr(0, e + 1) : std::string{};
                    };
                    if (auto t = trim(tag + 3,  30); !t.empty()) title  = t;
                    if (auto a = trim(tag + 33, 30); !a.empty()) artist = a;
                    if (auto b = trim(tag + 63, 30); !b.empty()) album  = b;
                }
            }
            std::fclose(fp);
        }

    // ---- FLAC --------------------------------------------------------------
    } else if (ext == ".flac") {
        struct FlacMeta { std::string title, artist, album; };
        FlacMeta meta;

        drflac* flac = drflac_open_file_with_metadata(
            path.c_str(),
            [](void* pUserData, drflac_metadata* pMeta) {
                if (pMeta->type != DRFLAC_METADATA_BLOCK_TYPE_VORBIS_COMMENT) return;
                auto* m = static_cast<FlacMeta*>(pUserData);
                drflac_vorbis_comment_iterator it;
                drflac_init_vorbis_comment_iterator(
                    &it,
                    pMeta->data.vorbis_comment.commentCount,
                    pMeta->data.vorbis_comment.pComments);
                drflac_uint32 len = 0;
                const char* comment;
                while ((comment = drflac_next_vorbis_comment(&it, &len)) != nullptr) {
                    std::string entry(comment, len);
                    auto eq = entry.find('=');
                    if (eq == std::string::npos) continue;
                    std::string key = entry.substr(0, eq);
                    std::transform(key.begin(), key.end(), key.begin(),
                        [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
                    std::string val = entry.substr(eq + 1);
                    if      (key == "title")  m->title  = val;
                    else if (key == "artist") m->artist = val;
                    else if (key == "album")  m->album  = val;
                }
            },
            &meta, nullptr);

        if (!flac) return false;
        pcm.resize(static_cast<size_t>(flac->totalPCMFrameCount) * flac->channels);
        drflac_uint64 read = drflac_read_pcm_frames_s16(flac, flac->totalPCMFrameCount, pcm.data());
        pcm.resize(static_cast<size_t>(read) * flac->channels);
        sampleRate = flac->sampleRate;
        channels   = flac->channels;
        drflac_close(flac);
        if (!meta.title.empty())  title  = meta.title;
        if (!meta.artist.empty()) artist = meta.artist;
        if (!meta.album.empty())  album  = meta.album;

    // ---- OGG ---------------------------------------------------------------
    } else if (ext == ".ogg") {
        int ch = 0, sr = 0;
        short* raw = nullptr;
        int frames = stb_vorbis_decode_filename(path.c_str(), &ch, &sr, &raw);
        if (frames <= 0 || !raw) return false;
        pcm.assign(raw, raw + static_cast<size_t>(frames) * ch);
        free(raw);
        sampleRate = static_cast<uint32_t>(sr);
        channels   = static_cast<uint32_t>(ch);
        stb_vorbis* v = stb_vorbis_open_filename(path.c_str(), nullptr, nullptr);
        if (v) {
            stb_vorbis_comment c = stb_vorbis_get_comment(v);
            for (int i = 0; i < c.comment_list_length; ++i) {
                std::string entry(c.comment_list[i]);
                auto eq = entry.find('=');
                if (eq == std::string::npos) continue;
                std::string key = ToLower(entry.substr(0, eq));
                std::string val = entry.substr(eq + 1);
                if      (key == "title")  title  = val;
                else if (key == "artist") artist = val;
                else if (key == "album")  album  = val;
            }
            stb_vorbis_close(v);
        }

    } else {
        return false;
    }

    return !pcm.empty();
}

// ---- Loader thread body ---------------------------------------------------

static void AudioLoaderThread() {
    // OpenAL contexts are per-thread on some platforms; on others the context
    // set by Init() is already current everywhere.  We just use the existing
    // context — alcMakeContextCurrent is already done on the main thread.

    while (true) {
        std::string path;
        uint64_t myGeneration;

        {
            std::unique_lock<std::mutex> lk(s_QueueMutex);
            s_QueueCV.wait(lk, [] { return s_StopThread || !s_PendingPath.empty(); });
            if (s_StopThread && s_PendingPath.empty()) return;

            path         = std::move(s_PendingPath);
            s_PendingPath.clear();
            myGeneration = s_LoadGeneration.load(std::memory_order_acquire);
        }

        // --- Decode (no locks held during the heavy work) ---
        std::vector<int16_t> pcm;
        uint32_t sampleRate = 0, channels = 0;
        std::string title  = std::filesystem::path(path).stem().string();
        std::string artist, album;

        bool ok = DecodeFile(path, pcm, sampleRate, channels, title, artist, album);

        // Discard if a newer Load() arrived while we were decoding
        if (s_LoadGeneration.load(std::memory_order_acquire) != myGeneration) continue;
        if (!ok) continue;

        // --- Upload to OpenAL (must hold AL context) ---
        {
            std::lock_guard<std::mutex> lk(s_StateMutex);
            ClearSourceBuffer();

            float duration = static_cast<float>(pcm.size() / channels) / static_cast<float>(sampleRate);

            ALenum fmt = (channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
            alGenBuffers(1, &s_Buffer);
            alBufferData(s_Buffer, fmt,
                         pcm.data(),
                         static_cast<ALsizei>(pcm.size() * sizeof(int16_t)),
                         static_cast<ALsizei>(sampleRate));
            alSourcei(s_Source, AL_BUFFER, static_cast<ALint>(s_Buffer));
            alSourcePlay(s_Source);

            s_Title      = title;
            s_Artist     = artist;
            s_Album      = album;
            s_Duration   = duration;
            s_WasPlaying = true;
        }

        // Notify UI (safe — Event::Emit is mutex-protected inside event.cc)
        Event::Emit(TrackChangedEvent{ title, artist, album,
            static_cast<float>(pcm.size() / channels) / static_cast<float>(sampleRate),
            /*hasAlbumArt=*/false });
    }
}

// ---- Init / Shutdown ------------------------------------------------------

int AudioPlayer::Init() {
    s_Device = alcOpenDevice(nullptr);
    if (!s_Device) return -1;
    s_Context = alcCreateContext(s_Device, nullptr);
    if (!s_Context) { alcCloseDevice(s_Device); s_Device = nullptr; return -1; }
    alcMakeContextCurrent(s_Context);
    alGenSources(1, &s_Source);
    alSourcef(s_Source, AL_GAIN,  1.0f);
    alSourcef(s_Source, AL_PITCH, 1.0f);
    alSource3f(s_Source, AL_POSITION, 0.f, 0.f, 0.f);
    alSource3f(s_Source, AL_VELOCITY, 0.f, 0.f, 0.f);
    alSourcei(s_Source, AL_LOOPING, AL_FALSE);

    s_StopThread = false;
    s_LoadThread = std::thread(AudioLoaderThread);
    return 0;
}

void AudioPlayer::Shutdown() {
    // Wake and join the loader thread first
    {
        std::lock_guard<std::mutex> lk(s_QueueMutex);
        s_StopThread = true;
        s_PendingPath.clear();
    }
    s_QueueCV.notify_one();
    if (s_LoadThread.joinable()) s_LoadThread.join();

    if (s_Source) { ClearSourceBuffer(); alDeleteSources(1, &s_Source); s_Source = 0; }
    if (s_Context) { alcMakeContextCurrent(nullptr); alcDestroyContext(s_Context); s_Context = nullptr; }
    if (s_Device)  { alcCloseDevice(s_Device); s_Device = nullptr; }
}

// ---- Load (non-blocking) --------------------------------------------------

void AudioPlayer::Load(const std::string& path) {
    // Bump generation so any in-flight decode for the previous track is discarded
    s_LoadGeneration.fetch_add(1, std::memory_order_release);

    // Stop current playback immediately so there's no overlap
    {
        std::lock_guard<std::mutex> lk(s_StateMutex);
        alSourceStop(s_Source);
        s_WasPlaying = false;
    }

    {
        std::lock_guard<std::mutex> lk(s_QueueMutex);
        s_PendingPath = path;
    }
    s_QueueCV.notify_one();
}

// ---- Transport ------------------------------------------------------------

void AudioPlayer::Play()   { std::lock_guard<std::mutex> lk(s_StateMutex); if (s_Source) { alSourcePlay(s_Source);  s_WasPlaying = true; } }
void AudioPlayer::Pause()  { std::lock_guard<std::mutex> lk(s_StateMutex); if (s_Source) alSourcePause(s_Source); }
void AudioPlayer::Resume() { std::lock_guard<std::mutex> lk(s_StateMutex); if (s_Source) { alSourcePlay(s_Source);  s_WasPlaying = true; } }
void AudioPlayer::Stop()   { std::lock_guard<std::mutex> lk(s_StateMutex); if (s_Source) { alSourceStop(s_Source); alSourceRewind(s_Source); } s_WasPlaying = false; }

void AudioPlayer::Seek(float seconds) {
    std::lock_guard<std::mutex> lk(s_StateMutex);
    if (!s_Source) return;
    ALint state; alGetSourcei(s_Source, AL_SOURCE_STATE, &state);
    alSourcef(s_Source, AL_SEC_OFFSET, seconds);
    if (state == AL_PLAYING) alSourcePlay(s_Source);
}

void AudioPlayer::SetVolume(float v) {
    std::lock_guard<std::mutex> lk(s_StateMutex);
    if (s_Source) alSourcef(s_Source, AL_GAIN, v < 0.f ? 0.f : (v > 1.f ? 1.f : v));
}

// ---- Queries --------------------------------------------------------------

float AudioPlayer::GetPosition() {
    std::lock_guard<std::mutex> lk(s_StateMutex);
    if (!s_Source) return 0.f;
    ALfloat p = 0.f; alGetSourcef(s_Source, AL_SEC_OFFSET, &p); return p;
}
float AudioPlayer::GetDuration() { std::lock_guard<std::mutex> lk(s_StateMutex); return s_Duration; }
bool  AudioPlayer::IsPlaying() {
    std::lock_guard<std::mutex> lk(s_StateMutex);
    if (!s_Source) return false;
    ALint st; alGetSourcei(s_Source, AL_SOURCE_STATE, &st); return st == AL_PLAYING;
}
bool  AudioPlayer::IsPaused() {
    std::lock_guard<std::mutex> lk(s_StateMutex);
    if (!s_Source) return false;
    ALint st; alGetSourcei(s_Source, AL_SOURCE_STATE, &st); return st == AL_PAUSED;
}

// ---- Update ---------------------------------------------------------------

void AudioPlayer::Update() {
    bool shouldEmitEnd = false;
    {
        std::lock_guard<std::mutex> lk(s_StateMutex);
        if (!s_Source || !s_WasPlaying) return;
        ALint state; alGetSourcei(s_Source, AL_SOURCE_STATE, &state);
        if (state == AL_STOPPED && s_Duration > 0.f) {
            s_WasPlaying = false;
            shouldEmitEnd = true;
        }
    }
    // Emit outside the lock to avoid deadlock with event listeners that call
    // back into AudioPlayer (e.g. PlayIndex → Load → alSourceStop).
    if (shouldEmitEnd) Event::Emit(TrackEndedEvent{});
}

// ---- Art / Metadata -------------------------------------------------------

const uint8_t* AudioPlayer::GetAlbumArtPixels() { return nullptr; }
int            AudioPlayer::GetAlbumArtWidth()   { return 0; }
int            AudioPlayer::GetAlbumArtHeight()  { return 0; }
bool           AudioPlayer::HasAlbumArt()        { return false; }

std::string AudioPlayer::GetTitle()  { std::lock_guard<std::mutex> lk(s_StateMutex); return s_Title;  }
std::string AudioPlayer::GetArtist() { std::lock_guard<std::mutex> lk(s_StateMutex); return s_Artist; }
std::string AudioPlayer::GetAlbum()  { std::lock_guard<std::mutex> lk(s_StateMutex); return s_Album;  }
