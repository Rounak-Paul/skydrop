#pragma once

/// @file image_analyzer.h
/// @brief Analyze image frequency content, color distribution, edge patterns, and textures.
/// This is the core bridge between image data and musical parameters.

#include "skydrop/image/image_loader.h"
#include "skydrop/dsp/fft.h"

namespace sky {

/// Result of frequency analysis on an image.
struct FrequencyBand {
    f32 centerFreq;    ///< Spatial frequency (cycles/pixel), mapped to audio freq later.
    f32 magnitude;     ///< Strength of this frequency band.
    f32 phase;         ///< Phase angle.
    f32 orientation;   ///< Dominant direction in degrees [0, 180).
};

/// Result of color analysis.
struct ColorProfile {
    f32 dominantHue;       ///< [0, 360)
    f32 saturation;        ///< [0, 1]
    f32 brightness;        ///< [0, 1]
    f32 warmth;            ///< computed warm/cool ratio [-1, 1]
    f32 complexity;        ///< color entropy / variety [0, 1]
    std::vector<std::array<f32, 3>> palette; ///< Top colors (HSB).
};

/// Edge analysis result.
struct EdgeProfile {
    f32 density;           ///< Overall edge density [0, 1].
    f32 directionality;    ///< How directional edges are [0, 1] (1 = all same direction).
    f32 dominantAngle;     ///< Dominant edge orientation [0, 180).
    std::vector<f32> horizontalProfile; ///< Edge density per row.
    std::vector<f32> verticalProfile;   ///< Edge density per column.
};

/// Texture analysis result.
struct TextureProfile {
    f32 roughness;     ///< High-frequency energy ratio [0, 1].
    f32 regularity;    ///< Periodic pattern strength [0, 1].
    f32 contrast;      ///< Local contrast measure [0, 1].
    f32 granularity;   ///< Fine vs coarse texture [0, 1].
};

/// Complete image analysis used by music synthesis algorithms.
struct ImageAnalysis {
    std::vector<FrequencyBand> frequencyBands;
    ColorProfile color;
    EdgeProfile  edges;
    TextureProfile texture;

    /// Raw 2D FFT magnitude spectrum (for direct sonification).
    std::vector<f32> fftMagnitude2D;
    u32 fftWidth  = 0;
    u32 fftHeight = 0;

    /// Frequency-sorted top N peaks from the spectrum.
    struct SpectrumPeak {
        f32 spatialFreqX; ///< Horizontal spatial frequency.
        f32 spatialFreqY; ///< Vertical spatial frequency.
        f32 magnitude;
        f32 phase;
    };
    std::vector<SpectrumPeak> topPeaks;
};

/// Performs comprehensive analysis on an image for music synthesis.
class ImageAnalyzer {
public:
    /// Perform full analysis. `numBands` controls frequency band resolution.
    /// `numPeaks` is how many top spectrum peaks to extract.
    /// `maxAnalysisDim` caps the image dimension before heavy processing.
    static ImageAnalysis Analyze(const ImageData& image, u32 numBands = 64,
                                 u32 numPeaks = 128, u32 maxAnalysisDim = 512);

    /// Extract only the FFT spectrum. Useful for direct sonification.
    static void AnalyzeSpectrum(const ImageData& image, ImageAnalysis& result);

    /// Extract color profile.
    static ColorProfile AnalyzeColor(const ImageData& image, u32 gridSize = 8);

    /// Edge detection (Sobel) and analysis.
    static EdgeProfile AnalyzeEdges(const ImageData& image);

    /// Texture analysis using the FFT spectrum.
    static TextureProfile AnalyzeTexture(const std::vector<f32>& fftMag, u32 width, u32 height);

private:
    /// Group 2D FFT into radial frequency bands.
    static std::vector<FrequencyBand> BandFromSpectrum(
        const std::vector<FFT::Complex>& fftData,
        const std::vector<f32>& phases,
        u32 width, u32 height, u32 numBands);

    /// Find the top N magnitude peaks in a 2D spectrum.
    static std::vector<ImageAnalysis::SpectrumPeak> FindPeaks(
        const std::vector<f32>& mag, u32 width, u32 height, u32 numPeaks);
};

} // namespace sky
