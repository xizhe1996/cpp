#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

class SafeCounter {
 public:
  void increment() {
    std::lock_guard<std::mutex> lk(mutex_);
    ++count_;
  }

  int value() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return count_;
  }

 private:
  int count_ = 0;
  mutable std::mutex mutex_;
};

int main() {
  SafeCounter counter;

  std::vector<std::thread> threads;

  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([&counter]() {
      for (int j = 0; j < 100000; ++j) {
        counter.increment();
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  std::cout << "counter = " << counter.value() << std::endl;

  return 0;
}