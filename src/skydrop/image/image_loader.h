#pragma once

/// @file image_loader.h
/// @brief Load images using stb_image and provide pixel access.

#include "skydrop/core/types.h"

// stb_image is included from tinyvk's vendor directory.
// Implementation is in image_loader.cpp.

namespace sky {

/// Loaded image data with pixel access utilities.
class ImageData {
public:
    ImageData() = default;
    ~ImageData() = default;

    /// Load an image from file. Returns true on success.
    bool Load(const std::string& path);

    /// Load from memory buffer.
    bool LoadFromMemory(const u8* data, size_t size);

    /// Create a blank image.
    void Create(u32 width, u32 height, u32 channels = 4);

    /// Get pixel at (x, y). Returns {r, g, b, a} normalised [0, 1].
    std::array<f32, 4> GetPixel(u32 x, u32 y) const;

    /// Get luminance at (x, y) in [0, 1].
    f32 GetLuminance(u32 x, u32 y) const;

    /// Extract a single-channel grayscale image as a float vector.
    std::vector<f32> ToGrayscale() const;

    /// Extract a specific channel as float vector.
    std::vector<f32> ExtractChannel(u32 channel) const;

    /// Get average color of a rectangular region.
    std::array<f32, 4> AverageColor(u32 x, u32 y, u32 w, u32 h) const;

    /// Get dominant hue of a region in [0, 360) degrees.
    f32 DominantHue(u32 x, u32 y, u32 w, u32 h) const;

    /// Compute average brightness of a region in [0, 1].
    f32 AverageBrightness(u32 x, u32 y, u32 w, u32 h) const;

    /// Resize the image to power-of-2 dimensions (for FFT). Returns new data.
    ImageData ResizeToPow2() const;

    /// Downscale so the longest edge is at most maxDim pixels. Preserves aspect ratio.
    /// Returns *this if already small enough.
    ImageData Downscale(u32 maxDim) const;

    bool  IsLoaded()  const { return !pixels_.empty(); }
    u32   Width()     const { return width_; }
    u32   Height()    const { return height_; }
    u32   NumChannels() const { return channels_; }
    const std::vector<u8>& Pixels() const { return pixels_; }

    /// File path (if loaded from file).
    const std::string& FilePath() const { return filePath_; }

private:
    std::vector<u8> pixels_;
    u32 width_    = 0;
    u32 height_   = 0;
    u32 channels_ = 0;
    std::string filePath_;
};

} // namespace sky
