#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

class TaskQueue {
 public:
  bool push(std::function<void()> task) {
    {
      std::lock_guard<std::mutex> lock(mutex_);

      if (closed_) {
        return false;
      }

      tasks_.push(std::move(task));
    }

    cv_.notify_one();
    return true;
  }

  bool pop(std::function<void()>& task) {
    std::unique_lock<std::mutex> lock(mutex_);

    cv_.wait(lock, [this]() { return closed_ || !tasks_.empty(); });

    if (tasks_.empty()) {
      return false;
    }

    task = std::move(tasks_.front());
    tasks_.pop();
    return true;
  }

  void close() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
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
      if (worker.joinable()) {
        worker.join();
      }
    }
  }

  template <typename F>
  auto submitTask(F func) -> std::future<decltype(func())> {
    using RetType = decltype(func());

    auto task =
        std::make_shared<std::packaged_task<RetType()>>(std::move(func));

    std::future<RetType> future = task->get_future();

    bool ok = queue_.push([task]() { (*task)(); });

    if (!ok) {
      throw std::runtime_error("ThreadPool is closed");
    }

    return future;
  }

 private:
  TaskQueue queue_;
  std::vector<std::thread> workers_;
};

int main() {
  ThreadPool pool(2);
  // test 1
  auto f1 = pool.submitTask([]() { return 100 + 100; });
  std::cout << f1.get() << std::endl;

  // test 2
  std::string name = "xizhe";
  auto f2 = pool.submitTask([name]() { return std::string("name: ") + name; });
  std::cout << f2.get() << std::endl;

  // test 3
  auto f3 = pool.submitTask([]() {
    std::cout << "thread id: " << std::this_thread::get_id() << std::endl;
  });
  f3.get();

  // test 4
  auto f4 = pool.submitTask([]() { throw std::runtime_error("task failed."); });

  try {
    f4.get();
  } catch (const std::exception& e) {
    std::cout << "catch exception: " << e.what() << std::endl;
  }

  return 0;
}