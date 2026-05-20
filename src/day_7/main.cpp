#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class TaskQueue {
 public:
  bool push(std::function<void()> task) {
    {
      std::lock_guard<std::mutex> lg(mutex_);

      if (closed_) return false;

      tasks_.push(std::move(task));
    }

    cv_.notify_one();
    return true;
  }

  bool pop(std::function<void()>& task) {
    std::unique_lock<std::mutex> ul(mutex_);

    cv_.wait(ul, [this]() { return closed_ || !tasks_.empty(); });

    if (tasks_.empty()) return false;

    task = std::move(tasks_.front());
    tasks_.pop();
    return true;
  }

  void close() {
    {
      std::lock_guard<std::mutex> lg(mutex_);
      closed_ = true;
    }
    cv_.notify_all();
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<std::function<void()>> tasks_;
  bool closed_ = false;
};

class ThreadPool {
 public:
  explicit ThreadPool(size_t threadCount) {
    for (size_t i = 0; i < threadCount; ++i) {
      workers_.emplace_back([this]() {
        std::function<void()> task;
        while (queue_.pop(task)) {
          task();
        }
      });
    }
  }

  ~ThreadPool() {
    queue_.close();

    for (auto& worker : workers_) {
      if (worker.joinable()) worker.join();
    }
  }

  bool submit(std::function<void()> task) {
    return queue_.push(std::move(task));
  }

 private:
  TaskQueue queue_;
  std::vector<std::thread> workers_;
};

int main() {
  ThreadPool thread_pool(3);

  // only debug
  std::mutex cout_mutex;

  for (int i = 0; i < 10; ++i) {
    thread_pool.submit([i, &cout_mutex]() {
      std::lock_guard<std::mutex> lg(cout_mutex);
      std::cout << "i:" << i << "thread_id:" << std::this_thread::get_id()
                << std::endl;
    });
  }

  std::this_thread::sleep_for(std::chrono::seconds(1));

  return 0;
}