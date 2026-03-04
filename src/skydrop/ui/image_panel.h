#pragma once

/// @file image_panel.h
/// @brief Image display and analysis panel.

#include "skydrop/image/image_loader.h"
#include "skydrop/image/image_analyzer.h"

#include <tinyvk/tinyvk.h>
#include <future>
#include <atomic>

namespace sky {

/// Displays a loaded image and its analysis results.
class ImagePanel {
public:
    /// Load an image from file and create a GPU texture for display.
    /// Analysis runs asynchronously in a background thread.
    bool LoadImage(const std::string& path);

    /// Poll for async analysis completion. Call once per frame from OnUI().
    void PollAnalysis();

    /// Get the loaded image data.
    const ImageData& GetImage() const { return image_; }

    /// Get the analysis.
    const ImageAnalysis& GetAnalysis() const { return analysis_; }

    /// Has an image been loaded?
    bool HasImage() const { return image_.IsLoaded(); }

    /// Has analysis been run?
    bool IsAnalyzed() const { return analyzed_; }

    /// Is analysis currently running in the background?
    bool IsAnalyzing() const { return analyzing_.load(); }

    /// Get the GPU texture for ImGui rendering.
    tvk::Texture* GetTexture() const { return texture_.get(); }

    /// Draw the panel UI (standalone window). Call from OnUI().
    void Draw();

    /// Draw the spectrum visualization of the image.
    void DrawSpectrumOverlay(float width = 0, float height = 0);

private:
    ImageData image_;
    ImageAnalysis analysis_;
    tvk::Ref<tvk::Texture> texture_;
    bool analyzed_ = false;
    std::atomic<bool> analyzing_{false};
    std::future<ImageAnalysis> analysisFuture_;
};

} // namespace sky
