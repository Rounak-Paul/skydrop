/// @file image_panel.cpp
/// @brief Image panel UI implementation.

#include "skydrop/ui/image_panel.h"
#include <imgui.h>
#include <algorithm>
#include <cctype>

namespace {

/// Check if the extension requires our own decoder (not stb_image).
static bool IsNonStbFormat(const std::string& path) {
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = path.substr(dot + 1);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == "heic" || ext == "heif" || ext == "webp" || ext == "avif"
        || ext == "jp2"  || ext == "j2k";
}

} // anonymous namespace

namespace sky {

void ImagePanel::LoadImage(const std::string& path) {
    // If a previous load is still in-flight, let it finish before starting new.
    if (loading_.load() && loadFuture_.valid()) {
        loadFuture_.wait();
        loading_.store(false);
    }
    analyzed_ = false;
    loading_.store(true);

    // Keep old texture alive through the current frame (ImGui may still reference it).
    prevTexture_ = std::move(texture_);

    // Decode + analyse entirely on a background thread.
    loadFuture_ = std::async(std::launch::async, [path]() -> LoadResult {
        LoadResult r;
        r.path = path;
        if (r.image.Load(path)) {
            r.analysis = ImageAnalyzer::Analyze(r.image);
        }
        return r;
    });
}

void ImagePanel::CreateTexture(const std::string& path) {
    auto* app = tvk::App::Get();
    if (!app) return;

    // For formats stb_image can handle, use tinyvk's file loader (fast path).
    if (!IsNonStbFormat(path)) {
        texture_ = app->LoadTexture(path);
    }

    // Create from decoded pixels if the fast path didn't work.
    if (!texture_ && !image_.Pixels().empty()) {
        texture_ = tvk::Texture::Create(
            app->GetRenderer(),
            image_.Pixels().data(),
            image_.Width(), image_.Height());
    }

    if (texture_) texture_->BindToImGui();
}

void ImagePanel::Poll() {
    // Release the previous frame's texture now that it's no longer in-flight.
    prevTexture_.reset();

    if (!loading_.load()) return;
    if (!loadFuture_.valid()) return;

    // Non-blocking check.
    if (loadFuture_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        return;

    auto result = loadFuture_.get();
    loading_.store(false);

    if (!result.image.IsLoaded()) return;

    image_    = std::move(result.image);
    analysis_ = std::move(result.analysis);
    analyzed_ = true;

    // GPU texture upload (must happen on the main thread).
    CreateTexture(result.path);
}

void ImagePanel::Draw() {
    ImGui::Begin("Image");

    if (!image_.IsLoaded()) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No image loaded.");
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Drag & drop or use File > Open Image");

        if (ImGui::Button("Open Image...")) {
            auto result = tvk::FileDialog::OpenFile({tvk::Filters::Images()});
            if (result.has_value()) {
                LoadImage(result.value());
            }
        }
    } else {
        // Display image.
        if (texture_) {
            float avail = ImGui::GetContentRegionAvail().x;
            float aspect = static_cast<float>(image_.Height()) / static_cast<float>(image_.Width());
            float displayW = std::min(avail, 512.0f);
            float displayH = displayW * aspect;

            ImGui::Image(texture_->GetImGuiTextureID(), ImVec2(displayW, displayH));
        }

        ImGui::Text("%ux%u  (%u channels)", image_.Width(), image_.Height(), image_.NumChannels());

        if (ImGui::Button("Change Image...")) {
            auto result = tvk::FileDialog::OpenFile({tvk::Filters::Images()});
            if (result.has_value()) {
                LoadImage(result.value());
            }
        }

        // Analysis summary.
        if (analyzed_ && ImGui::CollapsingHeader("Analysis", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Color:");
            ImGui::BulletText("Dominant Hue: %.0f degrees", analysis_.color.dominantHue);
            ImGui::BulletText("Saturation: %.2f", analysis_.color.saturation);
            ImGui::BulletText("Brightness: %.2f", analysis_.color.brightness);
            ImGui::BulletText("Warmth: %.2f", analysis_.color.warmth);
            ImGui::BulletText("Complexity: %.2f", analysis_.color.complexity);

            ImGui::Spacing();
            ImGui::Text("Edges:");
            ImGui::BulletText("Density: %.2f", analysis_.edges.density);
            ImGui::BulletText("Directionality: %.2f", analysis_.edges.directionality);
            ImGui::BulletText("Dominant Angle: %.0f degrees", analysis_.edges.dominantAngle);

            ImGui::Spacing();
            ImGui::Text("Texture:");
            ImGui::BulletText("Roughness: %.2f", analysis_.texture.roughness);
            ImGui::BulletText("Regularity: %.2f", analysis_.texture.regularity);
            ImGui::BulletText("Contrast: %.2f", analysis_.texture.contrast);
            ImGui::BulletText("Granularity: %.2f", analysis_.texture.granularity);

            ImGui::Spacing();
            ImGui::Text("Spectrum: %u peaks, %ux%u FFT",
                static_cast<u32>(analysis_.topPeaks.size()),
                analysis_.fftWidth, analysis_.fftHeight);
        }
    }

    ImGui::End();
}

void ImagePanel::DrawSpectrumOverlay(float width, float height) {
    if (!analyzed_ || analysis_.fftMagnitude2D.empty()) return;

    if (width <= 0) width = ImGui::GetContentRegionAvail().x;
    if (height <= 0) height = width;

    auto canvasPos = ImGui::GetCursorScreenPos();
    auto* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(canvasPos,
        ImVec2(canvasPos.x + width, canvasPos.y + height),
        IM_COL32(10, 10, 15, 255));

    u32 fw = analysis_.fftWidth, fh = analysis_.fftHeight;

    // Find max for normalization.
    f32 maxMag = 1e-10f;
    for (auto v : analysis_.fftMagnitude2D) maxMag = std::max(maxMag, v);

    float pixelW = width / static_cast<float>(fw);
    float pixelH = height / static_cast<float>(fh);

    // Draw spectrum as colored rectangles (downsampled).
    u32 stepX = std::max(1u, fw / static_cast<u32>(width));
    u32 stepY = std::max(1u, fh / static_cast<u32>(height));

    for (u32 y = 0; y < fh; y += stepY) {
        for (u32 x = 0; x < fw; x += stepX) {
            f32 val = std::log10(analysis_.fftMagnitude2D[y * fw + x] / maxMag + 1e-10f);
            val = (val + 4.0f) / 4.0f; // map -4..0 → 0..1
            val = std::clamp(val, 0.0f, 1.0f);

            u8 r = static_cast<u8>(val * val * 255);
            u8 g = static_cast<u8>(val * 200);
            u8 b = static_cast<u8>(std::sqrt(val) * 255);

            float px = static_cast<float>(x) / fw * width;
            float py = static_cast<float>(y) / fh * height;
            float pw = static_cast<float>(stepX) / fw * width + 1;
            float ph = static_cast<float>(stepY) / fh * height + 1;

            drawList->AddRectFilled(
                ImVec2(canvasPos.x + px, canvasPos.y + py),
                ImVec2(canvasPos.x + px + pw, canvasPos.y + py + ph),
                IM_COL32(r, g, b, 200));
        }
    }

    ImGui::Dummy(ImVec2(width, height));
}

} // namespace sky
