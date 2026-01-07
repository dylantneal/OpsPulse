#include "server/ThreadPool.hpp"
#include <iostream>

namespace opspulse {

ThreadPool::ThreadPool(size_t numThreads) {
    if (numThreads == 0) {
        numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 4;  // Fallback
    }

    workers_.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        workers_.emplace_back(&ThreadPool::workerLoop, this);
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::execute(std::function<void()> task) {
    if (!running_.load()) {
        throw std::runtime_error("ThreadPool is shutting down");
    }
    taskQueue_.push(std::move(task));
}

void ThreadPool::shutdown() {
    if (!running_.exchange(false)) {
        return;  // Already shut down
    }

    taskQueue_.close();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

void ThreadPool::workerLoop() {
    while (running_.load()) {
        std::function<void()> task;
        if (taskQueue_.pop(task)) {
            try {
                task();
            } catch (const std::exception& e) {
                std::cerr << "[ThreadPool] Worker caught exception: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[ThreadPool] Worker caught unknown exception" << std::endl;
            }
        }
    }

    // Drain remaining tasks on shutdown
    std::function<void()> task;
    while (taskQueue_.tryPop(task)) {
        try {
            task();
        } catch (...) {
            // Ignore exceptions during shutdown
        }
    }
}

} // namespace opspulse

