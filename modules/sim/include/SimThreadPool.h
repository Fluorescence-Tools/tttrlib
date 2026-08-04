/*!
 * \file SimThreadPool.h
 * \brief Minimal persistent std::thread pool with a chunked parallel_for (PRD-005).
 *
 * Explicit threading for the simulation engine — no OpenMP. Workers persist across
 * calls so per-window dispatch avoids thread-creation overhead. `parallel_for`
 * splits [0,n) into one contiguous range per worker and invokes the body with
 * (begin,end); callers use per-range buffers so no locking is needed. Header-only.
 * Additive; does not modify existing tttrlib.
 */
#ifndef TTTRLIB_SIMTHREADPOOL_H
#define TTTRLIB_SIMTHREADPOOL_H

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace tttrlib {

class SimThreadPool {
public:
    explicit SimThreadPool(unsigned n_threads = 0) {
        if (n_threads == 0) {
            unsigned hw = std::thread::hardware_concurrency();
            n_threads = hw > 1 ? hw : 1;
        }
        n_ = n_threads;
        for (unsigned i = 1; i < n_; ++i)
            workers_.emplace_back([this, i] { worker_loop(i); });
    }

    ~SimThreadPool() {
        { std::lock_guard<std::mutex> lk(m_); stop_ = true; }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
    }

    unsigned size() const { return n_; }

    /// Run body(begin,end,worker_index) over `n` items, split into `n_` contiguous ranges.
    void parallel_for(size_t n, const std::function<void(size_t, size_t, unsigned)>& body) {
        if (n == 0) return;
        if (n_ == 1) { body(0, n, 0); return; }

        body_ = &body;
        n_items_ = n;
        remaining_.store(int(n_) - 1);
        { std::lock_guard<std::mutex> lk(m_); generation_++; }
        cv_.notify_all();

        run_range(0);                                   // this thread does range 0

        std::unique_lock<std::mutex> lk(done_m_);
        done_cv_.wait(lk, [this] { return remaining_.load() == 0; });
    }

private:
    void run_range(unsigned idx) {
        size_t chunk = (n_items_ + n_ - 1) / n_;
        size_t b = size_t(idx) * chunk;
        size_t e = b + chunk; if (e > n_items_) e = n_items_;
        if (b < e) (*body_)(b, e, idx);
    }

    void worker_loop(unsigned idx) {
        uint64_t seen = 0;
        for (;;) {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [this, &seen] { return stop_ || generation_ != seen; });
            if (stop_) return;
            seen = generation_;
            lk.unlock();

            run_range(idx);

            if (remaining_.fetch_sub(1) - 1 == 0) {
                std::lock_guard<std::mutex> dlk(done_m_);
                done_cv_.notify_one();
            }
        }
    }

    unsigned n_ = 1;
    std::vector<std::thread> workers_;

    std::mutex m_;
    std::condition_variable cv_;
    uint64_t generation_ = 0;
    bool stop_ = false;

    const std::function<void(size_t, size_t, unsigned)>* body_ = nullptr;
    size_t n_items_ = 0;
    std::atomic<int> remaining_{0};
    std::mutex done_m_;
    std::condition_variable done_cv_;
};

} // namespace tttrlib

#endif // TTTRLIB_SIMTHREADPOOL_H
