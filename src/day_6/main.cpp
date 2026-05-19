#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>

class BlockingQueue {
 public:
  bool push(int value) {
    {
      std::lock_guard<std::mutex> lk(mutex_);

      if (closed_) return false;

      queue_.push(value);
    }

    cv_.notify_one();
    return true;
  }

  bool pop(int& value) {
    std::unique_lock<std::mutex> ul(mutex_);

    cv_.wait(ul, [this]() { return closed_ || !queue_.empty(); });

    if (queue_.empty()) return false;

    value = queue_.front();
    queue_.pop();
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
  std::queue<int> queue_;
  bool closed_ = false;
};

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

#if 0
int main() {
  BlockingQueue bq;

  std::thread consumer([&bq]() {
    int value = 0;

    while (bq.pop(value)) {
      std::cout << value << std::endl;
    }

    std::cout << "consumer exit" << std::endl;
  });

  std::thread producer([&bq]() {
    for (int i = 0; i < 5; ++i) {
      bq.push(i);
    }

    bq.close();
  });

  producer.join();
  consumer.join();

  return 0;
}

#endif

int main() {
  TaskQueue queue;

  std::thread workerThread([&queue]() {
    std::function<void()> task;

    while (queue.pop(task)) {
      task();
    }

    std::cout << "worker exit\n";
  });

  queue.push([]() { std::cout << "task 1\n"; });

  queue.push([]() { std::cout << "task 2\n"; });

  queue.push([]() { std::cout << "task 3\n"; });

  queue.close();

  workerThread.join();

  return 0;
}