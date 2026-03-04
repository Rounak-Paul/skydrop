/// @file image_loader.cpp
/// @brief Implementation of ImageData.
/// Decoding priority: libheif (HEIC/HEIF) → stb_image (PNG/JPG/…) → macOS ImageIO (fallback).

// stb_image implementation is already provided by tinyvk (texture.cpp).
#include "stb_image.h"

#include "skydrop/image/image_loader.h"
#include "skydrop/dsp/fft.h"

#include <cstring>
#include <cmath>
#include <algorithm>

// ── Cross-platform HEIC/HEIF via libheif ─────────────────────────────────────
#ifdef SKYDROP_HAS_LIBHEIF
#include <libheif/heif.h>

namespace {

/// Decode a HEIC/HEIF file using libheif.
/// Returns RGBA pixels (caller must free()), or nullptr on failure.
static uint8_t* LoadWithLibheif(const char* path, int* outW, int* outH) {
    heif_context* ctx = heif_context_alloc();
    if (!ctx) return nullptr;

    heif_error err = heif_context_read_from_file(ctx, path, nullptr);
    if (err.code != heif_error_Ok) {
        heif_context_free(ctx);
        return nullptr;
    }

    heif_image_handle* handle = nullptr;
    err = heif_context_get_primary_image_handle(ctx, &handle);
    if (err.code != heif_error_Ok || !handle) {
        heif_context_free(ctx);
        return nullptr;
    }

    heif_image* img = nullptr;
    err = heif_decode_image(handle, &img, heif_colorspace_RGB,
                            heif_chroma_interleaved_RGBA, nullptr);
    if (err.code != heif_error_Ok || !img) {
        heif_image_handle_release(handle);
        heif_context_free(ctx);
        return nullptr;
    }

    int stride = 0;
    const uint8_t* plane = heif_image_get_plane_readonly(
        img, heif_channel_interleaved, &stride);
    int w = heif_image_get_width(img, heif_channel_interleaved);
    int h = heif_image_get_height(img, heif_channel_interleaved);

    if (!plane || w <= 0 || h <= 0) {
        heif_image_release(img);
        heif_image_handle_release(handle);
        heif_context_free(ctx);
        return nullptr;
    }

    // Copy to a tightly-packed RGBA buffer.
    size_t rowBytes = static_cast<size_t>(w) * 4;
    auto* pixels = static_cast<uint8_t*>(malloc(rowBytes * h));
    if (pixels) {
        for (int y = 0; y < h; ++y)
            memcpy(pixels + y * rowBytes, plane + y * stride, rowBytes);
    }

    *outW = w;
    *outH = h;

    heif_image_release(img);
    heif_image_handle_release(handle);
    heif_context_free(ctx);
    return pixels;
}

} // anonymous namespace
#endif // SKYDROP_HAS_LIBHEIF

// ── macOS ImageIO fallback (handles WebP, AVIF, JP2 and HEIC when libheif
//    is not installed) ────────────────────────────────────────────────────────
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

namespace {

static uint8_t* LoadWithImageIO(const char* path, int* outW, int* outH) {
    CFStringRef cfPath = CFStringCreateWithCString(nullptr, path, kCFStringEncodingUTF8);
    if (!cfPath) return nullptr;

    CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, cfPath, kCFURLPOSIXPathStyle, false);
    CFRelease(cfPath);
    if (!url) return nullptr;

    CGImageSourceRef src = CGImageSourceCreateWithURL(url, nullptr);
    CFRelease(url);
    if (!src) return nullptr;

    CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
    CFRelease(src);
    if (!img) return nullptr;

    int w = static_cast<int>(CGImageGetWidth(img));
    int h = static_cast<int>(CGImageGetHeight(img));

    size_t rowBytes = static_cast<size_t>(w) * 4;
    auto* pixels = static_cast<uint8_t*>(malloc(rowBytes * h));
    if (!pixels) { CGImageRelease(img); return nullptr; }

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(
        pixels, w, h, 8, rowBytes, cs,
        static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(cs);
    if (!ctx) { free(pixels); CGImageRelease(img); return nullptr; }

    CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), img);
    CGContextRelease(ctx);
    CGImageRelease(img);

    // Un-premultiply alpha.
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        uint8_t* px = pixels + i * 4;
        uint8_t a = px[3];
        if (a > 0 && a < 255) {
            px[0] = static_cast<uint8_t>(std::min(255, px[0] * 255 / a));
            px[1] = static_cast<uint8_t>(std::min(255, px[1] * 255 / a));
            px[2] = static_cast<uint8_t>(std::min(255, px[2] * 255 / a));
        }
    }

    *outW = w;
    *outH = h;
    return pixels;
}

} // anonymous namespace
#endif // __APPLE__

// ── Helpers ──────────────────────────────────────────────────────────────────

namespace {

static std::string GetLowerExtension(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot + 1);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

/// Returns true for file extensions that stb_image cannot handle.
static bool NeedsSpecialDecoder(const std::string& ext) {
    return ext == "heic" || ext == "heif" || ext == "webp" || ext == "avif"
        || ext == "jp2"  || ext == "j2k";
}

} // anonymous namespace

namespace sky {

bool ImageData::Load(const std::string& path) {
    int w = 0, h = 0;
    u8* data = nullptr;
    std::string ext = GetLowerExtension(path);

    // ── 1. Try libheif for HEIC/HEIF ─────────────────────────────────────────
#ifdef SKYDROP_HAS_LIBHEIF
    if (ext == "heic" || ext == "heif") {
        data = LoadWithLibheif(path.c_str(), &w, &h);
        if (data) {
            width_    = static_cast<u32>(w);
            height_   = static_cast<u32>(h);
            channels_ = 4;
            pixels_.assign(data, data + static_cast<size_t>(w) * h * 4);
            free(data);
            filePath_ = path;
            return true;
        }
    }
#endif

    // ── 2. Try stb_image (PNG, JPEG, BMP, TGA, GIF, HDR, PSD) ────────────────
    if (!NeedsSpecialDecoder(ext)) {
        int ch = 0;
        data = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (data) {
            width_    = static_cast<u32>(w);
            height_   = static_cast<u32>(h);
            channels_ = 4;
            pixels_.assign(data, data + width_ * height_ * channels_);
            stbi_image_free(data);
            filePath_ = path;
            return true;
        }
    }

    // ── 3. macOS ImageIO fallback (HEIC without libheif, WebP, AVIF, JP2…) ───
#ifdef __APPLE__
    data = LoadWithImageIO(path.c_str(), &w, &h);
    if (data) {
        width_    = static_cast<u32>(w);
        height_   = static_cast<u32>(h);
        channels_ = 4;
        pixels_.assign(data, data + static_cast<size_t>(w) * h * 4);
        free(data);
        filePath_ = path;
        return true;
    }
#endif

    // ── 4. Last resort: try stb_image anyway (maybe the extension was wrong) ─
    {
        int ch = 0;
        data = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (data) {
            width_    = static_cast<u32>(w);
            height_   = static_cast<u32>(h);
            channels_ = 4;
            pixels_.assign(data, data + width_ * height_ * channels_);
            stbi_image_free(data);
            filePath_ = path;
            return true;
        }
    }

    return false;
}

bool ImageData::LoadFromMemory(const u8* data, size_t size) {
    int w, h, ch;
    u8* px = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &ch, 4);
    if (!px) return false;

    width_    = static_cast<u32>(w);
    height_   = static_cast<u32>(h);
    channels_ = 4;
    pixels_.assign(px, px + width_ * height_ * channels_);
    stbi_image_free(px);
    return true;
}

void ImageData::Create(u32 width, u32 height, u32 channels) {
    width_    = width;
    height_   = height;
    channels_ = channels;
    pixels_.assign(static_cast<size_t>(width) * height * channels, 0);
}

std::array<f32, 4> ImageData::GetPixel(u32 x, u32 y) const {
    if (x >= width_ || y >= height_) return {0, 0, 0, 0};
    size_t idx = (static_cast<size_t>(y) * width_ + x) * channels_;
    std::array<f32, 4> c = {0, 0, 0, 1.0f};
    for (u32 i = 0; i < std::min(channels_, 4u); ++i)
        c[i] = static_cast<f32>(pixels_[idx + i]) / 255.0f;
    return c;
}

f32 ImageData::GetLuminance(u32 x, u32 y) const {
    auto px = GetPixel(x, y);
    return 0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2];
}

std::vector<f32> ImageData::ToGrayscale() const {
    std::vector<f32> gray(static_cast<size_t>(width_) * height_);
    for (u32 y = 0; y < height_; ++y)
        for (u32 x = 0; x < width_; ++x)
            gray[y * width_ + x] = GetLuminance(x, y);
    return gray;
}

std::vector<f32> ImageData::ExtractChannel(u32 channel) const {
    std::vector<f32> ch(static_cast<size_t>(width_) * height_);
    for (u32 y = 0; y < height_; ++y)
        for (u32 x = 0; x < width_; ++x) {
            size_t idx = (static_cast<size_t>(y) * width_ + x) * channels_;
            ch[y * width_ + x] = (channel < channels_)
                ? static_cast<f32>(pixels_[idx + channel]) / 255.0f : 0.0f;
        }
    return ch;
}

std::array<f32, 4> ImageData::AverageColor(u32 x, u32 y, u32 w, u32 h) const {
    std::array<f32, 4> sum = {0, 0, 0, 0};
    u32 count = 0;
    for (u32 dy = 0; dy < h && (y + dy) < height_; ++dy) {
        for (u32 dx = 0; dx < w && (x + dx) < width_; ++dx) {
            auto px = GetPixel(x + dx, y + dy);
            for (int i = 0; i < 4; ++i) sum[i] += px[i];
            ++count;
        }
    }
    if (count > 0) for (auto& s : sum) s /= static_cast<f32>(count);
    return sum;
}

f32 ImageData::DominantHue(u32 x, u32 y, u32 w, u32 h) const {
    auto avg = AverageColor(x, y, w, h);
    f32 r = avg[0], g = avg[1], b = avg[2];
    f32 maxC = std::max({r, g, b});
    f32 minC = std::min({r, g, b});
    f32 delta = maxC - minC;

    if (delta < 1e-6f) return 0.0f; // achromatic

    f32 hue = 0.0f;
    if (maxC == r)      hue = 60.0f * std::fmod((g - b) / delta, 6.0f);
    else if (maxC == g) hue = 60.0f * ((b - r) / delta + 2.0f);
    else                hue = 60.0f * ((r - g) / delta + 4.0f);
    if (hue < 0.0f) hue += 360.0f;
    return hue;
}

f32 ImageData::AverageBrightness(u32 x, u32 y, u32 w, u32 h) const {
    f32 sum = 0.0f;
    u32 count = 0;
    for (u32 dy = 0; dy < h && (y + dy) < height_; ++dy) {
        for (u32 dx = 0; dx < w && (x + dx) < width_; ++dx) {
            sum += GetLuminance(x + dx, y + dy);
            ++count;
        }
    }
    return (count > 0) ? sum / static_cast<f32>(count) : 0.0f;
}

ImageData ImageData::ResizeToPow2() const {
    u32 newW = FFT::NextPow2(width_);
    u32 newH = FFT::NextPow2(height_);
    if (newW == width_ && newH == height_) {
        return *this;
    }

    ImageData result;
    result.width_    = newW;
    result.height_   = newH;
    result.channels_ = channels_;
    result.pixels_.resize(static_cast<size_t>(newW) * newH * channels_, 0);

    // Simple bilinear resize.
    for (u32 y = 0; y < newH; ++y) {
        f32 srcY = static_cast<f32>(y) * static_cast<f32>(height_) / static_cast<f32>(newH);
        u32 y0 = static_cast<u32>(srcY);
        u32 y1 = std::min(y0 + 1, height_ - 1);
        f32 fy = srcY - static_cast<f32>(y0);

        for (u32 x = 0; x < newW; ++x) {
            f32 srcX = static_cast<f32>(x) * static_cast<f32>(width_) / static_cast<f32>(newW);
            u32 x0 = static_cast<u32>(srcX);
            u32 x1 = std::min(x0 + 1, width_ - 1);
            f32 fx = srcX - static_cast<f32>(x0);

            size_t dstIdx = (static_cast<size_t>(y) * newW + x) * channels_;
            for (u32 c = 0; c < channels_; ++c) {
                f32 v00 = static_cast<f32>(pixels_[(y0 * width_ + x0) * channels_ + c]);
                f32 v10 = static_cast<f32>(pixels_[(y0 * width_ + x1) * channels_ + c]);
                f32 v01 = static_cast<f32>(pixels_[(y1 * width_ + x0) * channels_ + c]);
                f32 v11 = static_cast<f32>(pixels_[(y1 * width_ + x1) * channels_ + c]);
                f32 val = v00 * (1 - fx) * (1 - fy) + v10 * fx * (1 - fy)
                        + v01 * (1 - fx) * fy + v11 * fx * fy;
                result.pixels_[dstIdx + c] = static_cast<u8>(std::clamp(val, 0.0f, 255.0f));
            }
        }
    }
    return result;
}

ImageData ImageData::Downscale(u32 maxDim) const {
    if (!IsLoaded()) return *this;
    u32 longest = std::max(width_, height_);
    if (longest <= maxDim) return *this;

    f32 scale = static_cast<f32>(maxDim) / static_cast<f32>(longest);
    u32 newW = std::max(1u, static_cast<u32>(width_ * scale));
    u32 newH = std::max(1u, static_cast<u32>(height_ * scale));

    ImageData result;
    result.width_    = newW;
    result.height_   = newH;
    result.channels_ = channels_;
    result.pixels_.resize(static_cast<size_t>(newW) * newH * channels_, 0);

    for (u32 y = 0; y < newH; ++y) {
        f32 srcY = static_cast<f32>(y) * static_cast<f32>(height_) / static_cast<f32>(newH);
        u32 y0 = static_cast<u32>(srcY);
        u32 y1 = std::min(y0 + 1, height_ - 1);
        f32 fy = srcY - static_cast<f32>(y0);

        for (u32 x = 0; x < newW; ++x) {
            f32 srcX = static_cast<f32>(x) * static_cast<f32>(width_) / static_cast<f32>(newW);
            u32 x0 = static_cast<u32>(srcX);
            u32 x1 = std::min(x0 + 1, width_ - 1);
            f32 fx = srcX - static_cast<f32>(x0);

            size_t dstIdx = (static_cast<size_t>(y) * newW + x) * channels_;
            for (u32 c = 0; c < channels_; ++c) {
                f32 v00 = static_cast<f32>(pixels_[(y0 * width_ + x0) * channels_ + c]);
                f32 v10 = static_cast<f32>(pixels_[(y0 * width_ + x1) * channels_ + c]);
                f32 v01 = static_cast<f32>(pixels_[(y1 * width_ + x0) * channels_ + c]);
                f32 v11 = static_cast<f32>(pixels_[(y1 * width_ + x1) * channels_ + c]);
                f32 val = v00 * (1 - fx) * (1 - fy) + v10 * fx * (1 - fy)
                        + v01 * (1 - fx) * fy + v11 * fx * fy;
                result.pixels_[dstIdx + c] = static_cast<u8>(std::clamp(val, 0.0f, 255.0f));
            }
        }
    }
    return result;
}

} // namespace sky
