/// @file skydrop_app.cpp
/// @brief Skydrop player — simple image-to-music application.

#include "skydrop/app/skydrop_app.h"
#include "skydrop/ui/waveform_view.h"
#include "skydrop/ui/spectrum_view.h"
#include <tinyvk/assets/icons_font_awesome.h>
#include <imgui.h>
#include <cmath>

namespace sky {

// ── Lifecycle ────────────────────────────────────────────────────────────────

void SkydropApp::OnStart() {
    engine_.Init(kDefaultSampleRate, kDefaultBufferSize);
    engine_.SetGraph(&graph_);
    graph_.SetSampleRate(kDefaultSampleRate);
    graph_.SetBufferSize(kDefaultBufferSize);
}

void SkydropApp::OnUpdate() {
    // Audio streaming runs on its own thread — nothing to do here.
}

void SkydropApp::OnStop() {
    engine_.Shutdown();
    graph_.Clear();
    graphBuilt_ = false;
    synthId_ = reverbId_ = outputId_ = 0;
    imagePanel_.ReleaseTexture();
}

// ── Main UI ──────────────────────────────────────────────────────────────────

void SkydropApp::OnUI() {
    // Poll async load + analysis.
    imagePanel_.Poll();

    // When analysis just completed, apply data to synth and start playing.
    if (pendingApply_ && imagePanel_.IsAnalyzed()) {
        ApplyImage();
        pendingApply_ = false;
    }

    // Menu bar.
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN " Open Image...", "Ctrl+O"))
                OpenImage();
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Ctrl+Q")) Quit();
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // ── Left panel: Image ────────────────────────────────────────────────────
    ImGui::Begin(ICON_FA_IMAGE " Image");
    DrawImageSection();
    ImGui::End();

    // ── Right panel: Controls ────────────────────────────────────────────────
    ImGui::Begin(ICON_FA_SLIDERS " Controls");
    DrawControlsSection();
    ImGui::End();

    // ── Bottom panel: Transport + Visualizer ─────────────────────────────────
    ImGui::Begin(ICON_FA_MUSIC " Player");
    DrawTransportBar();
    ImGui::Separator();
    DrawVisualizerSection();
    ImGui::End();
}

// ── Image panel ──────────────────────────────────────────────────────────────

void SkydropApp::OpenImage() {
    tvk::FileFilter imageFilter{
        "Image Files",
        "*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif;*.heic;*.heif;*.webp;*.avif"
    };
    auto result = tvk::FileDialog::OpenFile({imageFilter});
    if (!result.has_value()) return;

    engine_.Stop();
    imagePanel_.LoadImage(result.value());

    // Build the graph now (fast), defer ApplyImage until bg decode+analysis finish.
    BuildGraph();
    pendingApply_ = true;
}

void SkydropApp::DrawImageSection() {
    if (!imagePanel_.HasImage()) {
        // Empty state.
        float availW = ImGui::GetContentRegionAvail().x;
        float availH = ImGui::GetContentRegionAvail().y;
        float cx = availW * 0.5f;
        float cy = availH * 0.4f;

        ImGui::SetCursorPosY(cy - 40);

        // Centered icon.
        auto textSize = ImGui::CalcTextSize(ICON_FA_IMAGE);
        ImGui::SetCursorPosX(cx - textSize.x * 0.5f);
        ImGui::TextColored(ImVec4(0.35f, 0.35f, 0.4f, 1.0f), ICON_FA_IMAGE);

        // Centered message.
        const char* msg = "Drop an image here or click below";
        textSize = ImGui::CalcTextSize(msg);
        ImGui::SetCursorPosX(cx - textSize.x * 0.5f);
        ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.5f, 1.0f), "%s", msg);

        ImGui::Spacing(); ImGui::Spacing();

        float btnW = 150.0f;
        ImGui::SetCursorPosX(cx - btnW * 0.5f);
        if (ImGui::Button(ICON_FA_FOLDER_OPEN " Open Image", ImVec2(btnW, 32)))
            OpenImage();

        return;
    }

    // Show the loaded image.
    auto* texture = imagePanel_.GetTexture();
    if (texture) {
        float availW = ImGui::GetContentRegionAvail().x;
        float aspect = static_cast<f32>(imagePanel_.GetImage().Height())
                     / static_cast<f32>(imagePanel_.GetImage().Width());
        float displayW = availW;
        float displayH = displayW * aspect;

        // Cap height to leave room for info.
        float maxH = ImGui::GetContentRegionAvail().y - 90;
        if (displayH > maxH && maxH > 50) {
            displayH = maxH;
            displayW = displayH / aspect;
        }

        // Center horizontally.
        float padX = (availW - displayW) * 0.5f;
        if (padX > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + padX);
        ImGui::Image(texture->GetImGuiTextureID(), ImVec2(displayW, displayH));
    }

    ImGui::Spacing();

    // Image info bar.
    auto& img = imagePanel_.GetImage();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.65f, 1.0f),
        "%ux%u  |  %u ch", img.Width(), img.Height(), img.NumChannels());

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 110);
    if (ImGui::SmallButton(ICON_FA_FOLDER_OPEN " Change..."))
        OpenImage();

    // Quick analysis summary.
    if (imagePanel_.IsLoading()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.7f, 0.6f, 0.3f, 1.0f),
            ICON_FA_SPINNER " Loading & analyzing...");
    } else if (imagePanel_.IsAnalyzed()) {
        auto& a = imagePanel_.GetAnalysis();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.6f, 1.0f),
            "Hue %.0f°  Sat %.0f%%  Bright %.0f%%  Edges %.0f%%",
            a.color.dominantHue,
            a.color.saturation * 100,
            a.color.brightness * 100,
            a.edges.density * 100);
    }
}

// ── Controls panel ───────────────────────────────────────────────────────────

void SkydropApp::DrawControlsSection() {
    // ── Algorithm selector ───────────────────────────────────────────────────
    ImGui::Text("Algorithm");
    ImGui::Spacing();

    int algoIdx = static_cast<int>(currentAlgo_);
    const char* algoNames[] = {
        "Ethereal", "Harmonic", "Rhythmic",
        "Melodic", "Cinematic"
    };
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##Algorithm", &algoIdx, algoNames, static_cast<int>(Algorithm::Count))) {
        currentAlgo_ = static_cast<Algorithm>(algoIdx);
        if (imagePanel_.HasImage()) {
            engine_.Stop();
            BuildGraph();
            if (imagePanel_.IsAnalyzed()) {
                ApplyImage();
            } else {
                pendingApply_ = true;
            }
        }
    }

    // Description.
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.55f, 1.0f), "%s",
        AlgorithmDescription(currentAlgo_));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Synth knobs ──────────────────────────────────────────────────────────
    ImGui::Text("Sound");
    ImGui::Spacing();
    DrawSynthKnobs();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // ── Master controls ──────────────────────────────────────────────────────
    ImGui::Text("Master");
    ImGui::Spacing();
    DrawMasterSection();
}

void SkydropApp::DrawSynthKnobs() {
    if (!graphBuilt_ || synthId_ == 0) {
        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.45f, 1.0f), "Load an image to see controls.");
        return;
    }

    auto* node = graph_.GetNode(synthId_);
    if (!node) return;

    auto& params = node->Params();
    ImGui::PushItemWidth(-1);

    for (u32 i = 0; i < static_cast<u32>(params.size()); ++i) {
        auto& p = params[i];
        f32 val = node->GetParam(i);

        ImGui::PushID(static_cast<int>(i));

        if (p.isEnum) {
            int intVal = static_cast<int>(val);
            std::vector<const char*> items;
            for (auto& lbl : p.enumLabels) items.push_back(lbl.c_str());
            ImGui::Text("%s", p.name.c_str());
            if (ImGui::Combo("##enum", &intVal, items.data(), static_cast<int>(items.size())))
                node->SetParam(i, static_cast<f32>(intVal));
        } else {
            ImGui::Text("%s", p.name.c_str());
            if (ImGui::SliderFloat("##val", &val, p.minValue, p.maxValue))
                node->SetParam(i, val);
        }

        ImGui::PopID();
    }

    ImGui::PopItemWidth();
}

void SkydropApp::DrawMasterSection() {
    ImGui::PushItemWidth(-1);

    // Volume.
    ImGui::Text("Volume");
    if (ImGui::SliderFloat("##vol", &masterVolume_, 0.0f, 1.0f, "%.0f%%")) {
        if (outputId_) {
            auto* outNode = graph_.GetNode(outputId_);
            if (outNode) outNode->SetParam(0, masterVolume_);
        }
    }

    // Reverb mix.
    ImGui::Text("Reverb");
    if (ImGui::SliderFloat("##revmix", &reverbMix_, 0.0f, 1.0f)) {
        if (reverbId_) {
            auto* revNode = graph_.GetNode(reverbId_);
            if (revNode) {
                i32 mixIdx = revNode->FindParam("Mix");
                if (mixIdx >= 0) revNode->SetParam(mixIdx, reverbMix_);
            }
        }
    }

    // Reverb room size.
    ImGui::Text("Room Size");
    if (ImGui::SliderFloat("##revroom", &reverbRoom_, 0.0f, 1.0f)) {
        if (reverbId_) {
            auto* revNode = graph_.GetNode(reverbId_);
            if (revNode) {
                i32 roomIdx = revNode->FindParam("RoomSize");
                if (roomIdx >= 0) revNode->SetParam(roomIdx, reverbRoom_);
            }
        }
    }

    ImGui::PopItemWidth();

    ImGui::Spacing();
    ImGui::Checkbox("Auto-play on load", &autoPlay_);
}

// ── Transport bar ────────────────────────────────────────────────────────────

void SkydropApp::DrawTransportBar() {
    bool playing = engine_.IsPlaying();
    bool hasImage = imagePanel_.HasImage();

    // Play / Pause / Stop buttons.
    bool canPlay = hasImage && imagePanel_.IsAnalyzed();
    ImGui::BeginDisabled(!canPlay);

    if (!playing) {
        if (ImGui::Button(ICON_FA_PLAY " Play", ImVec2(80, 30))) {
            if (!graphBuilt_) { BuildGraph(); ApplyImage(); }
            engine_.Play();
        }
    } else {
        if (ImGui::Button(ICON_FA_PAUSE " Pause", ImVec2(80, 30)))
            engine_.Pause();
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_STOP " Stop", ImVec2(80, 30)))
        engine_.Stop();

    ImGui::EndDisabled();

    // Level meters.
    ImGui::SameLine();
    ImGui::Spacing(); ImGui::SameLine();

    auto [peakL, peakR] = engine_.GetPeakLevel();
    float meterW = ImGui::GetContentRegionAvail().x - 10;
    if (meterW < 50) meterW = 50;

    ImGui::BeginGroup();
    ImGui::ProgressBar(peakL, ImVec2(meterW, 8), "");
    ImGui::ProgressBar(peakR, ImVec2(meterW, 8), "");
    ImGui::EndGroup();
}

// ── Visualization ────────────────────────────────────────────────────────────

void SkydropApp::DrawVisualizerSection() {
    auto& buf = engine_.GetLastOutputBuffer();

    ImGui::Spacing();
    WaveformView::DrawStereo(buf, 0, 80);

    ImGui::Spacing();
    SpectrumView::Draw(buf, engine_.GetSampleRate(), 0, 80);
}

// ── Internal graph builder ───────────────────────────────────────────────────

void SkydropApp::BuildGraph() {
    engine_.Stop();
    graph_.Clear();
    graphBuilt_ = false;
    synthId_ = reverbId_ = outputId_ = 0;

    // Create synth node based on selected algorithm.
    Ref<AudioNode> synthNode;
    switch (currentAlgo_) {
    case Algorithm::Ethereal:  synthNode = MakeRef<SpectralMapperNode>(); break;
    case Algorithm::Harmonic:  synthNode = MakeRef<ColorHarmonyNode>();   break;
    case Algorithm::Rhythmic:  synthNode = MakeRef<TextureRhythmNode>();  break;
    case Algorithm::Melodic:   synthNode = MakeRef<EdgeMelodyNode>();     break;
    case Algorithm::Cinematic: synthNode = MakeRef<RegionPadNode>();      break;
    default: return;
    }

    auto reverbNode = MakeRef<ReverbNode>();
    auto outputNode = MakeRef<OutputNode>();

    synthId_  = graph_.AddNode(synthNode);
    reverbId_ = graph_.AddNode(reverbNode);
    outputId_ = graph_.AddNode(outputNode);

    graph_.Connect(synthId_, 0, reverbId_, 0);
    graph_.Connect(reverbId_, 0, outputId_, 0);

    // Apply master settings.
    if (auto* outN = graph_.GetNode(outputId_))
        outN->SetParam(0, masterVolume_);
    if (auto* revN = graph_.GetNode(reverbId_)) {
        i32 mixIdx  = revN->FindParam("Mix");
        i32 roomIdx = revN->FindParam("RoomSize");
        if (mixIdx >= 0)  revN->SetParam(mixIdx, reverbMix_);
        if (roomIdx >= 0) revN->SetParam(roomIdx, reverbRoom_);
    }

    builtAlgo_ = currentAlgo_;
    graphBuilt_ = true;
}

void SkydropApp::ApplyImage() {
    if (!imagePanel_.HasImage() || !graphBuilt_) return;

    auto& analysis = imagePanel_.GetAnalysis();
    auto& image    = imagePanel_.GetImage();
    auto* node     = graph_.GetNode(synthId_);
    if (!node) return;

    // Stop audio while we mutate the synth node's composition data.
    bool wasPlaying = engine_.IsPlaying();
    engine_.Stop();

    // All synth nodes now use the unified SetImageData interface.
    switch (currentAlgo_) {
    case Algorithm::Ethereal:
        dynamic_cast<SpectralMapperNode*>(node)->SetImageData(image, analysis); break;
    case Algorithm::Harmonic:
        dynamic_cast<ColorHarmonyNode*>(node)->SetImageData(image, analysis);   break;
    case Algorithm::Rhythmic:
        dynamic_cast<TextureRhythmNode*>(node)->SetImageData(image, analysis);  break;
    case Algorithm::Melodic:
        dynamic_cast<EdgeMelodyNode*>(node)->SetImageData(image, analysis);     break;
    case Algorithm::Cinematic:
        dynamic_cast<RegionPadNode*>(node)->SetImageData(image, analysis);      break;
    default: break;
    }

    if (wasPlaying || autoPlay_) engine_.Play();
}

} // namespace sky
