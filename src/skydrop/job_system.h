#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// A counter that tracks how many jobs belonging to a group are still in flight.
// Wait() blocks until the count reaches zero.
struct JobCounter {
    std::atomic<uint32_t>   pending{ 0 };
    std::condition_variable cv;
    std::mutex              mx;

    void Wait() {
        std::unique_lock lock(mx);
        cv.wait(lock, [this] { return pending.load(std::memory_order_acquire) == 0; });
    }
};

using Job = std::function<void()>;

class JobSystem {
public:
    // workerCount = 0  →  hardware_concurrency - 1  (leave one core for the main thread)
    static int  Init(uint32_t workerCount = 0);
    static void Shutdown();

    // Submit a fire-and-forget job.
    static void Submit(Job job);

    // Submit a job that decrements counter when finished.
    static void Submit(Job job, JobCounter& counter);

    // Convenience: submit N parallel jobs and return a future that is ready
    // when all have completed.  Each call receives its index in [0, count).
    static std::future<void> ParallelFor(uint32_t count,
                                         std::function<void(uint32_t)> fn);

    static uint32_t WorkerCount() { return static_cast<uint32_t>(_workers.size()); }

private:
    struct Task {
        Job          job;
        JobCounter*  counter = nullptr; // optional, decremented on completion
    };

    static void WorkerLoop();

    static std::vector<std::thread> _workers;
    static std::queue<Task>         _queue;
    static std::mutex               _queueMx;
    static std::condition_variable  _cv;
    static std::atomic<bool>        _stopping;
};
