#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <atomic>
#include <stdexcept>

namespace opspulse {

/**
 * Thread-safe queue for work items.
 * Supports blocking pop and bounded capacity.
 */
template<typename T>
class ThreadSafeQueue {
public:
    explicit ThreadSafeQueue(size_t maxSize = 0) : maxSize_(maxSize) {}

    // Push an item (blocks if queue is full and maxSize > 0)
    bool push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (maxSize_ > 0) {
            notFull_.wait(lock, [this] {
                return closed_ || queue_.size() < maxSize_;
            });
        }
        if (closed_) return false;
        queue_.push(std::move(item));
        notEmpty_.notify_one();
        return true;
    }

    // Try push without blocking (returns false if full or closed)
    bool tryPush(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return false;
        if (maxSize_ > 0 && queue_.size() >= maxSize_) return false;
        queue_.push(std::move(item));
        notEmpty_.notify_one();
        return true;
    }

    // Pop an item (blocks until available or closed)
    bool pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        notEmpty_.wait(lock, [this] {
            return closed_ || !queue_.empty();
        });
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop();
        if (maxSize_ > 0) notFull_.notify_one();
        return true;
    }

    // Try pop without blocking
    bool tryPop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop();
        if (maxSize_ > 0) notFull_.notify_one();
        return true;
    }

    // Close the queue (wake up all waiters)
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

    bool isClosed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    size_t maxSize_;
    bool closed_ = false;
};

/**
 * Thread pool for executing tasks concurrently.
 * Uses a work-stealing approach for better load balancing.
 */
class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads = std::thread::hardware_concurrency());
    ~ThreadPool();

    // Non-copyable
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Submit a task and get a future for the result
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) 
        -> std::future<std::invoke_result_t<F, Args...>> 
    {
        using ReturnType = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<ReturnType> result = task->get_future();

        if (!taskQueue_.push([task]() { (*task)(); })) {
            throw std::runtime_error("ThreadPool is shutting down");
        }

        return result;
    }

    // Submit a task without waiting for result
    void execute(std::function<void()> task);

    // Gracefully shut down the pool
    void shutdown();

    // Get number of worker threads
    size_t size() const { return workers_.size(); }

    // Get number of pending tasks
    size_t pendingTasks() const { return taskQueue_.size(); }

private:
    std::vector<std::thread> workers_;
    ThreadSafeQueue<std::function<void()>> taskQueue_;
    std::atomic<bool> running_{true};

    void workerLoop();
};

} // namespace opspulse

