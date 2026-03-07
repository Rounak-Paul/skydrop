#include "job_system.h"

#include <stdexcept>

// ---- Static member definitions ------------------------------------------

std::vector<std::thread> JobSystem::_workers;
std::queue<JobSystem::Task> JobSystem::_queue;
std::mutex               JobSystem::_queueMx;
std::condition_variable  JobSystem::_cv;
std::atomic<bool>        JobSystem::_stopping{ false };

// ---- Init / Shutdown ----------------------------------------------------

int JobSystem::Init(uint32_t workerCount) {
    if (!_workers.empty()) return -1;

    if (workerCount == 0) {
        uint32_t hw = std::thread::hardware_concurrency();
        workerCount = hw > 1 ? hw - 1 : 1;
    }

    _stopping.store(false, std::memory_order_release);
    _workers.reserve(workerCount);

    for (uint32_t i = 0; i < workerCount; ++i)
        _workers.emplace_back(&JobSystem::WorkerLoop);

    return 0;
}

void JobSystem::Shutdown() {
    {
        std::lock_guard lock(_queueMx);
        _stopping.store(true, std::memory_order_release);
    }
    _cv.notify_all();

    for (auto& t : _workers)
        if (t.joinable()) t.join();

    _workers.clear();
}

// ---- Submit -------------------------------------------------------------

void JobSystem::Submit(Job job) {
    {
        std::lock_guard lock(_queueMx);
        _queue.push({ std::move(job), nullptr });
    }
    _cv.notify_one();
}

void JobSystem::Submit(Job job, JobCounter& counter) {
    counter.pending.fetch_add(1, std::memory_order_release);
    {
        std::lock_guard lock(_queueMx);
        _queue.push({ std::move(job), &counter });
    }
    _cv.notify_one();
}

// ---- ParallelFor --------------------------------------------------------

std::future<void> JobSystem::ParallelFor(uint32_t count,
                                          std::function<void(uint32_t)> fn) {
    auto counter = std::make_shared<JobCounter>();

    for (uint32_t i = 0; i < count; ++i) {
        counter->pending.fetch_add(1, std::memory_order_release);
        Job job = [fn, i, counter]() {
            fn(i);
            if (counter->pending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::lock_guard lock(counter->mx);
                counter->cv.notify_all();
            }
        };
        {
            std::lock_guard lock(_queueMx);
            _queue.push({ std::move(job), nullptr }); // counter managed manually above
        }
        _cv.notify_one();
    }

    return std::async(std::launch::deferred, [counter] { counter->Wait(); });
}

// ---- Worker loop --------------------------------------------------------

void JobSystem::WorkerLoop() {
    while (true) {
        Task task;
        {
            std::unique_lock lock(_queueMx);
            _cv.wait(lock, [] {
                return _stopping.load(std::memory_order_acquire) || !_queue.empty();
            });

            if (_stopping.load(std::memory_order_acquire) && _queue.empty())
                return;

            task = std::move(_queue.front());
            _queue.pop();
        }

        task.job();

        if (task.counter) {
            if (task.counter->pending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::lock_guard lock(task.counter->mx);
                task.counter->cv.notify_all();
            }
        }
    }
}
