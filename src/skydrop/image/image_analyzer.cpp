/// @file image_analyzer.cpp
/// @brief Implementation of image analysis for music synthesis.

#include "skydrop/image/image_analyzer.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace sky {

ImageAnalysis ImageAnalyzer::Analyze(const ImageData& image, u32 numBands, u32 numPeaks, u32 maxAnalysisDim) {
    ImageAnalysis result;

    // Downscale the image before any heavy processing.
    auto small = image.Downscale(maxAnalysisDim);

    // -- Single FFT pass (no duplicate work) ----------------------------------
    auto pow2 = small.ResizeToPow2();
    auto gray = pow2.ToGrayscale();
    u32 w = pow2.Width(), h = pow2.Height();

    std::vector<FFT::Complex> fftData(w * h);
    for (size_t i = 0; i < gray.size(); ++i)
        fftData[i] = FFT::Complex(gray[i], 0.0f);
    FFT::Forward2D(fftData, w, h);

    // Magnitude + shift (for texture analysis and visualization).
    result.fftMagnitude2D = FFT::Magnitude2D(fftData, w, h);
    result.fftWidth  = w;
    result.fftHeight = h;
    FFT::FFTShift2D(result.fftMagnitude2D, w, h);

    // Phases for band extraction.
    std::vector<f32> phases(w * h);
    for (size_t i = 0; i < fftData.size(); ++i)
        phases[i] = std::arg(fftData[i]);

    result.frequencyBands = BandFromSpectrum(fftData, phases, w, h, numBands);
    result.topPeaks = FindPeaks(result.fftMagnitude2D, result.fftWidth, result.fftHeight, numPeaks);

    // -- Color (use downscaled image), edges, texture -------------------------
    result.color   = AnalyzeColor(small);
    result.edges   = AnalyzeEdges(small);
    result.texture = AnalyzeTexture(result.fftMagnitude2D, result.fftWidth, result.fftHeight);

    return result;
}

void ImageAnalyzer::AnalyzeSpectrum(const ImageData& image, ImageAnalysis& result) {
    auto pow2 = image.ResizeToPow2();
    auto gray = pow2.ToGrayscale();
    u32 w = pow2.Width(), h = pow2.Height();

    std::vector<FFT::Complex> fftData(w * h);
    for (size_t i = 0; i < gray.size(); ++i)
        fftData[i] = FFT::Complex(gray[i], 0.0f);

    FFT::Forward2D(fftData, w, h);

    result.fftMagnitude2D = FFT::Magnitude2D(fftData, w, h);
    result.fftWidth  = w;
    result.fftHeight = h;

    // Shift for visualization.
    FFT::FFTShift2D(result.fftMagnitude2D, w, h);
}

ColorProfile ImageAnalyzer::AnalyzeColor(const ImageData& image, u32 gridSize) {
    ColorProfile cp;
    if (!image.IsLoaded()) return cp;

    u32 cellW = std::max(1u, image.Width() / gridSize);
    u32 cellH = std::max(1u, image.Height() / gridSize);

    // Collect hue histogram (36 bins of 10 degrees each).
    std::array<f32, 36> hueHist{};
    f32 totalSat = 0, totalBri = 0;
    u32 sampleCount = 0;

    for (u32 gy = 0; gy < gridSize; ++gy) {
        for (u32 gx = 0; gx < gridSize; ++gx) {
            u32 x = gx * cellW, y = gy * cellH;
            auto avg = image.AverageColor(x, y, cellW, cellH);

            // RGB to HSB.
            f32 r = avg[0], g = avg[1], b = avg[2];
            f32 maxC = std::max({r, g, b});
            f32 minC = std::min({r, g, b});
            f32 delta = maxC - minC;
            f32 bri = maxC;
            f32 sat = (maxC > 1e-6f) ? delta / maxC : 0.0f;

            f32 hue = image.DominantHue(x, y, cellW, cellH);
            i32 bin = static_cast<i32>(hue / 10.0f) % 36;
            hueHist[bin] += sat * bri; // weight by saturation and brightness

            totalSat += sat;
            totalBri += bri;
            ++sampleCount;
        }
    }

    // Find dominant hue.
    i32 bestBin = 0;
    f32 bestVal = 0;
    for (i32 i = 0; i < 36; ++i) {
        if (hueHist[i] > bestVal) { bestVal = hueHist[i]; bestBin = i; }
    }
    cp.dominantHue = static_cast<f32>(bestBin) * 10.0f + 5.0f;
    cp.saturation  = totalSat / static_cast<f32>(sampleCount);
    cp.brightness  = totalBri / static_cast<f32>(sampleCount);

    // Warmth: warm hues (red/orange/yellow: 0-60, 300-360) vs cool (120-240).
    f32 warm = 0, cool = 0;
    for (i32 i = 0; i < 36; ++i) {
        f32 deg = i * 10.0f;
        if (deg < 60 || deg >= 300) warm += hueHist[i];
        else if (deg >= 120 && deg < 240) cool += hueHist[i];
    }
    f32 total = warm + cool + 1e-10f;
    cp.warmth = (warm - cool) / total;

    // Color complexity: entropy of hue histogram.
    f32 sumH = 0;
    for (auto& v : hueHist) sumH += v;
    f32 entropy = 0;
    if (sumH > 0) {
        for (auto& v : hueHist) {
            f32 p = v / sumH;
            if (p > 1e-10f) entropy -= p * std::log2(p);
        }
        cp.complexity = entropy / std::log2(36.0f); // normalise to [0, 1]
    }

    // Build palette: top 5 hue bins → HSB.
    std::vector<std::pair<f32, i32>> sorted;
    for (i32 i = 0; i < 36; ++i) sorted.push_back({hueHist[i], i});
    std::sort(sorted.begin(), sorted.end(), std::greater<>());
    for (int i = 0; i < std::min(5, static_cast<int>(sorted.size())); ++i) {
        f32 h = sorted[i].second * 10.0f + 5.0f;
        cp.palette.push_back({h, cp.saturation, cp.brightness});
    }

    return cp;
}

EdgeProfile ImageAnalyzer::AnalyzeEdges(const ImageData& image) {
    EdgeProfile ep;
    if (!image.IsLoaded()) return ep;

    auto gray = image.ToGrayscale();
    u32 w = image.Width(), h = image.Height();

    // Sobel edge detection.
    std::vector<f32> edgeMag(w * h, 0.0f);
    std::vector<f32> edgeDir(w * h, 0.0f);

    for (u32 y = 1; y + 1 < h; ++y) {
        for (u32 x = 1; x + 1 < w; ++x) {
            // Gx
            f32 gx = -gray[(y-1)*w + (x-1)] - 2*gray[y*w + (x-1)] - gray[(y+1)*w + (x-1)]
                    + gray[(y-1)*w + (x+1)] + 2*gray[y*w + (x+1)] + gray[(y+1)*w + (x+1)];
            // Gy
            f32 gy = -gray[(y-1)*w + (x-1)] - 2*gray[(y-1)*w + x] - gray[(y-1)*w + (x+1)]
                    + gray[(y+1)*w + (x-1)] + 2*gray[(y+1)*w + x] + gray[(y+1)*w + (x+1)];

            edgeMag[y * w + x] = std::sqrt(gx * gx + gy * gy);
            edgeDir[y * w + x] = std::atan2(gy, gx) * 180.0f / kPI;
            if (edgeDir[y * w + x] < 0) edgeDir[y * w + x] += 180.0f;
        }
    }

    // Overall edge density.
    f32 edgeSum = 0;
    for (auto v : edgeMag) edgeSum += v;
    ep.density = std::min(1.0f, edgeSum / static_cast<f32>(w * h) * 2.0f);

    // Direction histogram (18 bins of 10 degrees).
    std::array<f32, 18> dirHist{};
    for (u32 i = 0; i < w * h; ++i) {
        if (edgeMag[i] > 0.05f) {
            i32 bin = static_cast<i32>(edgeDir[i] / 10.0f) % 18;
            dirHist[bin] += edgeMag[i];
        }
    }

    f32 maxDir = *std::max_element(dirHist.begin(), dirHist.end());
    f32 dirSum = 0;
    for (auto v : dirHist) dirSum += v;
    ep.directionality = (dirSum > 0) ? maxDir / dirSum * 18.0f : 0.0f; // 1 = uniform, 18 = single direction
    ep.directionality = std::min(1.0f, ep.directionality / 3.0f);

    // Dominant angle.
    i32 maxBin = 0;
    for (i32 i = 0; i < 18; ++i)
        if (dirHist[i] > dirHist[maxBin]) maxBin = i;
    ep.dominantAngle = maxBin * 10.0f + 5.0f;

    // Horizontal and vertical profiles.
    ep.horizontalProfile.resize(h, 0.0f);
    ep.verticalProfile.resize(w, 0.0f);
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            ep.horizontalProfile[y] += edgeMag[y * w + x];
            ep.verticalProfile[x]   += edgeMag[y * w + x];
        }
    for (auto& v : ep.horizontalProfile) v /= static_cast<f32>(w);
    for (auto& v : ep.verticalProfile)   v /= static_cast<f32>(h);

    return ep;
}

TextureProfile ImageAnalyzer::AnalyzeTexture(const std::vector<f32>& fftMag, u32 width, u32 height) {
    TextureProfile tp;
    if (fftMag.empty()) return tp;

    f32 hw = width / 2.0f, hh = height / 2.0f;
    f32 maxRadius = std::sqrt(hw * hw + hh * hh);

    // Split energy into low/mid/high frequency.
    f32 lowEnergy = 0, midEnergy = 0, highEnergy = 0;
    f32 totalEnergy = 0;

    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            f32 dx = static_cast<f32>(x) - hw;
            f32 dy = static_cast<f32>(y) - hh;
            f32 r = std::sqrt(dx * dx + dy * dy) / maxRadius;
            f32 m = fftMag[y * width + x];

            totalEnergy += m;
            if (r < 0.15f)      lowEnergy  += m;
            else if (r < 0.5f)  midEnergy  += m;
            else                highEnergy += m;
        }
    }

    if (totalEnergy > 0) {
        tp.roughness   = highEnergy / totalEnergy;
        tp.granularity = (highEnergy + midEnergy) / totalEnergy;
    }

    // Regularity: look for peaks beyond DC in the spectrum.
    // A regular texture will have distinct peaks.
    f32 mean = totalEnergy / static_cast<f32>(width * height);
    f32 variance = 0;
    for (auto m : fftMag) {
        f32 d = m - mean;
        variance += d * d;
    }
    variance /= static_cast<f32>(width * height);
    f32 stddev = std::sqrt(variance);
    tp.regularity = std::min(1.0f, stddev / (mean + 1e-10f) * 0.5f);

    // Contrast from spectrum spread.
    tp.contrast = std::min(1.0f, (midEnergy + highEnergy) / (lowEnergy + 1e-10f) * 0.1f);

    return tp;
}

std::vector<FrequencyBand> ImageAnalyzer::BandFromSpectrum(
    const std::vector<FFT::Complex>& fftData,
    const std::vector<f32>& phases,
    u32 width, u32 height, u32 numBands)
{
    std::vector<FrequencyBand> bands(numBands);
    f32 hw = width / 2.0f, hh = height / 2.0f;
    f32 maxRadius = std::sqrt(hw * hw + hh * hh);

    // Each band covers a radial ring in the 2D spectrum.
    std::vector<f32> bandMag(numBands, 0.0f);
    std::vector<f32> bandPhase(numBands, 0.0f);
    std::vector<f32> bandAngle(numBands, 0.0f);
    std::vector<u32> bandCount(numBands, 0);

    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            f32 dx = static_cast<f32>(x) - hw;
            f32 dy = static_cast<f32>(y) - hh;
            f32 r = std::sqrt(dx * dx + dy * dy);
            f32 normR = r / maxRadius;

            u32 band = static_cast<u32>(normR * numBands);
            if (band >= numBands) band = numBands - 1;

            size_t idx = y * width + x;
            f32 mag = std::abs(fftData[idx]);
            bandMag[band] += mag;
            bandPhase[band] += phases[idx];
            bandAngle[band] += std::atan2(dy, dx);
            bandCount[band]++;
        }
    }

    for (u32 i = 0; i < numBands; ++i) {
        f32 n = static_cast<f32>(bandCount[i]);
        bands[i].centerFreq  = (static_cast<f32>(i) + 0.5f) / static_cast<f32>(numBands);
        bands[i].magnitude   = (n > 0) ? bandMag[i] / n : 0.0f;
        bands[i].phase       = (n > 0) ? bandPhase[i] / n : 0.0f;
        bands[i].orientation = (n > 0) ? (bandAngle[i] / n) * 180.0f / kPI : 0.0f;
        if (bands[i].orientation < 0) bands[i].orientation += 180.0f;
    }

    return bands;
}

std::vector<ImageAnalysis::SpectrumPeak> ImageAnalyzer::FindPeaks(
    const std::vector<f32>& mag, u32 width, u32 height, u32 numPeaks)
{
    // Collect all magnitudes with their spatial frequencies.
    struct MagIdx { f32 mag; u32 x, y; };
    std::vector<MagIdx> all;
    all.reserve(width * height);

    f32 hw = width / 2.0f, hh = height / 2.0f;
    for (u32 y = 0; y < height; ++y)
        for (u32 x = 0; x < width; ++x)
            all.push_back({mag[y * width + x], x, y});

    // Partial sort for top N.
    u32 n = std::min(numPeaks, static_cast<u32>(all.size()));
    std::partial_sort(all.begin(), all.begin() + n, all.end(),
        [](const MagIdx& a, const MagIdx& b) { return a.mag > b.mag; });

    std::vector<ImageAnalysis::SpectrumPeak> peaks(n);
    for (u32 i = 0; i < n; ++i) {
        peaks[i].spatialFreqX = (static_cast<f32>(all[i].x) - hw) / hw;
        peaks[i].spatialFreqY = (static_cast<f32>(all[i].y) - hh) / hh;
        peaks[i].magnitude    = all[i].mag;
        peaks[i].phase        = 0.0f; // phase info not available from magnitude-only
    }

    return peaks;
}

} // namespace sky
