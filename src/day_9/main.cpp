#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>

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

  template <typename F>
  auto submitTask(F func) -> std::future<decltype(func())> {
    using RetType = decltype(func());

    auto task =
        std::make_shared<std::packaged_task<RetType()>>(std::move(func));

    std::future<RetType> future_ = task->get_future();

    bool ret = queue_.push([task]() { (*task)(); });

    if (!ret) throw std::runtime_error("queue close.");

    return future_;
  }

  ~ThreadPool() {
    queue_.close();

    for (auto& worker : workers_) {
      if (worker.joinable()) worker.join();
    }
  }

 private:
  TaskQueue queue_;
  std::vector<std::thread> workers_;
};

int main() {
  ThreadPool pool(2);

  auto f1 = pool.submitTask([]() { return 100 + 200; });

  auto f2 = pool.submitTask([]() { return std::string("hello"); });

  auto f3 = pool.submitTask([]() { std::cout << "void task running\n"; });

  std::cout << f1.get() << std::endl;
  std::cout << f2.get() << std::endl;
  f3.get();

  return 0;
}