#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// Fixed worker pool draining a FIFO job queue.
class JobQueue {
public:
    explicit JobQueue(int threads) {
        for (int i = 0; i < threads; ++i)
            workers_.emplace_back([this] { run(); });
    }
    ~JobQueue() {
        {
            std::lock_guard<std::mutex> l(m_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
    }
    JobQueue(const JobQueue&) = delete;
    JobQueue& operator=(const JobQueue&) = delete;

    void push(std::function<void()> job) {
        {
            std::lock_guard<std::mutex> l(m_);
            jobs_.push(std::move(job));
        }
        cv_.notify_one();
    }

private:
    void run() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock<std::mutex> l(m_);
                cv_.wait(l, [this] { return stop_ || !jobs_.empty(); });
                if (stop_ && jobs_.empty()) return;
                job = std::move(jobs_.front());
                jobs_.pop();
            }
            job();
        }
    }

    std::mutex m_;
    std::condition_variable cv_;
    std::queue<std::function<void()>> jobs_;
    std::vector<std::thread> workers_;
    bool stop_ = false;
};
