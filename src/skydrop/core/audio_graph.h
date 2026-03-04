#pragma once

/// @file audio_graph.h
/// @brief Directed acyclic graph (DAG) of AudioNodes with topological evaluation.

#include "skydrop/core/audio_node.h"

#include <queue>
#include <set>
#include <stdexcept>

namespace sky {

/// Represents a connection from one node's output port to another's input port.
struct Connection {
    NodeId srcNode  = 0;
    u32    srcPort  = 0;
    NodeId dstNode  = 0;
    u32    dstPort  = 0;

    bool operator==(const Connection& o) const {
        return srcNode == o.srcNode && srcPort == o.srcPort &&
               dstNode == o.dstNode && dstPort == o.dstPort;
    }
};

/// The audio processing graph.
/// Nodes are added, connected, and then processed in topological order.
class AudioGraph {
public:
    AudioGraph() = default;
    ~AudioGraph() = default;

    // -- Configuration --------------------------------------------------------

    void SetSampleRate(u32 rate) { sampleRate_ = rate; }
    void SetBufferSize(u32 size) { bufferSize_ = size; }
    u32  GetSampleRate() const { return sampleRate_; }
    u32  GetBufferSize() const { return bufferSize_; }

    // -- Node management ------------------------------------------------------

    /// Add a node and return its ID.
    NodeId AddNode(Ref<AudioNode> node) {
        NodeId id = nextId_++;
        node->SetId(id);
        node->Init(sampleRate_, bufferSize_);
        if (node->Name().empty())
            node->SetName(std::string(node->TypeName()) + " " + std::to_string(id));
        nodes_[id] = std::move(node);
        dirty_ = true;
        return id;
    }

    /// Remove a node and all its connections.
    void RemoveNode(NodeId id) {
        // Remove connections involving this node.
        connections_.erase(
            std::remove_if(connections_.begin(), connections_.end(),
                [id](const Connection& c) { return c.srcNode == id || c.dstNode == id; }),
            connections_.end());
        nodes_.erase(id);
        dirty_ = true;
    }

    /// Retrieve a node by ID.
    AudioNode* GetNode(NodeId id) const {
        auto it = nodes_.find(id);
        return it != nodes_.end() ? it->second.get() : nullptr;
    }

    /// Get all nodes.
    const std::unordered_map<NodeId, Ref<AudioNode>>& Nodes() const { return nodes_; }

    // -- Connection management ------------------------------------------------

    /// Connect an output port of srcNode to an input port of dstNode.
    bool Connect(NodeId srcNode, u32 srcPort, NodeId dstNode, u32 dstPort) {
        // Validate nodes exist.
        if (!GetNode(srcNode) || !GetNode(dstNode)) return false;

        // Check for duplicate.
        Connection c{srcNode, srcPort, dstNode, dstPort};
        for (auto& existing : connections_)
            if (existing == c) return false;

        connections_.push_back(c);
        dirty_ = true;

        // Verify no cycle was introduced.
        if (!TopologicalSort()) {
            connections_.pop_back();
            dirty_ = true;
            return false;
        }
        return true;
    }

    /// Remove a specific connection.
    void Disconnect(NodeId srcNode, u32 srcPort, NodeId dstNode, u32 dstPort) {
        Connection c{srcNode, srcPort, dstNode, dstPort};
        connections_.erase(
            std::remove(connections_.begin(), connections_.end(), c),
            connections_.end());
        dirty_ = true;
    }

    /// Get all connections.
    const std::vector<Connection>& Connections() const { return connections_; }

    // -- Processing -----------------------------------------------------------

    /// Process one buffer through the entire graph.
    void Process() {
        if (dirty_) {
            if (!TopologicalSort()) return;
            AllocateBuffers();
            dirty_ = false;
        }

        // Process nodes in topological order.
        for (NodeId id : sortedOrder_) {
            auto* node = GetNode(id);
            if (!node) continue;

            // Gather input buffers for this node.
            std::vector<const SampleBuffer*> inputs(node->InputPorts().size(), nullptr);
            for (auto& conn : connections_) {
                if (conn.dstNode == id) {
                    auto it = outputBuffers_.find({conn.srcNode, conn.srcPort});
                    if (it != outputBuffers_.end())
                        inputs[conn.dstPort] = &it->second;
                }
            }

            // Gather output buffers.
            std::vector<SampleBuffer*> outputs;
            for (u32 p = 0; p < static_cast<u32>(node->OutputPorts().size()); ++p) {
                auto& buf = outputBuffers_[{id, p}];
                buf.Clear();
                outputs.push_back(&buf);
            }

            node->Process(inputs, outputs);
        }
    }

    /// Get the output buffer of a specific node+port after processing.
    const SampleBuffer* GetOutputBuffer(NodeId node, u32 port) const {
        auto it = outputBuffers_.find({node, port});
        return it != outputBuffers_.end() ? &it->second : nullptr;
    }

    /// Find the "output" node — the node with no outgoing connections to other nodes
    /// that has audio output ports. Returns 0 if none found.
    NodeId FindOutputNode() const {
        if (sortedOrder_.empty()) return 0;
        // The last node in topological order with output ports is typically the output.
        for (auto it = sortedOrder_.rbegin(); it != sortedOrder_.rend(); ++it) {
            auto* node = GetNode(*it);
            if (node && !node->OutputPorts().empty()) return *it;
        }
        return 0;
    }

    /// Reset all nodes.
    void ResetAll() {
        for (auto& [id, node] : nodes_) node->Reset();
    }

    /// Clear the entire graph.
    void Clear() {
        nodes_.clear();
        connections_.clear();
        outputBuffers_.clear();
        sortedOrder_.clear();
        dirty_ = true;
    }

private:
    /// Kahn's algorithm for topological sort. Returns false if a cycle is detected.
    bool TopologicalSort() {
        sortedOrder_.clear();
        if (nodes_.empty()) return true;

        // Build adjacency and in-degree.
        std::unordered_map<NodeId, std::set<NodeId>> adj;
        std::unordered_map<NodeId, u32> inDeg;
        for (auto& [id, _] : nodes_) { adj[id]; inDeg[id] = 0; }
        for (auto& c : connections_) {
            if (adj[c.srcNode].insert(c.dstNode).second)
                inDeg[c.dstNode]++;
        }

        std::queue<NodeId> q;
        for (auto& [id, deg] : inDeg)
            if (deg == 0) q.push(id);

        while (!q.empty()) {
            NodeId curr = q.front(); q.pop();
            sortedOrder_.push_back(curr);
            for (NodeId next : adj[curr]) {
                if (--inDeg[next] == 0) q.push(next);
            }
        }

        return sortedOrder_.size() == nodes_.size(); // false = cycle
    }

    /// Pre-allocate output buffers for every node output port.
    void AllocateBuffers() {
        outputBuffers_.clear();
        for (auto& [id, node] : nodes_) {
            for (u32 p = 0; p < static_cast<u32>(node->OutputPorts().size()); ++p) {
                outputBuffers_[{id, p}] = SampleBuffer(kDefaultChannels, bufferSize_);
            }
        }
    }

    // -- Data -----------------------------------------------------------------

    struct PortKey {
        NodeId node;
        u32    port;
        bool operator==(const PortKey& o) const { return node == o.node && port == o.port; }
    };
    struct PortKeyHash {
        size_t operator()(const PortKey& k) const {
            return std::hash<u64>()(static_cast<u64>(k.node) << 32 | k.port);
        }
    };

    std::unordered_map<NodeId, Ref<AudioNode>> nodes_;
    std::vector<Connection> connections_;
    std::unordered_map<PortKey, SampleBuffer, PortKeyHash> outputBuffers_;
    std::vector<NodeId> sortedOrder_;

    u32 sampleRate_ = kDefaultSampleRate;
    u32 bufferSize_ = kDefaultBufferSize;
    NodeId nextId_  = 1;
    bool dirty_     = true;
};

} // namespace sky
