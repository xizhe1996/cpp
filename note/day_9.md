# Day 9｜线程池阶段收束：手写复盘 + 面试表达

# 1. 今日学习目标

Day 9 的目标不是继续扩展线程池功能，而是把 Day 6～Day 8 学过的内容收束成稳定能力。

核心目标：

- 复盘线程池整体结构
- 手写 `TaskQueue`
- 手写 `ThreadPool` 主体
- 手写 `submitTask` 返回 `future`
- 理解 `decltype(func)` 和 `decltype(func())` 的区别
- 能够讲清楚线程池核心面试问题
- 明确当前线程池版本的能力边界

核心主线：

> TaskQueue 负责同步和任务存储；worker 负责循环执行任务；packaged_task 负责把返回值或异常写入 shared state；future 负责把结果或异常带回调用者线程。

---

# 2. 线程池整体结构

## 2.1 为什么需要线程池？

如果每来一个任务就创建一个线程：

```cpp
std::thread t(task);
t.detach();
````

会有几个问题：

```text
1. 创建线程有开销
2. 销毁线程有开销
3. 线程数量不可控
4. 大量线程会增加调度压力
5. detach 后线程生命周期难管理
```

线程池的思路是：

```text
提前创建固定数量的 worker 线程。
任务提交到任务队列。
worker 循环从任务队列中取任务并执行。
```

线程池的核心价值：

```text
1. 复用线程资源
2. 控制线程数量
3. 降低线程创建 / 销毁开销
4. 降低调度压力
5. 统一管理线程生命周期
6. 解耦任务提交和任务执行
```

---

# 3. 最小线程池由什么组成？

最小线程池主要由两部分组成：

```text
1. TaskQueue
2. ThreadPool
```

其中：

```text
TaskQueue：
    保存任务
    支持 push
    支持 pop
    支持 close
    使用 mutex + condition_variable 实现阻塞等待和关闭唤醒

ThreadPool：
    持有 TaskQueue
    持有 worker 线程数组
    构造时创建 worker
    submitTask 提交任务
    析构时 close queue + join workers
```

---

# 4. TaskQueue 的职责

`TaskQueue` 内部保存：

```cpp
std::queue<std::function<void()>> tasks_;
```

为什么是：

```cpp
std::function<void()>
```

因为线程池内部希望 worker 的执行逻辑统一。

worker 不关心：

```text
任务原来有没有参数
任务原来返回什么类型
任务内部具体做什么
```

worker 只需要统一执行：

```cpp
task();
```

所以 TaskQueue 统一存：

```text
无参数、无返回值、可以直接执行的任务
```

也就是：

```cpp
void()
```

---

# 5. TaskQueue 的最终实现

```cpp
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <utility>

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

# 6. TaskQueue::push

## 6.1 代码

```cpp
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
```

## 6.2 语义

```text
如果队列已经关闭：
    返回 false

如果队列未关闭：
    把任务放入队列
    notify_one 唤醒一个等待中的 worker
    返回 true
```

## 6.3 关键点

```text
1. tasks_ 是共享队列，访问必须加锁
2. closed_ 是共享状态，访问也必须加锁
3. 入队时使用 std::move(task)
4. notify_one 通常放在锁释放之后
5. 成功路径必须 return true
```

---

# 7. TaskQueue::pop

## 7.1 代码

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

## 7.2 语义

```text
如果队列为空但未关闭：
    阻塞等待

如果队列非空：
    取出一个任务
    返回 true

如果队列关闭且为空：
    返回 false
```

## 7.3 为什么等待条件是 `closed_ || !tasks_.empty()`

```cpp
cv_.wait(ul, [this]() {
  return closed_ || !tasks_.empty();
});
```

worker 醒来的原因有两个：

```text
1. tasks_ 非空：有任务可以执行
2. closed_ 为 true：线程池要关闭，worker 需要退出
```

如果只写：

```cpp
!tasks_.empty()
```

那么线程池关闭且队列为空时，worker 可能永远睡眠，无法退出。

---

# 8. TaskQueue::close

## 8.1 代码

```cpp
void close() {
  {
    std::lock_guard<std::mutex> lg(mutex_);
    closed_ = true;
  }

  cv_.notify_all();
}
```

## 8.2 语义

```text
设置 closed_ = true。
通知所有等待中的 worker。
```

## 8.3 为什么用 notify_all？

因为可能有多个 worker 都阻塞在 `pop()` 中。

关闭线程池时，必须唤醒所有 worker，让它们检查：

```cpp
closed_ == true
```

然后在队列为空时退出。

如果只用：

```cpp
notify_one()
```

可能只唤醒一个 worker，其他 worker 继续睡眠，析构函数 `join()` 时可能卡住。

---

# 9. ThreadPool 主体

## 9.1 ThreadPool 的职责

`ThreadPool` 负责：

```text
1. 构造时创建 worker 线程
2. worker 循环从 TaskQueue 取任务并执行
3. 析构时关闭队列并 join 所有 worker
4. submitTask 提交任务并返回 future
```

---

## 9.2 ThreadPool 成员

```cpp
TaskQueue queue_;
std::vector<std::thread> workers_;
```

其中：

```text
queue_：
    保存任务
    负责同步
    负责关闭唤醒

workers_：
    保存所有 worker 线程对象
    析构时需要 join
```

---

# 10. ThreadPool 构造函数

## 10.1 正确写法

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

## 10.2 为什么要保存到 workers_

错误写法：

```cpp
for (size_t i = 0; i < threadCount; ++i) {
  std::thread t([this]() {
    std::function<void()> task;

    while (queue_.pop(task)) {
      task();
    }
  });
}
```

这个写法会出问题。

原因：

```text
t 是局部变量。
每轮循环结束时，t 会析构。
如果 std::thread 对象析构时仍然 joinable，程序会调用 std::terminate。
```

正确做法是：

```cpp
workers_.emplace_back(...);
```

把线程对象保存到 `workers_` 中，析构时统一 join。

---

# 11. worker 线程循环

## 11.1 代码

```cpp
std::function<void()> task;

while (queue_.pop(task)) {
  task();
}
```

## 11.2 含义

```text
pop 返回 true：
    成功取到任务
    执行 task()

pop 阻塞：
    队列为空但未关闭
    worker 睡眠等待任务

pop 返回 false：
    队列关闭且没有剩余任务
    worker 退出循环
```

所以 worker 的行为是：

```text
有任务就执行。
没任务就等待。
关闭且空了就退出。
```

---

# 12. 为什么 task() 必须在锁外执行？

`TaskQueue::pop()` 内部只在访问队列时加锁。

当 `pop()` 返回后，锁已经释放。

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

是在锁外执行。

这样设计是正确的。

原因：

```text
锁只应该保护任务队列本身。
任务执行可能很耗时。
如果持锁执行 task()：
    其他线程不能 submit 新任务
    其他 worker 不能 pop 任务
    并发度会严重下降
    甚至可能死锁
```

原则：

```text
锁只保护共享数据结构，不包裹耗时业务逻辑。
```

---

# 13. ThreadPool 析构函数

## 13.1 代码

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

## 13.2 为什么先 close 再 join？

worker 可能阻塞在：

```cpp
queue_.pop(task)
```

如果析构函数直接：

```cpp
worker.join();
```

但没有先关闭队列，那么：

```text
worker 一直睡眠等待任务
join 一直等待 worker 退出
程序卡死
```

正确流程：

```text
1. queue_.close()
2. 设置 closed_ = true
3. notify_all 唤醒所有 worker
4. worker 从 pop 中醒来
5. 队列空且关闭时 pop 返回 false
6. worker 退出循环
7. 析构函数 join 成功返回
```

---

# 14. submitTask 返回 future

## 14.1 最终代码

```cpp
template <typename F>
auto submitTask(F func) -> std::future<decltype(func())> {
  using RetType = decltype(func());

  auto task =
      std::make_shared<std::packaged_task<RetType()>>(std::move(func));

  std::future<RetType> future = task->get_future();

  bool ok = queue_.push([task]() {
    (*task)();
  });

  if (!ok) {
    throw std::runtime_error("ThreadPool is closed");
  }

  return future;
}
```

---

# 15. submitTask 的生成步骤

不要背整段代码，而是按 5 步生成。

## 第一步：推导返回值类型

```cpp
using RetType = decltype(func());
```

这里要注意：

```cpp
decltype(func)
```

和：

```cpp
decltype(func())
```

不是一回事。

当前需要的是任务执行后的返回值类型，所以必须用：

```cpp
decltype(func())
```

---

## 第二步：创建 packaged_task

```cpp
auto task =
    std::make_shared<std::packaged_task<RetType()>>(std::move(func));
```

这里：

```cpp
std::packaged_task<RetType()>
```

表示：

```text
无参数，返回 RetType 的任务
```

---

## 第三步：获取 future

```cpp
std::future<RetType> future = task->get_future();
```

这个 future 返回给调用者。

调用者通过：

```cpp
future.get();
```

获取结果或异常。

---

## 第四步：包装成 void() 任务并入队

```cpp
bool ok = queue_.push([task]() {
  (*task)();
});
```

线程池内部队列存的是：

```cpp
std::function<void()>
```

所以入队任务必须是：

```text
无参数，无返回值
```

这个 lambda 表面上没有返回值，但内部执行：

```cpp
(*task)();
```

`packaged_task` 会把真正的返回值或异常写入 shared state。

---

## 第五步：失败处理并返回 future

```cpp
if (!ok) {
  throw std::runtime_error("ThreadPool is closed");
}

return future;
```

---

# 16. decltype(func) 和 decltype(func()) 的区别

这是 Day 9 暴露出的重要薄弱点。

假设：

```cpp
auto func = []() {
  return 123;
};
```

那么：

```cpp
decltype(func)
```

表示：

```text
func 这个变量本身的类型
```

也就是 lambda 对象的闭包类型。

而：

```cpp
decltype(func())
```

表示：

```text
调用 func() 这个表达式之后得到的结果类型
```

也就是：

```cpp
int
```

所以：

```cpp
using A = decltype(func);    // A 是 lambda 对象类型
using B = decltype(func());  // B 是 int
```

---

# 17. decltype 的常见用法

## 17.1 看变量本身类型

```cpp
int x = 10;

using T = decltype(x);
```

结果：

```text
T = int
```

---

## 17.2 看表达式结果类型

```cpp
int x = 10;
int y = 20;

using T = decltype(x + y);
```

结果：

```text
T = int
```

---

## 17.3 看函数调用返回类型

```cpp
auto func = []() {
  return 123;
};

using T = decltype(func());
```

结果：

```text
T = int
```

---

## 17.4 看带参数调用返回类型

```cpp
auto func = [](int a, int b) {
  return a + b;
};

using T = decltype(func(1, 2));
```

结果：

```text
T = int
```

注意：

```cpp
decltype(func())
```

不行，因为这个 `func` 需要两个参数，不能无参数调用。

---

# 18. packaged_task<RetType()> 的含义

```cpp
std::packaged_task<RetType()>
```

这里不是单纯传返回值类型。

`packaged_task` 接收的是：

```text
函数签名
```

所以：

```cpp
RetType()
```

表示：

```text
无参数，返回 RetType
```

例如：

```cpp
std::packaged_task<int()>
```

表示：

```text
无参数，返回 int 的任务
```

```cpp
std::packaged_task<std::string()>
```

表示：

```text
无参数，返回 std::string 的任务
```

```cpp
std::packaged_task<void()>
```

表示：

```text
无参数，无返回值的任务
```

如果是：

```cpp
std::packaged_task<int(int, int)>
```

表示：

```text
接收两个 int 参数，返回 int 的任务
```

---

# 19. 为什么 packaged_task 要用 shared_ptr 包起来？

`std::packaged_task` 是：

```text
不可拷贝，可以移动
```

但是我们要把任务放入：

```cpp
std::function<void()>
```

而 `std::function` 保存的 callable 通常需要可拷贝。

如果直接捕获 `packaged_task`，会遇到拷贝问题。

所以使用：

```cpp
auto task =
    std::make_shared<std::packaged_task<RetType()>>(std::move(func));
```

然后：

```cpp
queue_.push([task]() {
  (*task)();
});
```

这里 lambda 捕获的是：

```cpp
std::shared_ptr<std::packaged_task<RetType()>>
```

`shared_ptr` 可以拷贝，所以 lambda 可以放进 `std::function<void()>`。

真正的 `packaged_task` 仍然只有一份，存在堆上。

---

# 20. future 和 worker 线程是什么关系？

`future` 不是直接从 worker 线程“返回值”里拿结果。

真正关系是：

```text
worker 执行 wrapper
    ↓
wrapper 执行 packaged_task
    ↓
packaged_task 执行用户函数
    ↓
用户函数返回值或异常写入 shared state
    ↓
future.get() 从 shared state 读取结果或重新抛出异常
```

所以：

```text
future 和 worker 不直接绑定。
future 绑定的是 shared state。
worker 只是负责执行任务并把结果写入 shared state。
```

如果 worker 还没执行任务：

```cpp
future.get();
```

会阻塞。

如果 worker 已经执行完任务并写入 shared state，即使 worker 后来退出，future 仍然可以从 shared state 获取结果。

---

# 21. 当前完整 ThreadPool with future

```cpp
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
#include <utility>
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

  template <typename F>
  auto submitTask(F func) -> std::future<decltype(func())> {
    using RetType = decltype(func());

    auto task =
        std::make_shared<std::packaged_task<RetType()>>(std::move(func));

    std::future<RetType> future = task->get_future();

    bool ok = queue_.push([task]() {
      (*task)();
    });

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

  auto f1 = pool.submitTask([]() {
    return 100 + 100;
  });

  std::cout << f1.get() << std::endl;

  std::string name = "xizhe";

  auto f2 = pool.submitTask([name]() {
    return std::string("name: ") + name;
  });

  std::cout << f2.get() << std::endl;

  auto f3 = pool.submitTask([]() {
    std::cout << "thread id: "
              << std::this_thread::get_id()
              << std::endl;
  });

  f3.get();

  auto f4 = pool.submitTask([]() {
    throw std::runtime_error("task failed.");
  });

  try {
    f4.get();
  } catch (const std::exception& e) {
    std::cout << "catch exception: " << e.what() << std::endl;
  }

  return 0;
}
```

---

# 22. 当前版本支持什么？

当前线程池支持：

```text
1. 固定数量 worker 线程
2. TaskQueue 阻塞队列
3. graceful shutdown
4. std::function<void()> 任务队列
5. submitTask 提交无参数任务
6. 自动推导返回值类型
7. 返回 std::future<RetType>
8. 支持 int / string / void 等返回值
9. 支持任务异常通过 future.get() 传递
10. 带参数逻辑可以通过 lambda 值捕获解决
```

---

# 23. 当前版本不支持什么？

当前线程池暂不支持：

```text
1. submit(func, arg1, arg2) 直接传参数
2. 变参模板 submit(F&&, Args&&...)
3. 动态扩缩容
4. 任务优先级
5. 任务取消
6. immediate shutdown
7. work stealing
8. 无锁队列
9. 析构期间并发 submit 的复杂生命周期保护
```

当前阶段先主动收束，不继续深挖。

---

# 24. 线程池面试表达

## Q1：线程池解决什么问题？

答：

线程池通过提前创建固定数量的 worker 线程来复用线程资源。任务提交后进入任务队列，worker 线程循环从队列中取任务执行。这样可以减少频繁创建和销毁线程的开销，控制线程数量，降低调度压力，并统一管理线程生命周期。

---

## Q2：TaskQueue 的作用是什么？

答：

TaskQueue 是任务提交线程和 worker 线程之间的缓冲层。submit 线程负责把任务 push 到队列，worker 线程负责从队列 pop 任务并执行。TaskQueue 内部通过 mutex 和 condition_variable 实现线程安全访问、阻塞等待和关闭唤醒。

---

## Q3：为什么 TaskQueue 存 std::function<void()>？

答：

这是为了统一任务模型。worker 不关心任务原本有几个参数、返回什么类型，只需要统一调用 task()。参数绑定、返回值包装和异常传递都在 submit 阶段处理，TaskQueue 内部只保存可直接执行的 void() 任务。

---

## Q4：pop 的等待条件为什么是 closed_ || !tasks_.empty()？

答：

worker 醒来的原因有两个：一是队列中有任务可以执行；二是队列已经关闭，需要让 worker 退出。如果只等待 tasks_ 非空，那么队列关闭且为空时，worker 会一直睡眠，析构时 join 可能卡死。

---

## Q5：close 为什么要 notify_all？

答：

关闭线程池时，可能有多个 worker 都阻塞在 pop() 中。close 设置 closed_ = true 后，需要 notify_all 唤醒所有 worker，让它们重新检查条件并在队列为空时退出。如果只 notify_one，其他 worker 可能继续睡眠，导致析构 join 卡住。

---

## Q6：worker 线程的核心循环是什么？

答：

```cpp
std::function<void()> task;

while (queue_.pop(task)) {
    task();
}
```

pop 返回 true 表示成功取到任务，执行 task；如果队列为空但未关闭，pop 会阻塞等待；如果队列关闭且为空，pop 返回 false，worker 退出循环。

---

## Q7：为什么析构时必须先 close 再 join？

答：

worker 可能阻塞在 queue_.pop(task) 中等待任务。如果析构函数直接 join，而没有先 close 队列唤醒 worker，worker 没有退出条件，join 会一直阻塞。正确流程是先 close 队列，notify_all 唤醒 worker，再 join 等待线程退出。

---

## Q8：submitTask 为什么要用 packaged_task？

答：

packaged_task 用来包装用户提交的可调用对象。当 packaged_task 被执行时，它会自动把函数返回值或异常写入 shared state。submitTask 可以返回对应的 future 给调用者，调用者之后通过 future.get() 获取结果或接收异常。

---

## Q9：为什么 submitTask 返回 future？

答：

任务实际在线程池 worker 中异步执行，调用 submitTask 时结果还没有产生。返回 future 可以让调用者之后通过 get() 等待并获取结果。如果任务抛出异常，也会在 future.get() 时重新抛出。

---

## Q10：为什么 packaged_task 要用 shared_ptr 包起来？

答：

packaged_task 是 move-only 类型，不能拷贝；而 std::function 保存的 callable 通常要求可拷贝。用 shared_ptr 包住 packaged_task 后，lambda 捕获的是可拷贝的 shared_ptr，可以放进 std::function<void()> 队列，而真正的 packaged_task 仍然只有一份。

---

## Q11：future.get() 的结果是 worker 线程直接返回的吗？

答：

不是。worker 执行 wrapper，wrapper 执行 packaged_task，packaged_task 把用户函数的返回值或异常写入 shared state。future.get() 是从 shared state 中读取结果或重新抛出异常。

---

## Q12：当前线程池有哪些限制？

答：

当前版本是教学版线程池，固定线程数，使用 graceful shutdown，不支持动态扩缩容，不支持任务优先级，不支持任务取消，不支持 immediate shutdown，不处理析构期间并发 submit 的复杂生命周期问题。当前 submitTask 只支持无参数任务，带参数逻辑通过 lambda 值捕获解决。

---

# 25. Day 9 易错点总结

```text
1. TaskQueue::push 成功路径必须 return true。
2. std::thread 局部变量如果不 join / detach，析构时会 std::terminate。
3. worker 线程必须保存到 workers_ 中，析构时统一 join。
4. worker 循环是 while(queue_.pop(task))，不是只 pop 一次。
5. pop 等待条件必须包含 closed_。
6. close 要 notify_all，不是 notify_one。
7. 析构要先 close 再 join。
8. task() 要在锁外执行。
9. decltype(func) 和 decltype(func()) 不一样。
10. decltype(func) 是 func 对象本身类型。
11. decltype(func()) 是 func() 调用结果类型。
12. packaged_task<RetType()> 中 RetType() 是函数签名。
13. packaged_task 不可拷贝，只能移动。
14. shared_ptr<packaged_task> 用来适配 std::function。
15. future 不是直接绑定 worker，而是绑定 shared state。
16. worker 只是负责执行任务并写入 shared state。
```

---

# 26. 后续归属

线程池阶段到 Day 9 先收束。

后续如果还有时间，可以再拓展：

```text
1. 完整变参模板 submit(F&&, Args&&...)
2. std::invoke_result_t
3. std::forward / 完美转发
4. std::bind 与 lambda 参数绑定
5. immediate shutdown
6. 动态扩缩容
7. 任务取消
8. 任务优先级
```