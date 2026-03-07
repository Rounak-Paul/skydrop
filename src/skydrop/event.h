#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <vector>

using ListenerID = uint32_t;

// ---- Event dispatcher ---------------------------------------------------

class Event {
public:
    static int  Init();
    static void Shutdown();

    // Register a typed callback. Returns an ID used to Unregister.
    template<typename T>
    static ListenerID Register(std::function<void(const T&)> callback) {
        std::lock_guard<std::mutex> lk(_instance->_mutex);
        ListenerID id = _instance->_nextID++;
        _instance->_listeners[typeid(T)].emplace_back(
            id, [cb = std::move(callback)](const void* data) {
                cb(*static_cast<const T*>(data));
            });
        return id;
    }

    // Remove a previously registered callback.
    template<typename T>
    static void Unregister(ListenerID id) {
        std::lock_guard<std::mutex> lk(_instance->_mutex);
        auto it = _instance->_listeners.find(typeid(T));
        if (it == _instance->_listeners.end()) return;
        auto& vec = it->second;
        vec.erase(
            std::remove_if(vec.begin(), vec.end(),
                           [id](const auto& p) { return p.first == id; }),
            vec.end());
    }

    // Fire all callbacks registered for T.
    // Safe to call from any thread — listeners are invoked while the mutex
    // is NOT held, so callbacks can safely call Register/Unregister/Emit.
    template<typename T>
    static void Emit(const T& data) {
        std::vector<std::pair<ListenerID, std::function<void(const void*)>>> snapshot;
        {
            std::lock_guard<std::mutex> lk(_instance->_mutex);
            auto it = _instance->_listeners.find(typeid(T));
            if (it == _instance->_listeners.end()) return;
            snapshot = it->second;
        }
        for (const auto& [id, callback] : snapshot)
            callback(static_cast<const void*>(&data));
    }

private:
    using ErasedCallback = std::function<void(const void*)>;

    static Event* _instance;

    Event()  = default;
    ~Event() = default;

    std::mutex _mutex;
    ListenerID _nextID = 1;
    std::unordered_map<std::type_index,
                       std::vector<std::pair<ListenerID, ErasedCallback>>>
        _listeners;
};