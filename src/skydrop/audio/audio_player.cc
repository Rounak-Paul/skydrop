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
// stb_image for JPEG/PNG art blobs — implementation is in tinyvk, we just need the declarations
#include <stb_image.h>

// Enable prototype declarations for all OpenAL extension functions
// (EFX, HRTF, etc.)  — must be defined before any AL header
#define AL_ALEXT_PROTOTYPES

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <AL/efx.h>
#include <AL/efx-presets.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <numeric>
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

// Album art — RGBA8, decoded by the audio thread, read by main thread
static std::vector<uint8_t> s_ArtPixels;
static int                  s_ArtWidth  = 0;
static int                  s_ArtHeight = 0;

// ---- Waveform state -------------------------------------------------------
static constexpr int kWaveformBins = 1024;
static std::vector<float> s_Waveform; // normalized RMS peaks [0,1]

// ---- Spatial / HRTF state ------------------------------------------------
static bool s_HasHRTF     = false;
static bool s_HasEFX      = false;
static ALuint s_EffectSlot = 0;
static ALuint s_Effect     = 0;
static AudioPlayer::SpatialPreset s_SpatialPreset  = AudioPlayer::SpatialPreset::Off;
static float                      s_SpatialAzimuth = 0.0f;

// ---- EQ state -------------------------------------------------------------
static ALuint s_EQSlot           = 0;    // auxiliary effect slot for equalizer
static ALuint s_EQEffect         = 0;    // AL_EFFECT_EQUALIZER object
static ALuint s_DirectMuteFilter = 0;    // low-pass filter with gain=0 to mute direct path
static float  s_BassGain         = 1.0f; // linear gain for low band  [0.126, 7.943]
static float  s_MidGain          = 1.0f; // linear gain for mid bands [0.126, 7.943]
static float  s_TrebleGain       = 1.0f; // linear gain for high band [0.126, 7.943]

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

// ---- Waveform helper (called from loader thread, no lock needed) ----------

static std::vector<float> ComputeWaveform(const std::vector<int16_t>& pcm, uint32_t channels) {
    std::vector<float> peaks(kWaveformBins, 0.0f);
    const size_t totalFrames = pcm.size() / channels;
    if (totalFrames == 0) return peaks;

    const size_t framesPerBin = std::max<size_t>(1, totalFrames / kWaveformBins);
    for (int bin = 0; bin < kWaveformBins; ++bin) {
        const size_t start = static_cast<size_t>(bin) * framesPerBin;
        const size_t end   = std::min(start + framesPerBin, totalFrames);
        float sumSq = 0.0f;
        for (size_t f = start; f < end; ++f) {
            float mono = 0.0f;
            for (uint32_t c = 0; c < channels; ++c)
                mono += pcm[f * channels + c] * (1.0f / 32768.0f);
            mono /= static_cast<float>(channels);
            sumSq += mono * mono;
        }
        peaks[bin] = std::sqrt(sumSq / static_cast<float>(end - start));
    }
    const float maxPeak = *std::max_element(peaks.begin(), peaks.end());
    if (maxPeak > 1e-6f)
        for (auto& p : peaks) p /= maxPeak;
    return peaks;
}

// ---- HRTF / EFX helpers (must be called while ALContext is current) ------

static void ProbeExtensions() {
    s_HasHRTF = (alcIsExtensionPresent(s_Device, "ALC_SOFT_HRTF") == ALC_TRUE);
    s_HasEFX  = (alcIsExtensionPresent(s_Device, "ALC_EXT_EFX")   == ALC_TRUE);
    if (s_HasEFX) {
        // Slot 0: reverb (used only when spatial is on)
        alGenAuxiliaryEffectSlots(1, &s_EffectSlot);
        alGenEffects(1, &s_Effect);

        // Slot 1: equalizer — always active, chains into reverb when spatial on
        alGenAuxiliaryEffectSlots(1, &s_EQSlot);
        alGenEffects(1, &s_EQEffect);
        alEffecti(s_EQEffect, AL_EFFECT_TYPE, AL_EFFECT_EQUALIZER);
        alAuxiliaryEffectSloti(s_EQSlot, AL_EFFECTSLOT_EFFECT,
                               static_cast<ALint>(s_EQEffect));

        // Zero-gain lowpass to mute the source's direct (dry) path;
        // all audio is routed through the EQ slot instead.
        alGenFilters(1, &s_DirectMuteFilter);
        alFilteri(s_DirectMuteFilter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
        alFilterf(s_DirectMuteFilter, AL_LOWPASS_GAIN,   0.0f);
        alFilterf(s_DirectMuteFilter, AL_LOWPASS_GAINHF, 0.0f);
    }
}

// Apply current EQ band gains and make the source route through the EQ slot.
// Must be called while s_StateMutex is held and the AL context is current.
static void ApplyEQState() {
    if (!s_HasEFX || !s_EQSlot || !s_EQEffect || !s_Source) return;

    // Mute the direct (dry) path — all audio goes out through aux sends.
    if (s_DirectMuteFilter)
        alSourcei(s_Source, AL_DIRECT_FILTER,
                  static_cast<ALint>(s_DirectMuteFilter));

    // Push updated band gains into the EQ effect.
    alEffectf(s_EQEffect, AL_EQUALIZER_LOW_GAIN,  s_BassGain);
    alEffectf(s_EQEffect, AL_EQUALIZER_MID1_GAIN, s_MidGain);
    alEffectf(s_EQEffect, AL_EQUALIZER_MID2_GAIN, s_MidGain);
    alEffectf(s_EQEffect, AL_EQUALIZER_HIGH_GAIN, s_TrebleGain);
    alAuxiliaryEffectSloti(s_EQSlot, AL_EFFECTSLOT_EFFECT,
                           static_cast<ALint>(s_EQEffect));

    // Source always sends to the EQ slot on send index 0.
    alSource3i(s_Source, AL_AUXILIARY_SEND_FILTER,
               static_cast<ALint>(s_EQSlot), 0, AL_FILTER_NULL);
}

// Apply the current s_SpatialPreset + s_SpatialAzimuth to the OpenAL source.
// Must be called under s_StateMutex.
static void ApplySpatialState() {
    if (!s_Source) return;

    const bool spatial = (s_SpatialPreset != AudioPlayer::SpatialPreset::Off);

    // 1) HRTF toggle via ALC_SOFT_HRTF device reset
    if (s_HasHRTF) {
        const ALCint hrtfAttrs[] = {
            ALC_HRTF_SOFT, spatial ? ALC_TRUE : ALC_FALSE,
            0
        };
        alcResetDeviceSOFT(s_Device, hrtfAttrs);
    }

    if (!spatial) {
        // Non-spatial: relative source, no reverb
        alSourcei(s_Source, AL_SOURCE_RELATIVE, AL_TRUE);
        alSource3f(s_Source, AL_POSITION, 0.f, 0.f, 0.f);
        if (s_HasEFX && s_EffectSlot) {
            alAuxiliaryEffectSloti(s_EffectSlot, AL_EFFECTSLOT_EFFECT,
                                   AL_EFFECT_NULL);
            // Unchain EQ → reverb
            if (s_EQSlot && alIsExtensionPresent("AL_SOFT_effect_target"))
                alAuxiliaryEffectSloti(s_EQSlot, AL_EFFECTSLOT_TARGET_SOFT, 0);
        }
        ApplyEQState(); // reapply EQ send after any state reset
        return;
    }

    // 2) Position source in 3D from azimuth (listener at origin, facing -Z)
    alSourcei(s_Source, AL_SOURCE_RELATIVE, AL_FALSE);
    const float radAz = s_SpatialAzimuth * (3.14159265358979f / 180.0f);
    alSource3f(s_Source, AL_POSITION,
               std::sin(radAz) * 1.5f,
               0.f,
               -std::cos(radAz) * 1.5f);

    // Listener at origin, looking -Z, up +Y (explicit for safety)
    const ALfloat ori[6] = { 0.f, 0.f, -1.f,   0.f, 1.f, 0.f };
    alListenerfv(AL_ORIENTATION, ori);
    alListener3f(AL_POSITION, 0.f, 0.f, 0.f);

    // 3) EFX reverb preset
    if (!s_HasEFX || !s_EffectSlot || !s_Effect) return;

    EFXEAXREVERBPROPERTIES rev;
    switch (s_SpatialPreset) {
        case AudioPlayer::SpatialPreset::Room:
            rev = EFXEAXREVERBPROPERTIES EFX_REVERB_PRESET_ROOM;
            break;
        case AudioPlayer::SpatialPreset::ConcertHall:
            rev = EFXEAXREVERBPROPERTIES EFX_REVERB_PRESET_CONCERTHALL;
            break;
        case AudioPlayer::SpatialPreset::OpenAir:
            rev = EFXEAXREVERBPROPERTIES EFX_REVERB_PRESET_OUTDOORS_ROLLINGPLAINS;
            break;
        default:
            return;
    }

    alEffecti(s_Effect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);
    if (alGetError() != AL_NO_ERROR) {
        // Fallback to standard reverb
        alEffecti(s_Effect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
        alEffectf(s_Effect, AL_REVERB_DENSITY,               rev.flDensity);
        alEffectf(s_Effect, AL_REVERB_DIFFUSION,             rev.flDiffusion);
        alEffectf(s_Effect, AL_REVERB_GAIN,                  rev.flGain);
        alEffectf(s_Effect, AL_REVERB_GAINHF,                rev.flGainHF);
        alEffectf(s_Effect, AL_REVERB_DECAY_TIME,            rev.flDecayTime);
        alEffectf(s_Effect, AL_REVERB_DECAY_HFRATIO,         rev.flDecayHFRatio);
        alEffectf(s_Effect, AL_REVERB_REFLECTIONS_GAIN,      rev.flReflectionsGain);
        alEffectf(s_Effect, AL_REVERB_REFLECTIONS_DELAY,     rev.flReflectionsDelay);
        alEffectf(s_Effect, AL_REVERB_LATE_REVERB_GAIN,      rev.flLateReverbGain);
        alEffectf(s_Effect, AL_REVERB_LATE_REVERB_DELAY,     rev.flLateReverbDelay);
        alEffectf(s_Effect, AL_REVERB_AIR_ABSORPTION_GAINHF, rev.flAirAbsorptionGainHF);
        alEffectf(s_Effect, AL_REVERB_ROOM_ROLLOFF_FACTOR,   rev.flRoomRolloffFactor);
        alEffecti(s_Effect, AL_REVERB_DECAY_HFLIMIT,         rev.iDecayHFLimit);
    } else {
        alEffectf(s_Effect, AL_EAXREVERB_DENSITY,               rev.flDensity);
        alEffectf(s_Effect, AL_EAXREVERB_DIFFUSION,             rev.flDiffusion);
        alEffectf(s_Effect, AL_EAXREVERB_GAIN,                  rev.flGain);
        alEffectf(s_Effect, AL_EAXREVERB_GAINHF,                rev.flGainHF);
        alEffectf(s_Effect, AL_EAXREVERB_GAINLF,                rev.flGainLF);
        alEffectf(s_Effect, AL_EAXREVERB_DECAY_TIME,            rev.flDecayTime);
        alEffectf(s_Effect, AL_EAXREVERB_DECAY_HFRATIO,         rev.flDecayHFRatio);
        alEffectf(s_Effect, AL_EAXREVERB_DECAY_LFRATIO,         rev.flDecayLFRatio);
        alEffectf(s_Effect, AL_EAXREVERB_REFLECTIONS_GAIN,      rev.flReflectionsGain);
        alEffectf(s_Effect, AL_EAXREVERB_REFLECTIONS_DELAY,     rev.flReflectionsDelay);
        alEffectfv(s_Effect, AL_EAXREVERB_REFLECTIONS_PAN,      rev.flReflectionsPan);
        alEffectf(s_Effect, AL_EAXREVERB_LATE_REVERB_GAIN,      rev.flLateReverbGain);
        alEffectf(s_Effect, AL_EAXREVERB_LATE_REVERB_DELAY,     rev.flLateReverbDelay);
        alEffectfv(s_Effect, AL_EAXREVERB_LATE_REVERB_PAN,      rev.flLateReverbPan);
        alEffectf(s_Effect, AL_EAXREVERB_ECHO_TIME,             rev.flEchoTime);
        alEffectf(s_Effect, AL_EAXREVERB_ECHO_DEPTH,            rev.flEchoDepth);
        alEffectf(s_Effect, AL_EAXREVERB_MODULATION_TIME,       rev.flModulationTime);
        alEffectf(s_Effect, AL_EAXREVERB_MODULATION_DEPTH,      rev.flModulationDepth);
        alEffectf(s_Effect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, rev.flAirAbsorptionGainHF);
        alEffectf(s_Effect, AL_EAXREVERB_HFREFERENCE,           rev.flHFReference);
        alEffectf(s_Effect, AL_EAXREVERB_LFREFERENCE,           rev.flLFReference);
        alEffectf(s_Effect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR,   rev.flRoomRolloffFactor);
        alEffecti(s_Effect, AL_EAXREVERB_DECAY_HFLIMIT,         rev.iDecayHFLimit);
    }

    alAuxiliaryEffectSloti(s_EffectSlot, AL_EFFECTSLOT_EFFECT,
                           static_cast<ALint>(s_Effect));

    // Chain EQ slot → reverb slot so the EQ'd signal gets spatial reverb.
    // The source itself only sends to the EQ slot (via ApplyEQState below).
    if (s_EQSlot && alIsExtensionPresent("AL_SOFT_effect_target")) {
        alAuxiliaryEffectSloti(s_EQSlot, AL_EFFECTSLOT_TARGET_SOFT,
                               static_cast<ALint>(s_EffectSlot));
    } else {
        // Fallback: direct source send to reverb slot on send index 1
        alSource3i(s_Source, AL_AUXILIARY_SEND_FILTER,
                   static_cast<ALint>(s_EffectSlot), 1, AL_FILTER_NULL);
    }
    ApplyEQState(); // reapply EQ send after HRTF/spatial state changes
}

// ---- Art helpers ----------------------------------------------------------

// Decode a JPEG/PNG blob into RGBA8 pixels.
static bool DecodeArtBlob(const uint8_t* data, int size,
                          std::vector<uint8_t>& pixels, int& w, int& h) {
    int comp = 0;
    stbi_uc* raw = stbi_load_from_memory(data, size, &w, &h, &comp, 4);
    if (!raw || w <= 0 || h <= 0) return false;
    pixels.assign(raw, raw + (size_t)w * h * 4);
    stbi_image_free(raw);
    return true;
}

// Decode a big-endian uint32 from a byte buffer.
static uint32_t ReadBE32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

// Minimal base64 decoder (RFC 4648, no line-wrapping).
static std::vector<uint8_t> Base64Decode(const char* src, size_t srcLen) {
    static const int8_t T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    };
    std::vector<uint8_t> out;
    out.reserve(srcLen / 4 * 3);
    uint32_t accum = 0;
    int bits = 0;
    for (size_t i = 0; i < srcLen; ++i) {
        int8_t v = T[(uint8_t)src[i]];
        if (v < 0) continue;
        accum = (accum << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)(accum >> bits));
        }
    }
    return out;
}

// Parse a FLAC PICTURE block structure (used both in raw FLAC and in OGG
// vorbis METADATA_BLOCK_PICTURE comments) and decode the embedded image.
static bool ParseFlacPictureBlock(const uint8_t* data, size_t size,
                                   std::vector<uint8_t>& pixels, int& w, int& h) {
    if (size < 32) return false;
    const uint8_t* p   = data;
    const uint8_t* end = data + size;

    auto readU32 = [&]() -> uint32_t {
        if (p + 4 > end) return 0;
        uint32_t v = ReadBE32(p);
        p += 4;
        return v;
    };

    /* uint32_t pictureType = */ readU32();
    uint32_t mimeLen = readU32();
    if (p + mimeLen > end) return false;
    p += mimeLen;                              // skip MIME string
    uint32_t descLen = readU32();
    if (p + descLen > end) return false;
    p += descLen;                              // skip description
    p += 16;                                   // skip width, height, colorDepth, indexColorCount
    uint32_t imgLen = readU32();
    if (!imgLen || p + imgLen > end) return false;

    return DecodeArtBlob(p, (int)imgLen, pixels, w, h);
}

// Parse ID3v2 tag at the start of an MP3 file; extract APIC art blob.
static bool TryExtractMp3Art(const std::string& path,
                              std::vector<uint8_t>& pixels, int& w, int& h) {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) return false;

    uint8_t hdr[10];
    if (std::fread(hdr, 1, 10, fp) != 10 ||
        hdr[0] != 'I' || hdr[1] != 'D' || hdr[2] != '3') {
        std::fclose(fp);
        return false;
    }

    const int id3Version   = hdr[3];
    // Syncsafe size (ID3v2.3+ tag body length, not including the 10-byte header)
    const uint32_t tagSize = ((hdr[6] & 0x7fu) << 21) | ((hdr[7] & 0x7fu) << 14) |
                             ((hdr[8] & 0x7fu) <<  7) |  (hdr[9] & 0x7fu);

    bool found = false;
    uint32_t consumed = 0;

    while (consumed < tagSize) {
        if (id3Version == 2) break; // ID3v2.2 uses 3-char IDs — skip for now

        uint8_t fhdr[10]; // 4-byte ID + 4-byte size + 2-byte flags
        if (std::fread(fhdr, 1, 10, fp) != 10) break;
        consumed += 10;

        if (fhdr[0] == 0) break; // padding reached

        uint32_t frameSize;
        if (id3Version >= 4) {
            frameSize = ((fhdr[4] & 0x7fu) << 21) | ((fhdr[5] & 0x7fu) << 14) |
                        ((fhdr[6] & 0x7fu) <<  7) |  (fhdr[7] & 0x7fu);
        } else {
            frameSize = ReadBE32(fhdr + 4);
        }

        if (frameSize == 0 || consumed + frameSize > tagSize) break;

        if (fhdr[0] == 'A' && fhdr[1] == 'P' && fhdr[2] == 'I' && fhdr[3] == 'C') {
            std::vector<uint8_t> apic(frameSize);
            if (std::fread(apic.data(), 1, frameSize, fp) == frameSize) {
                // APIC: [encoding(1)] [mime\0] [type(1)] [desc\0] [data]
                size_t pos = 1; // skip text encoding
                while (pos < frameSize && apic[pos] != 0) ++pos;
                ++pos; // skip NUL after MIME
                ++pos; // skip picture type byte
                // skip description (NUL-terminated in its encoding)
                // encoding byte was in apic[0]: 0=latin-1, 1=utf16 (double-NUL), 3=utf8
                uint8_t enc = apic[0];
                if (enc == 1 || enc == 2) {
                    // UTF-16: double-NUL terminator
                    while (pos + 1 < frameSize &&
                           !(apic[pos] == 0 && apic[pos + 1] == 0)) {
                        pos += 2;
                    }
                    pos += 2;
                } else {
                    while (pos < frameSize && apic[pos] != 0) ++pos;
                    ++pos;
                }
                if (pos < frameSize)
                    found = DecodeArtBlob(apic.data() + pos, (int)(frameSize - pos), pixels, w, h);
            }
            break; // first APIC is enough
        } else {
            std::fseek(fp, (long)frameSize, SEEK_CUR);
        }
        consumed += frameSize;
    }
    std::fclose(fp);
    return found;
}

// Extract art from OGG vorbis METADATA_BLOCK_PICTURE comment.
static bool TryExtractOggArt(const stb_vorbis_comment& cmt,
                              std::vector<uint8_t>& pixels, int& w, int& h) {
    static const char kPrefix[] = "METADATA_BLOCK_PICTURE=";
    const int prefixLen = (int)sizeof(kPrefix) - 1;

    for (int i = 0; i < cmt.comment_list_length; ++i) {
        const char* entry = cmt.comment_list[i];
        int entryLen = (int)std::strlen(entry);

        // Case-insensitive prefix match
        if (entryLen <= prefixLen) continue;
        bool match = true;
        for (int j = 0; j < prefixLen; ++j) {
            if (std::tolower((uint8_t)entry[j]) != std::tolower((uint8_t)kPrefix[j])) {
                match = false; break;
            }
        }
        if (!match) continue;

        const char* b64 = entry + prefixLen;
        int b64Len = entryLen - prefixLen;
        auto blob = Base64Decode(b64, (size_t)b64Len);
        if (blob.empty()) continue;

        if (ParseFlacPictureBlock(blob.data(), blob.size(), pixels, w, h))
            return true;
    }
    return false;
}

// ---- Decoding (runs on audio thread) --------------------------------------

static bool DecodeFile(
    const std::string& path,
    std::vector<int16_t>& pcm,
    uint32_t& sampleRate,
    uint32_t& channels,
    std::string& title,
    std::string& artist,
    std::string& album,
    std::vector<uint8_t>& artPixels,
    int& artW,
    int& artH)
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
        // ID3v2 cover art + ID3v1 text tags
        TryExtractMp3Art(path, artPixels, artW, artH);
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
        struct FlacMeta {
            std::string title, artist, album;
            std::vector<uint8_t> artBlob; // raw JPEG/PNG from PICTURE block
        };
        FlacMeta meta;

        drflac* flac = drflac_open_file_with_metadata(
            path.c_str(),
            [](void* pUserData, drflac_metadata* pMeta) {
                auto* m = static_cast<FlacMeta*>(pUserData);
                if (pMeta->type == DRFLAC_METADATA_BLOCK_TYPE_VORBIS_COMMENT) {
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
                } else if (pMeta->type == DRFLAC_METADATA_BLOCK_TYPE_PICTURE) {
                    // Prefer picture type 3 (cover art front); accept any if none found yet
                    if (m->artBlob.empty() || pMeta->data.picture.type == 3) {
                        if (pMeta->data.picture.pPictureData && pMeta->data.picture.pictureDataSize > 0) {
                            m->artBlob.assign(
                                pMeta->data.picture.pPictureData,
                                pMeta->data.picture.pPictureData + pMeta->data.picture.pictureDataSize);
                        }
                    }
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
        if (!meta.artBlob.empty())
            DecodeArtBlob(meta.artBlob.data(), (int)meta.artBlob.size(), artPixels, artW, artH);

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
            TryExtractOggArt(c, artPixels, artW, artH);
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
        std::vector<uint8_t> artPixels;
        int artW = 0, artH = 0;

        bool ok = DecodeFile(path, pcm, sampleRate, channels, title, artist, album, artPixels, artW, artH);

        // Discard if a newer Load() arrived while we were decoding
        if (s_LoadGeneration.load(std::memory_order_acquire) != myGeneration) continue;
        if (!ok) continue;

        const float duration = static_cast<float>(pcm.size() / channels) / static_cast<float>(sampleRate);
        const bool  hasArt   = !artPixels.empty();

        // --- Compute waveform from raw PCM (no lock, heavy but fast) ---
        std::vector<float> waveform = ComputeWaveform(pcm, channels);

        // --- Upload to OpenAL + store state (must hold AL context) ---
        {
            std::lock_guard<std::mutex> lk(s_StateMutex);
            ClearSourceBuffer();

            ALenum fmt = (channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
            alGenBuffers(1, &s_Buffer);
            alBufferData(s_Buffer, fmt,
                         pcm.data(),
                         static_cast<ALsizei>(pcm.size() * sizeof(int16_t)),
                         static_cast<ALsizei>(sampleRate));
            alSourcei(s_Source, AL_BUFFER, static_cast<ALint>(s_Buffer));
            alSourcePlay(s_Source);

            s_Title    = title;
            s_Artist   = artist;
            s_Album    = album;
            s_Duration = duration;
            s_WasPlaying = true;

            s_ArtPixels = std::move(artPixels);
            s_ArtWidth  = hasArt ? artW : 0;
            s_ArtHeight = hasArt ? artH : 0;

            s_Waveform = std::move(waveform);

            // Re-apply spatial settings to the new source (position, reverb)
            ApplySpatialState();
        }

        // Notify UI (safe — Event::Emit is mutex-protected inside event.cc)
        Event::Emit(TrackChangedEvent{ title, artist, album, duration, hasArt });
        Event::Emit(WaveformReadyEvent{});
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
    alSourcei(s_Source, AL_SOURCE_RELATIVE, AL_TRUE); // default: non-spatial

    // Listener at origin, looking -Z, up +Y
    const ALfloat ori[6] = { 0.f, 0.f, -1.f,  0.f, 1.f, 0.f };
    alListenerfv(AL_ORIENTATION, ori);
    alListener3f(AL_POSITION, 0.f, 0.f, 0.f);

    ProbeExtensions();
    ApplyEQState(); // activate EQ slot routing from the start

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

    // Cleanup EFX objects
    if (s_HasEFX) {
        if (s_EffectSlot)        { alDeleteAuxiliaryEffectSlots(1, &s_EffectSlot);    s_EffectSlot        = 0; }
        if (s_Effect)            { alDeleteEffects(1, &s_Effect);                     s_Effect            = 0; }
        if (s_EQSlot)            { alDeleteAuxiliaryEffectSlots(1, &s_EQSlot);        s_EQSlot            = 0; }
        if (s_EQEffect)          { alDeleteEffects(1, &s_EQEffect);                   s_EQEffect          = 0; }
        if (s_DirectMuteFilter)  { alDeleteFilters(1, &s_DirectMuteFilter);           s_DirectMuteFilter  = 0; }
    }

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

bool AudioPlayer::CopyAlbumArt(std::vector<uint8_t>& pixels, int& width, int& height) {
    std::lock_guard<std::mutex> lk(s_StateMutex);
    if (s_ArtPixels.empty()) return false;
    pixels = s_ArtPixels;   // copy while lock is held — safe
    width  = s_ArtWidth;
    height = s_ArtHeight;
    return true;
}

std::string AudioPlayer::GetTitle()  { std::lock_guard<std::mutex> lk(s_StateMutex); return s_Title;  }
std::string AudioPlayer::GetArtist() { std::lock_guard<std::mutex> lk(s_StateMutex); return s_Artist; }
std::string AudioPlayer::GetAlbum()  { std::lock_guard<std::mutex> lk(s_StateMutex); return s_Album;  }

// ---- Waveform -------------------------------------------------------------

bool AudioPlayer::CopyWaveform(std::vector<float>& peaks) {
    std::lock_guard<std::mutex> lk(s_StateMutex);
    if (s_Waveform.empty()) return false;
    peaks = s_Waveform;
    return true;
}

// ---- Spatial / HRTF -------------------------------------------------------

void AudioPlayer::SetSpatialPreset(SpatialPreset p) {
    std::lock_guard<std::mutex> lk(s_StateMutex);
    s_SpatialPreset = p;
    ApplySpatialState();
}

AudioPlayer::SpatialPreset AudioPlayer::GetSpatialPreset() {
    std::lock_guard<std::mutex> lk(s_StateMutex);
    return s_SpatialPreset;
}

void AudioPlayer::SetSpatialAzimuth(float degrees) {
    std::lock_guard<std::mutex> lk(s_StateMutex);
    // Clamp to [-180, 180]
    while (degrees >  180.0f) degrees -= 360.0f;
    while (degrees < -180.0f) degrees += 360.0f;
    s_SpatialAzimuth = degrees;
    if (s_SpatialPreset != SpatialPreset::Off && s_Source) {
        const float radAz = degrees * (3.14159265358979f / 180.0f);
        alSource3f(s_Source, AL_POSITION,
                   std::sin(radAz) * 1.5f,
                   0.f,
                   -std::cos(radAz) * 1.5f);
    }
}

float AudioPlayer::GetSpatialAzimuth() {
    std::lock_guard<std::mutex> lk(s_StateMutex);
    return s_SpatialAzimuth;
}

// ---- Equalizer ------------------------------------------------------------

void AudioPlayer::SetEQBands(float bassGain, float midGain, float trebleGain) {
    std::lock_guard<std::mutex> lk(s_StateMutex);
    s_BassGain   = std::clamp(bassGain,   AL_EQUALIZER_MIN_LOW_GAIN,  AL_EQUALIZER_MAX_LOW_GAIN);
    s_MidGain    = std::clamp(midGain,    AL_EQUALIZER_MIN_MID1_GAIN, AL_EQUALIZER_MAX_MID1_GAIN);
    s_TrebleGain = std::clamp(trebleGain, AL_EQUALIZER_MIN_HIGH_GAIN, AL_EQUALIZER_MAX_HIGH_GAIN);
    ApplyEQState();
}

void AudioPlayer::GetEQBands(float& bassGain, float& midGain, float& trebleGain) {
    std::lock_guard<std::mutex> lk(s_StateMutex);
    bassGain   = s_BassGain;
    midGain    = s_MidGain;
    trebleGain = s_TrebleGain;
}
