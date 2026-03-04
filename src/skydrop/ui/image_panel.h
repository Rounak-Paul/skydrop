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
    /// Start loading an image asynchronously.  Returns immediately.
    /// The decode + analysis run on a background thread.
    /// Call Poll() each frame; when IsAnalyzed() becomes true the data is ready.
    void LoadImage(const std::string& path);

    /// Poll for background load/analysis completion.  Call once per frame.
    void Poll();

    /// Get the loaded image data.
    const ImageData& GetImage() const { return image_; }

    /// Get the analysis.
    const ImageAnalysis& GetAnalysis() const { return analysis_; }

    /// Has an image been loaded (decode finished)?
    bool HasImage() const { return image_.IsLoaded(); }

    /// Has analysis completed?
    bool IsAnalyzed() const { return analyzed_; }

    /// Is background work (decode + analysis) still running?
    bool IsLoading() const { return loading_.load(); }

    /// Get the GPU texture for ImGui rendering.
    tvk::Texture* GetTexture() const { return texture_.get(); }

    /// Release the GPU texture (call before Vulkan teardown).
    void ReleaseTexture() { prevTexture_.reset(); texture_.reset(); }

    /// Draw the panel UI (standalone window). Call from OnUI().
    void Draw();

    /// Draw the spectrum visualization of the image.
    void DrawSpectrumOverlay(float width = 0, float height = 0);

private:
    ImageData image_;
    ImageAnalysis analysis_;
    tvk::Ref<tvk::Texture> texture_;
    tvk::Ref<tvk::Texture> prevTexture_;  ///< Kept alive one extra frame.
    bool analyzed_ = false;
    std::atomic<bool> loading_{false};

    /// Background result: decoded image + analysis.
    struct LoadResult {
        ImageData     image;
        ImageAnalysis analysis;
        std::string   path;
    };
    std::future<LoadResult> loadFuture_;

    void CreateTexture(const std::string& path);  ///< Main-thread GPU upload.
};

} // namespace sky
