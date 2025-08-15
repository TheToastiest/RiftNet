// File: ThreadPool.cpp
#include "../../include/core/ThreadPool.hpp"
#include <iostream>
#include <stdexcept> // For std::runtime_error
#include <optional>  // For std::optional (Improvement #7)

// Platform-specific includes for thread naming (Improvement #6)
#if defined(__linux__)
#include <pthread.h> // For pthread_setname_np
#elif defined(__APPLE__)
#include <pthread.h> // For pthread_setname_np
#elif defined(_WIN32)
#include <windows.h> // For SetThreadDescription
// For SetThreadDescription, you might need to link against Kernel32.lib
// and ensure you're compiling with a Windows SDK that supports it (Windows 10, version 1607+).
#endif
namespace RiftNet {
        namespace Threading {

            // Constructor
            TaskThreadPool::TaskThreadPool(size_t numThreads)
                // stop_ and paused_ are initialized by their in-class initializers in the header.
            {
                if (numThreads == 0) {
                    threadCount_ = std::thread::hardware_concurrency();
                    if (threadCount_ == 0) { // Ensure at least one thread
                        threadCount_ = 1;
                    }
                }
                else {
                    threadCount_ = numThreads;
                }

                workers_.reserve(threadCount_); // Pre-allocate memory
                for (size_t i = 0; i < threadCount_; ++i) {
                    // Improvement #2: Cleaner thread creation
                    workers_.emplace_back(&TaskThreadPool::worker_loop, this);
                }
            }

            // Destructor
            TaskThreadPool::~TaskThreadPool() {
                stop(); // Gracefully stop and join threads
            }

            // Signal threads to stop, wait for current tasks to complete, and join threads.
            void TaskThreadPool::stop() {
                // Improvement #1: Simplified and safer stop logic
                bool already_stopped = stop_.exchange(true); // Atomically set stop_ to true and get old value
                if (already_stopped) {
                    // If stop() was already called and threads are joining/joined,
                    // we might just return or ensure joining is complete.
                    // The loop below will attempt to join, joinable() handles if already joined.
                }

                condition_.notify_all(); // Wake up all worker threads

                for (std::thread& worker : workers_) {
                    if (worker.joinable()) {
                        worker.join(); // Wait for the thread to finish
                    }
                }
                // All threads are joined here.
            }

            // Pause task processing
            void TaskThreadPool::pause() {
                // Improvement #3: Pause functionality
                paused_.store(true);
                // No notification needed here, threads will stop picking new tasks when they check paused_
            }

            // Resume task processing
            void TaskThreadPool::resume() {
                // Improvement #3: Resume functionality
                paused_.store(false);
                condition_.notify_all(); // Notify threads that they can resume processing
            }

            // Clear all pending tasks from the queue
            void TaskThreadPool::clearQueue() {
                // Improvement #4: Clear queue functionality
                std::unique_lock<std::mutex> lock(queueMutex_);
                // Improvement #5: tasks_ is std::deque, which has clear()
                tasks_.clear();
            }

            // Get the number of worker threads in the pool
            size_t TaskThreadPool::getThreadCount() const {
                return threadCount_;
            }

            // Worker function that each thread will execute
            void TaskThreadPool::worker_loop() {
                // Improvement #6: Thread Naming
#if defined(__linux__)
                pthread_setname_np(pthread_self(), "PoolWorker");
#elif defined(__APPLE__)
                pthread_setname_np("PoolWorker");
#elif defined(_WIN32)
    // HRESULT hr = SetThreadDescription(GetCurrentThread(), L"PoolWorker");
    // Note: SetThreadDescription is preferred but requires newer Windows SDK.
    // As a fallback or for wider compatibility, thread naming might be skipped on Windows
    // or use older techniques if necessary. For simplicity, we'll use a comment here.
    // Consider how to handle errors or if wide char names are needed.
#endif

                while (true) {
                    std::optional<std::function<void()>> task_opt; // Improvement #7

                    { // Lock scope for accessing the tasks queue
                        std::unique_lock<std::mutex> lock(queueMutex_);

                        // Improvement #3: Wait condition updated for pause functionality
                        // Improvement #7: stop_ and paused_ are atomic, use .load() for clarity
                        condition_.wait(lock, [this] {
                            return this->stop_.load() || (!this->paused_.load() && !this->tasks_.empty());
                            });

                        // If stop_ is true AND the queue is empty, then exit the loop.
                        // This check is after the wait, ensuring tasks are drained if stop_ was signaled.
                        if (this->stop_.load() && this->tasks_.empty()) {
                            return; // Exit the worker loop
                        }

                        // If paused, or (not stopping and queue is empty), continue waiting
                        // This specific condition is largely handled by the wait predicate,
                        // but an explicit check after wake-up can be useful.
                        // However, the current predicate should be sufficient.

                        // Improvement #5: tasks_ is std::deque
                        // Improvement #7: Minimized lock duration
                        if (!this->paused_.load() && !this->tasks_.empty()) {
                            task_opt = std::move(this->tasks_.front()); // Get task
                            this->tasks_.pop_front();                   // Remove from deque
                        }
                    } // Mutex is released here (critical for Improvement #7)

                    // If a task was successfully dequeued, execute it outside the lock.
                    if (task_opt) {
                        (*task_opt)(); // Execute the task
                    }
                }
            }
        }
    }