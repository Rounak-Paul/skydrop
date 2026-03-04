#pragma once

/// @file audio_node.h
/// @brief Base class for all audio processing nodes in the graph.

#include "skydrop/core/types.h"
#include "skydrop/core/sample_buffer.h"

namespace sky {

// ---------------------------------------------------------------------------
// Port descriptors
// ---------------------------------------------------------------------------

enum class PortType : u8 { Audio, Control, Trigger };
enum class PortDir  : u8 { Input, Output };

struct PortDesc {
    std::string name;
    PortType    type = PortType::Audio;
    PortDir     dir  = PortDir::Input;
    f32         defaultValue = 0.0f;  ///< For control ports.
    f32         minValue     = 0.0f;
    f32         maxValue     = 1.0f;
};

// ---------------------------------------------------------------------------
// Node parameter
// ---------------------------------------------------------------------------

struct NodeParam {
    std::string name;
    f32  value       = 0.0f;
    f32  minValue    = 0.0f;
    f32  maxValue    = 1.0f;
    f32  defaultValue = 0.0f;
    bool isEnum      = false;  ///< If true, value is an integer selector.
    std::vector<std::string> enumLabels;
};

// ---------------------------------------------------------------------------
// AudioNode base class
// ---------------------------------------------------------------------------

/// Unique node identifier.
using NodeId = u32;

/// Base class for all nodes that live in the audio graph.
/// Each node can have input/output ports and parameters.
class AudioNode {
public:
    virtual ~AudioNode() = default;

    /// Human-readable type name (e.g. "Oscillator", "ImageSource").
    virtual const char* TypeName() const = 0;

    /// Category for grouping in the UI (e.g. "Source", "Effect", "Image").
    virtual const char* Category() const = 0;

    /// Called once when the node is added to the graph.
    virtual void Init(u32 sampleRate, u32 bufferSize) {
        sampleRate_ = sampleRate;
        bufferSize_ = bufferSize;
    }

    /// Process one buffer of audio. Subclasses implement this.
    /// @param inputs  One buffer per input port (may be empty / null for unconnected).
    /// @param outputs One buffer per output port (pre-allocated by the graph).
    virtual void Process(const std::vector<const SampleBuffer*>& inputs,
                         std::vector<SampleBuffer*>& outputs) = 0;

    /// Reset internal state (e.g. clear delay lines).
    virtual void Reset() {}

    // -- Port access ----------------------------------------------------------

    const std::vector<PortDesc>& InputPorts()  const { return inputPorts_; }
    const std::vector<PortDesc>& OutputPorts() const { return outputPorts_; }

    // -- Parameter access -----------------------------------------------------

    const std::vector<NodeParam>& Params() const { return params_; }

    f32 GetParam(u32 index) const {
        assert(index < params_.size());
        return params_[index].value;
    }

    void SetParam(u32 index, f32 value) {
        assert(index < params_.size());
        params_[index].value = Clamp(value, params_[index].minValue, params_[index].maxValue);
    }

    /// Find a parameter index by name. Returns -1 if not found.
    i32 FindParam(const std::string& name) const {
        for (i32 i = 0; i < static_cast<i32>(params_.size()); ++i)
            if (params_[i].name == name) return i;
        return -1;
    }

    // -- Identity -------------------------------------------------------------

    NodeId      Id()   const { return id_; }
    void        SetId(NodeId id) { id_ = id; }
    std::string Name() const { return name_; }
    void        SetName(const std::string& n) { name_ = n; }

    // -- UI position (for node editor) ----------------------------------------
    f32 posX = 0.0f;
    f32 posY = 0.0f;

protected:
    /// Subclasses call these in their constructor to declare ports and params.
    void AddInput(const std::string& name, PortType type = PortType::Audio) {
        inputPorts_.push_back({name, type, PortDir::Input});
    }

    void AddOutput(const std::string& name, PortType type = PortType::Audio) {
        outputPorts_.push_back({name, type, PortDir::Output});
    }

    void AddParam(const std::string& name, f32 defaultVal, f32 minVal, f32 maxVal) {
        params_.push_back({name, defaultVal, minVal, maxVal, defaultVal});
    }

    void AddEnumParam(const std::string& name, const std::vector<std::string>& labels, i32 defaultIdx = 0) {
        NodeParam p;
        p.name = name;
        p.value = static_cast<f32>(defaultIdx);
        p.minValue = 0.0f;
        p.maxValue = static_cast<f32>(labels.size() - 1);
        p.defaultValue = static_cast<f32>(defaultIdx);
        p.isEnum = true;
        p.enumLabels = labels;
        params_.push_back(std::move(p));
    }

    u32 sampleRate_ = kDefaultSampleRate;
    u32 bufferSize_ = kDefaultBufferSize;

private:
    NodeId      id_   = 0;
    std::string name_;
    std::vector<PortDesc> inputPorts_;
    std::vector<PortDesc> outputPorts_;
    std::vector<NodeParam> params_;
};

} // namespace sky
