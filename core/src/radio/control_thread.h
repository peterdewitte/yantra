// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The control thread of one radio plugin (RAD-15: control functions from
// one Yantra worker thread at a time, never the UI thread). run() hands a
// call to the thread and waits for it at most `limit`; a call that takes
// longer leaves the thread "stuck" (PLG-25: a crashed call) until it
// returns, and every run() meanwhile is refused at once. The destructor
// joins the thread, or detaches it while a call is still stuck (the
// shared state outlives it).
#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace yantra::radio::detail {

class ControlThread {
public:
    ControlThread() : s_(std::make_shared<State>()) {
        thread_ = std::thread([s = s_] { main(*s); });
    }
    ~ControlThread() {
        bool stuck = false;
        {
            const std::lock_guard lock(s_->mutex);
            s_->stop = true;
            stuck = s_->running;
        }
        s_->cv.notify_all();
        if (stuck)
            thread_.detach();
        else
            thread_.join();
    }
    ControlThread(const ControlThread&) = delete;
    ControlThread& operator=(const ControlThread&) = delete;

    enum class Result { Done, Timeout, Refused };

    // Runs `f` on the control thread (or directly when called from it) and
    // returns its value in `out` when it finished in time. `f` must capture
    // by value: after a timeout it may still run after run() returned.
    template <class R, class F>
    Result call(F f, std::chrono::milliseconds limit, R& out) {
        auto result = std::make_shared<R>();
        const Result r = run([f, result]() mutable { *result = f(); }, limit);
        if (r == Result::Done) out = *result;
        return r;
    }

    // Runs `f` (capturing by value, see call) on the control thread.
    Result run(const std::function<void()>& f, std::chrono::milliseconds limit) {
        if (std::this_thread::get_id() == thread_.get_id()) {
            f();
            return Result::Done;
        }
        auto task = std::make_shared<Task>();
        task->fn = f;
        std::unique_lock lock(s_->mutex);
        if (s_->stop || s_->running_late) return Result::Refused;
        s_->queue.push_back(task);
        s_->cv.notify_all();
        if (!s_->cv.wait_for(lock, limit, [&] { return task->done; })) {
            // Still queued or running: the thread is stuck in a call.
            s_->running_late = true;
            return Result::Timeout;
        }
        return Result::Done;
    }

    // True while a call that timed out has not returned yet.
    bool stuck() const {
        const std::lock_guard lock(s_->mutex);
        return s_->running_late;
    }

    // Waits until a stuck call returned (tests release a hanging plugin).
    bool wait_unstuck(std::chrono::milliseconds timeout) const {
        std::unique_lock lock(s_->mutex);
        return s_->cv.wait_for(lock, timeout, [&] { return !s_->running_late; });
    }

private:
    struct Task {
        std::function<void()> fn;
        bool done = false;
    };
    struct State {
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<std::shared_ptr<Task>> queue;
        bool stop = false;
        bool running = false;
        bool running_late = false;
    };

    static void main(State& s) {
        std::unique_lock lock(s.mutex);
        for (;;) {
            s.cv.wait(lock, [&] { return s.stop || !s.queue.empty(); });
            if (s.queue.empty()) return;  // stop
            std::shared_ptr<Task> task = s.queue.front();
            s.queue.pop_front();
            s.running = true;
            lock.unlock();
            task->fn();
            lock.lock();
            s.running = false;
            task->done = true;
            if (s.queue.empty()) s.running_late = false;
            s.cv.notify_all();
            if (s.stop && s.queue.empty()) return;
        }
    }

    std::shared_ptr<State> s_;
    std::thread thread_;
};

}  // namespace yantra::radio::detail
