#ifndef TASK_THREAD_POOL_H
#define TASK_THREAD_POOL_H

#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <utility>      // For std::move and std::forward
#include <type_traits>  // For std::invoke_result
#include <atomic>       // For std::atomic
#include <string>       // For std::string in thread naming
#include <stdexcept>    // For std::runtime_error

// Platform-specific includes for thread naming
#if defined(__linux__) || defined(__APPLE__)
#include <pthread.h> // For pthread_setname_np
#elif defined(_WIN32)
#include <windows.h> // For SetThreadDescription
// For SetThreadDescription, you might need to link against Kernel32.lib
// and ensure you're compiling with a Windows SDK that supports it (Windows 10, version 1607+).
#endif


namespace RiftNet {
        namespace Threading {

            class TaskThreadPool {
            public:
                // Constructor: Initializes the thread pool with a specific number of threads
                // Defaults to the number of hardware concurrency units if numThreads is 0
                explicit TaskThreadPool(size_t numThreads = 0);

                // Destructor: Gracefully shuts down the thread pool
                ~TaskThreadPool();

                // Enqueues a task (a function or lambda) to be executed by a worker thread.
                // Returns a std::future<ReturnType> so the caller can get the result if needed.
                template<class F, class... Args>
                auto enqueue(F&& f, Args&&... args)
                    -> std::future<typename std::invoke_result<F, Args...>::type>;

                // Call stop to signal threads to stop processing new tasks and finish current ones.
                // This is useful for initiating shutdown before the destructor is called.
                void stop();

                // Pauses the processing of tasks in the queue. Ongoing tasks will complete.
                void pause();

                // Resumes the processing of tasks in the queue.
                void resume();

                // Clears all pending tasks from the queue.
                void clearQueue();

                // Get the number of worker threads in the pool
                size_t getThreadCount() const;

            private:
                // Worker function that each thread will execute
                void worker_loop();

                std::vector<std::thread> workers_;
                std::deque<std::function<void()>> tasks_; // Changed from std::queue to std::deque

                std::mutex queueMutex_;
                std::condition_variable condition_;
                std::atomic<bool> stop_{ false };      // Initialized to false
                std::atomic<bool> paused_{ false };    // New: Flag to signal threads to pause, initialized

                size_t threadCount_; // Store the number of threads
            };

            // --- Template Implementation for enqueue ---
            template<class F, class... Args>
            auto TaskThreadPool::enqueue(F&& f, Args&&... args)
                -> std::future<typename std::invoke_result<F, Args...>::type> {

                // Deduce the return type of the function F when called with Args...
                using ReturnType = typename std::invoke_result<F, Args...>::type;

                // Create a packaged_task to encapsulate the function and its arguments.
                auto task = std::make_shared<std::packaged_task<ReturnType()>>(
                    std::bind(std::forward<F>(f), std::forward<Args>(args)...)
                );

                // Get the future associated with the packaged_task.
                std::future<ReturnType> res = task->get_future();

                { // Lock scope for queue access
                    std::unique_lock<std::mutex> lock(queueMutex_);

                    // Don't allow enqueueing after stopping
                    if (stop_.load()) { // Use .load() for atomic bool
                        throw std::runtime_error("enqueue on stopped TaskThreadPool");
                    }

                    // Add the task to the queue.
                    tasks_.emplace_back([task]() { (*task)(); }); // Use emplace_back for deque
                } // Mutex is released here

                // Notify one waiting worker thread that a new task is available.
                condition_.notify_one();
                return res;
            }

        }
}
#endif // TASK_THREAD_POOL_H