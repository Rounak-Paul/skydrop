#pragma once

/// @file fft.h
/// @brief Fast Fourier Transform for audio and image frequency analysis.
/// Uses Cooley-Tukey radix-2 DIT algorithm.

#include "skydrop/core/types.h"

namespace sky {

class FFT {
public:
    using Complex = std::complex<f32>;

    /// Forward FFT (in-place). Size must be power of 2.
    static void Forward(std::vector<Complex>& data) {
        const size_t N = data.size();
        if (N <= 1) return;
        assert((N & (N - 1)) == 0 && "FFT size must be power of 2");

        BitReverse(data);

        for (size_t len = 2; len <= N; len <<= 1) {
            f32 angle = -kTwoPI / static_cast<f32>(len);
            Complex wn(std::cos(angle), std::sin(angle));
            for (size_t i = 0; i < N; i += len) {
                Complex w(1.0f, 0.0f);
                for (size_t j = 0; j < len / 2; ++j) {
                    Complex u = data[i + j];
                    Complex v = data[i + j + len / 2] * w;
                    data[i + j]           = u + v;
                    data[i + j + len / 2] = u - v;
                    w *= wn;
                }
            }
        }
    }

    /// Inverse FFT (in-place).
    static void Inverse(std::vector<Complex>& data) {
        for (auto& c : data) c = std::conj(c);
        Forward(data);
        f32 invN = 1.0f / static_cast<f32>(data.size());
        for (auto& c : data) c = std::conj(c) * invN;
    }

    /// Compute magnitude spectrum from complex FFT output.
    static std::vector<f32> Magnitude(const std::vector<Complex>& fftData) {
        std::vector<f32> mag(fftData.size() / 2);
        for (size_t i = 0; i < mag.size(); ++i)
            mag[i] = std::abs(fftData[i]);
        return mag;
    }

    /// Compute phase spectrum from complex FFT output.
    static std::vector<f32> Phase(const std::vector<Complex>& fftData) {
        std::vector<f32> phase(fftData.size() / 2);
        for (size_t i = 0; i < phase.size(); ++i)
            phase[i] = std::arg(fftData[i]);
        return phase;
    }

    /// Compute power spectrum (magnitude squared).
    static std::vector<f32> Power(const std::vector<Complex>& fftData) {
        std::vector<f32> pow(fftData.size() / 2);
        for (size_t i = 0; i < pow.size(); ++i)
            pow[i] = std::norm(fftData[i]);
        return pow;
    }

    /// 2D FFT on a grayscale image (row-major, width x height, both power of 2).
    static void Forward2D(std::vector<Complex>& data, u32 width, u32 height) {
        // Row-wise FFT.
        std::vector<Complex> row(width);
        for (u32 y = 0; y < height; ++y) {
            for (u32 x = 0; x < width; ++x) row[x] = data[y * width + x];
            Forward(row);
            for (u32 x = 0; x < width; ++x) data[y * width + x] = row[x];
        }
        // Column-wise FFT.
        std::vector<Complex> col(height);
        for (u32 x = 0; x < width; ++x) {
            for (u32 y = 0; y < height; ++y) col[y] = data[y * width + x];
            Forward(col);
            for (u32 y = 0; y < height; ++y) data[y * width + x] = col[y];
        }
    }

    /// Inverse 2D FFT.
    static void Inverse2D(std::vector<Complex>& data, u32 width, u32 height) {
        // Row-wise IFFT.
        std::vector<Complex> row(width);
        for (u32 y = 0; y < height; ++y) {
            for (u32 x = 0; x < width; ++x) row[x] = data[y * width + x];
            Inverse(row);
            for (u32 x = 0; x < width; ++x) data[y * width + x] = row[x];
        }
        // Column-wise IFFT.
        std::vector<Complex> col(height);
        for (u32 x = 0; x < width; ++x) {
            for (u32 y = 0; y < height; ++y) col[y] = data[y * width + x];
            Inverse(col);
            for (u32 y = 0; y < height; ++y) data[y * width + x] = col[y];
        }
    }

    /// Compute 2D magnitude spectrum from 2D FFT data.
    static std::vector<f32> Magnitude2D(const std::vector<Complex>& fftData, u32 width, u32 height) {
        std::vector<f32> mag(width * height);
        for (size_t i = 0; i < mag.size(); ++i)
            mag[i] = std::abs(fftData[i]);
        return mag;
    }

    /// Shift zero-frequency to center (for visualization).
    static void FFTShift2D(std::vector<f32>& data, u32 width, u32 height) {
        u32 hw = width / 2, hh = height / 2;
        for (u32 y = 0; y < hh; ++y) {
            for (u32 x = 0; x < hw; ++x) {
                std::swap(data[y * width + x], data[(y + hh) * width + (x + hw)]);
                std::swap(data[y * width + (x + hw)], data[(y + hh) * width + x]);
            }
        }
    }

    /// Helper: next power of 2 >= n.
    static u32 NextPow2(u32 n) {
        u32 p = 1;
        while (p < n) p <<= 1;
        return p;
    }

    /// Apply a Hann window.
    static void ApplyHannWindow(std::vector<f32>& data) {
        size_t N = data.size();
        for (size_t i = 0; i < N; ++i)
            data[i] *= 0.5f * (1.0f - std::cos(kTwoPI * static_cast<f32>(i) / static_cast<f32>(N)));
    }

private:
    static void BitReverse(std::vector<Complex>& data) {
        size_t N = data.size();
        for (size_t i = 1, j = 0; i < N; ++i) {
            size_t bit = N >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(data[i], data[j]);
        }
    }
};

} // namespace sky
