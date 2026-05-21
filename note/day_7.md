# Day 7｜最小线程池 ThreadPool：TaskQueue + worker + submit + shutdown

# 1. 今日学习目标

Day 7 的核心目标：

- 理解线程池为什么存在
- 掌握线程池的基本组成
- 理解 `TaskQueue` 和 `ThreadPool` 的关系
- 实现一个最小可用线程池
- 掌握 worker 线程循环
- 掌握 `submit` 提交任务逻辑
- 掌握线程池析构时的关闭流程
- 理解 graceful shutdown
- 能够回答线程池基础面试问题

核心主线：

> 线程池的本质是：用固定数量的 worker 线程，循环消费任务队列中的任务，并在关闭时通过 close + join 正确回收线程资源。

---

# 2. 为什么需要线程池？

如果每来一个任务都创建一个线程：

```cpp
std::thread t(task);
t.detach();
````

会有几个问题：

```text
1. 创建线程有开销
2. 线程数量不可控
3. detach 后生命周期难管理
4. 大量线程会导致调度开销增加
5. 任务执行完成后线程就销毁，复用性差
```

线程池的思路是：

```text
提前创建固定数量的 worker 线程。
任务来了放入任务队列。
worker 从任务队列中取任务执行。
```

好处：

```text
减少线程频繁创建 / 销毁的开销。
控制线程数量。
降低调度压力。
线程生命周期更容易管理。
任务执行和任务提交解耦。
```

---

# 3. 最小线程池的核心组成

一个最小线程池通常包含两个核心组件：

```text
1. 任务队列 TaskQueue
2. worker 线程数组 workers_
```

代码结构：

```cpp
class ThreadPool {
public:
    explicit ThreadPool(size_t threadCount);
    ~ThreadPool();

    bool submit(std::function<void()> task);

private:
    TaskQueue queue_;
    std::vector<std::thread> workers_;
};
```

其中：

```text
queue_：
    保存待执行任务
    支持 push / pop / close
    负责阻塞等待和关闭唤醒

workers_：
    保存所有 worker 线程
    构造时创建
    析构时 join
```

---

# 4. TaskQueue 回顾

Day 6 中已经实现了任务队列：

```cpp
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>

class TaskQueue {
 public:
  bool push(std::function<void()> task) {
    {
      std::lock_guard<std::mutex> lg(mutex_);

      if (closed_) {
        return false;
      }

      tasks_.push(std::move(task));
    }

    cv_.notify_one();
    return true;
  }

  bool pop(std::function<void()>& task) {
    std::unique_lock<std::mutex> ul(mutex_);

    cv_.wait(ul, [this]() {
      return closed_ || !tasks_.empty();
    });

    if (tasks_.empty()) {
      return false;
    }

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
```

---

## 4.1 TaskQueue 的语义

### push

```cpp
bool push(std::function<void()> task);
```

语义：

```text
队列未关闭：
    插入任务
    notify_one 唤醒一个 worker
    返回 true

队列已关闭：
    不再接受任务
    返回 false
```

---

### pop

```cpp
bool pop(std::function<void()>& task);
```

语义：

```text
队列有任务：
    取出任务
    返回 true

队列为空但未关闭：
    阻塞等待

队列为空且已关闭：
    返回 false
```

---

### close

```cpp
void close();
```

语义：

```text
标记队列关闭。
不再接收新任务。
notify_all 唤醒所有等待中的 worker。
```

---

## 4.2 pop 的等待条件

```cpp
cv_.wait(ul, [this]() {
    return closed_ || !tasks_.empty();
});
```

等待条件是：

```text
closed_ || !tasks_.empty()
```

含义：

```text
队列有任务：
    worker 可以取任务执行。

队列关闭：
    即使没有任务，也要唤醒 worker，让 worker 有机会退出。
```

---

## 4.3 close 后是否丢弃任务？

当前设计不会丢弃已经入队的任务。

因为：

```cpp
if (tasks_.empty()) {
    return false;
}

task = std::move(tasks_.front());
tasks_.pop();
return true;
```

如果：

```text
closed_ == true
tasks_ 非空
```

worker 仍然会继续取任务执行。

只有当：

```text
closed_ == true
tasks_ 为空
```

`pop` 才返回 `false`。

所以当前队列关闭语义是：

```text
不再接收新任务。
继续执行已有任务。
任务全部执行完后，worker 退出。
```

这就是 graceful shutdown 的基础。

---

# 5. worker 线程核心循环

线程池 worker 的核心循环是：

```cpp
std::function<void()> task;

while (queue_.pop(task)) {
    task();
}
```

含义：

```text
pop 成功：
    取到任务
    执行 task()

pop 失败：
    队列已经关闭且没有剩余任务
    worker 退出循环
```

这段代码是线程池的核心。

---

## 5.1 为什么要 while 循环？

worker 线程不是只执行一个任务就退出，而是应该持续复用：

```text
有任务就执行。
没有任务就阻塞等待。
队列关闭且没有任务时退出。
```

所以要写：

```cpp
while (queue_.pop(task)) {
    task();
}
```

不能只写一次：

```cpp
if (queue_.pop(task)) {
    task();
}
```

否则 worker 只会执行一个任务就退出，线程池就失去了复用线程的意义。

---

## 5.2 task() 为什么在锁外执行？

`queue_.pop(task)` 内部只在取任务时加锁。

当 `pop` 返回后，`TaskQueue` 内部的锁已经释放。

所以：

```cpp
while (queue_.pop(task)) {
    task();
}
```

这里的：

```cpp
task();
```

是在锁外执行的。

这是正确设计。

原因：

```text
锁只应该保护任务队列的 push/pop 操作。
任务执行可能很耗时。
如果持锁执行 task()：
    其他线程不能 submit 新任务
    其他 worker 不能 pop 任务
    队列并发度会严重下降
    甚至可能引发死锁
```

原则：

```text
锁只保护共享数据结构，不包裹耗时业务逻辑。
```

---

# 6. ThreadPool 类结构

最小线程池：

```cpp
class ThreadPool {
 public:
  explicit ThreadPool(size_t threadCount);

  ~ThreadPool();

  bool submit(std::function<void()> task);

 private:
  TaskQueue queue_;
  std::vector<std::thread> workers_;
};
```

需要的头文件：

```cpp
#include <functional>
#include <thread>
#include <vector>
```

---

# 7. 构造函数

## 7.1 构造函数职责

```cpp
explicit ThreadPool(size_t threadCount);
```

构造函数负责：

```text
创建 threadCount 个 worker 线程。
每个 worker 线程执行 worker loop。
```

---

## 7.2 构造函数实现

```cpp
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
```

这里：

```cpp
workers_.emplace_back([this]() {
    ...
});
```

表示：

```text
创建一个新的 std::thread。
线程函数是一个 lambda。
lambda 捕获 this，用来访问成员变量 queue_。
```

---

## 7.3 为什么捕获 this？

worker 线程需要访问成员变量：

```cpp
queue_
```

所以 lambda 捕获：

```cpp
[this]
```

不过要注意：

```text
线程中捕获 this 有生命周期风险。
```

在当前设计中，这个风险通过析构函数控制：

```text
ThreadPool 析构时先 close 队列。
然后 join 所有 worker。
确保 worker 线程退出后，ThreadPool 对象才真正析构完成。
```

所以当前使用 `[this]` 是合理的。

---

# 8. submit 提交任务

## 8.1 submit 职责

```cpp
bool submit(std::function<void()> task);
```

作用：

```text
将任务提交到任务队列。
```

---

## 8.2 submit 实现

```cpp
bool submit(std::function<void()> task) {
  return queue_.push(std::move(task));
}
```

语义：

```text
如果队列未关闭，任务入队，返回 true。
如果队列已关闭，入队失败，返回 false。
```

---

## 8.3 为什么 submit 中使用 std::move？

```cpp
return queue_.push(std::move(task));
```

原因：

```text
task 是 submit 函数按值接收的参数。
进入 submit 后，task 已经是当前函数自己的局部对象。
放入队列后，submit 不再需要它。
所以可以 move 到 queue_ 中，减少 std::function 的拷贝成本。
```

`std::function<void()>` 可能内部持有：

```text
lambda
bind 对象
捕获数据
其他可调用对象
```

移动通常比拷贝更合适。

---

# 9. 析构函数

## 9.1 析构函数职责

```cpp
~ThreadPool();
```

析构函数负责：

```text
关闭任务队列。
唤醒所有 worker。
等待所有 worker 线程退出。
```

---

## 9.2 析构函数实现

```cpp
~ThreadPool() {
  queue_.close();

  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}
```

---

## 9.3 为什么先 close 再 join？

worker 线程可能正阻塞在：

```cpp
queue_.pop(task);
```

如果析构函数直接：

```cpp
worker.join();
```

但没有先关闭队列，那么：

```text
worker 一直睡眠等待任务。
析构函数一直等待 worker 退出。
程序卡死。
```

正确流程：

```text
1. queue_.close()
2. close 内部设置 closed_ = true
3. close 内部 notify_all 唤醒所有 worker
4. worker 从 pop 中醒来
5. 如果队列已空且关闭，pop 返回 false
6. worker 退出循环
7. 析构函数 join 成功返回
```

所以：

```text
析构时必须先 close，再 join。
```

---

# 10. ThreadPool 和 RAII

ThreadPool 也是 RAII 思想的体现。

```text
构造函数：
    创建 worker 线程资源。

析构函数：
    close 队列。
    join worker。
    回收线程资源。
```

也就是说：

```text
线程资源的生命周期绑定到 ThreadPool 对象生命周期。
```

这就是典型 RAII。

---

# 11. graceful shutdown

当前线程池关闭方式属于：

```text
graceful shutdown
```

也就是：

```text
不再接收新任务。
继续执行已经入队的任务。
任务执行完后 worker 退出。
析构函数 join 所有 worker。
```

---

## 11.1 为什么不会丢弃已入队任务？

因为 `TaskQueue::pop` 的逻辑是：

```cpp
cv_.wait(ul, [this]() {
    return closed_ || !tasks_.empty();
});

if (tasks_.empty()) {
    return false;
}

task = std::move(tasks_.front());
tasks_.pop();
return true;
```

当：

```text
closed_ == true
tasks_ 非空
```

worker 仍然会取任务执行。

只有：

```text
closed_ == true
tasks_ 为空
```

才返回 `false`。

所以已入队任务不会因为 close 被直接丢弃。

---

## 11.2 immediate shutdown 是什么？

与 graceful shutdown 相对的是：

```text
immediate shutdown
```

语义是：

```text
立即停止。
可能丢弃队列中未执行任务。
尽快让 worker 退出。
```

当前版本没有实现 immediate shutdown。

---

# 12. main 中是否需要 sleep？

测试代码中有时会写：

```cpp
std::this_thread::sleep_for(std::chrono::seconds(1));
```

它不是线程池正确性必须的。

它只是：

```text
方便观察任务执行输出。
```

从正确性上说：

```text
main 结束时，ThreadPool 析构。
析构函数会 close 队列并 join worker。
已成功入队的任务会被执行完。
```

所以即使不 sleep，当前 graceful shutdown 设计也应该能保证已提交任务执行完。

---

# 13. 当前版本的限制

当前最小线程池暂时不支持：

```text
1. 带返回值的任务
2. std::future 获取任务结果
3. 任务异常传递
4. 任务优先级
5. 动态扩缩容
6. 取消正在执行的任务
7. immediate shutdown
8. 析构期间并发 submit 的复杂生命周期保护
```

当前版本只支持：

```cpp
bool submit(std::function<void()> task);
```

也就是无返回值任务。

---

# 14. 析构期间并发 submit 的问题

如果一个线程正在析构 `ThreadPool`，另一个线程还在调用：

```cpp
pool.submit(...);
```

这是不安全的。

这属于：

```text
对象生命周期管理问题。
```

当前最小版本假设：

```text
调用方保证 ThreadPool 对象析构期间，不再有其他线程并发 submit。
```

如果要支持更严格的并发关闭，需要额外设计：

```text
running / stopping 状态
submit 与 shutdown 的同步
外部生命周期约束
shared_ptr 管理 ThreadPool 生命周期
```

这不是当前最小版本的重点。

---

# 15. 当前完整代码

```cpp
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

      if (closed_) {
        return false;
      }

      tasks_.push(std::move(task));
    }

    cv_.notify_one();
    return true;
  }

  bool pop(std::function<void()>& task) {
    std::unique_lock<std::mutex> ul(mutex_);

    cv_.wait(ul, [this]() {
      return closed_ || !tasks_.empty();
    });

    if (tasks_.empty()) {
      return false;
    }

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
      if (worker.joinable()) {
        worker.join();
      }
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
  std::mutex cout_mutex;

  for (int i = 0; i < 10; ++i) {
    thread_pool.submit([i, &cout_mutex]() {
      std::lock_guard<std::mutex> lock(cout_mutex);

      std::cout << "i: " << i
                << " thread_id: " << std::this_thread::get_id()
                << std::endl;
    });
  }

  return 0;
}
```

---

# 16. 测试输出可能交错的问题

如果多个线程同时写：

```cpp
std::cout
```

可能发生输出交错。

例如：

```text
i: 1 thread_id: ...
i: i: 2 thread_id: ...
```

这不是线程池逻辑错误，而是多个线程同时输出导致的竞争。

为了让输出更整齐，可以加：

```cpp
std::mutex cout_mutex;
```

然后任务中：

```cpp
std::lock_guard<std::mutex> lock(cout_mutex);
std::cout << ... << std::endl;
```

注意：

```text
cout_mutex 只保护输出。
不是线程池核心逻辑的一部分。
```

---

# 17. 今日重要面试问答

## Q1：线程池解决什么问题？

答：

线程池通过预先创建固定数量的 worker 线程来复用线程资源。任务到来时不再频繁创建和销毁线程，而是提交到任务队列，由 worker 线程取出执行。这样可以降低线程创建销毁开销，控制线程数量，减少调度压力，并让线程生命周期更容易管理。

---

## Q2：一个最小线程池由哪些部分组成？

答：

最小线程池通常由任务队列和 worker 线程组成。任务队列保存待执行任务，并支持阻塞 pop 和 close；worker 线程循环从任务队列取任务并执行；线程池析构时关闭任务队列并 join 所有 worker。

---

## Q3：worker 线程的核心循环是什么？

答：

```cpp
std::function<void()> task;

while (queue_.pop(task)) {
    task();
}
```

`pop` 成功表示取到任务，执行 `task()`；`pop` 返回 false 表示队列已关闭且没有剩余任务，worker 退出循环。

---

## Q4：为什么析构时要先 close 队列，再 join worker？

答：

worker 线程可能阻塞在 `queue_.pop(task)` 中等待任务。如果析构时直接 join，而没有先 close 队列唤醒 worker，worker 可能一直睡眠，join 会一直阻塞，导致程序卡死。正确流程是先 close 队列，notify_all 唤醒所有 worker，再 join 等待它们退出。

---

## Q5：当前线程池析构时会不会丢弃已入队任务？

答：

不会。当前设计是 graceful shutdown。`close` 后队列不再接受新任务，但已有任务仍然可以被 worker 继续取出执行。只有当队列为空且 closed 为 true 时，`pop` 才返回 false，worker 才退出。

---

## Q6：为什么 task() 要在锁外执行？

答：

锁只应该保护任务队列本身的 push/pop 操作。任务执行可能很耗时，如果持锁执行 `task()`，其他线程无法提交新任务，其他 worker 也无法取任务，线程池并发度会严重下降，甚至可能死锁。因此应该在队列内部只 move 出任务，释放锁后再执行 `task()`。

---

## Q7：submit 为什么传 std::function<void()>？

答：

`std::function<void()>` 可以统一封装不同形式的无返回值任务，比如普通函数、lambda、绑定了参数的函数对象等。worker 不需要关心任务具体类型，只需要调用 `task()`。

---

## Q8：submit 中为什么要 std::move(task)？

答：

`task` 是 `submit` 函数按值接收的参数，进入函数后已经是当前函数自己的局部对象。把它放入任务队列后，`submit` 不再需要这个对象，所以可以移动进队列，减少 `std::function` 的拷贝成本。

---

## Q9：如果一个任务执行时间很长，会不会阻塞整个线程池？

答：

不会阻塞任务队列，因为 `task()` 是在锁外执行的，其他线程仍然可以 `submit`，其他 worker 也可以继续 `pop` 任务。但长任务会占用执行它的那个 worker。如果 worker 数量较少，后续任务可能排队等待。

---

## Q10：当前最小线程池不支持哪些能力？

答：

当前版本只支持无返回值任务，不支持 `future` 获取返回值，不支持异常传递，不支持任务优先级，不支持动态扩缩容，不支持取消正在执行的任务，也没有处理析构期间其他线程并发 `submit` 的复杂生命周期问题。

---

# 18. 今日易错点总结

```text
1. 线程池不是每个任务创建一个线程，而是复用固定 worker。
2. worker 线程要保存到 workers_ 中，否则无法 join。
3. worker 循环不能只 pop 一次，必须 while 循环。
4. TaskQueue::pop 返回 false 表示队列关闭且没有剩余任务。
5. 析构时必须先 close 队列，再 join worker。
6. 如果不 close，worker 可能永久阻塞在 pop。
7. 如果 std::thread 析构时仍 joinable，会 std::terminate。
8. task() 必须在锁外执行。
9. close 后已有任务仍然应该执行完，这是 graceful shutdown。
10. 当前版本不处理析构期间并发 submit 的复杂问题。
11. main 中 sleep 不是正确性必须，只是方便观察。
12. 多线程 std::cout 可能输出交错，需要单独加锁保护输出。
```

---

# 19. 今日核心代码片段

## 19.1 worker 循环

```cpp
std::function<void()> task;

while (queue_.pop(task)) {
    task();
}
```

---

## 19.2 构造 worker

```cpp
for (size_t i = 0; i < threadCount; ++i) {
    workers_.emplace_back([this]() {
        std::function<void()> task;

        while (queue_.pop(task)) {
            task();
        }
    });
}
```

---

## 19.3 submit

```cpp
bool submit(std::function<void()> task) {
    return queue_.push(std::move(task));
}
```

---

## 19.4 析构 close + join

```cpp
~ThreadPool() {
    queue_.close();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}
```

---

## 19.5 graceful shutdown 的 pop

```cpp
bool pop(std::function<void()>& task) {
    std::unique_lock<std::mutex> ul(mutex_);

    cv_.wait(ul, [this]() {
        return closed_ || !tasks_.empty();
    });

    if (tasks_.empty()) {
        return false;
    }

    task = std::move(tasks_.front());
    tasks_.pop();
    return true;
}
```

---

# 20. 明日开始前复习问题

```text
1. 为什么需要线程池？
2. 线程池相比每个任务创建一个线程有什么优势？
3. 最小线程池由哪两个核心组件组成？
4. worker 线程的核心循环是什么？
5. 为什么 worker 要 while(queue_.pop(task))？
6. queue_.pop(task) 返回 false 表示什么？
7. submit 的作用是什么？
8. submit 中为什么要 std::move(task)？
9. ThreadPool 构造函数负责什么？
10. ThreadPool 析构函数负责什么？
11. 为什么析构时要先 close 再 join？
12. close 后已入队任务是否会丢失？
13. 当前关闭方式是 graceful shutdown 还是 immediate shutdown？
14. task() 为什么必须在锁外执行？
15. main 中 sleep 是否是正确性必须？
16. 当前版本不支持哪些能力？
17. 析构期间并发 submit 属于什么问题？
18. ThreadPool 为什么也可以看作 RAII？
```

---

# 21. Day 7 总结

Day 7 的核心收获：

```text
TaskQueue：
    保存任务
    阻塞等待
    close 唤醒退出

ThreadPool：
    构造时创建 worker
    submit 时提交任务
    worker 循环取任务执行
    析构时 close + join
```

最终模型：

```text
submit(task)
    ↓
TaskQueue
    ↓
worker pop task
    ↓
task()
```

最重要的一句话：

```text
线程池的本质是：用固定数量的 worker 线程，循环消费任务队列中的任务，并在关闭时通过 close + join 正确回收线程资源。
```
