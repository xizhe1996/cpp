# Day 8｜线程池返回值：future / promise / packaged_task / submitTask

# 1. 今日学习目标

Day 8 的核心目标：

- 理解 `std::future` 的作用
- 理解 `std::promise` 与 `std::future` 的关系
- 理解 shared state 的概念
- 掌握 `std::packaged_task` 的基本用法
- 理解 `packaged_task` 如何把函数返回值写入 `future`
- 理解为什么线程池内部任务队列通常存 `std::function<void()>`
- 理解如何把有返回值任务包装成 `void()` 任务
- 为 `ThreadPool` 增加支持返回值的 `submitTask`
- 理解 `future<void>` 的作用
- 理解任务异常如何通过 `future.get()` 传递
- 明确当前版本线程池的能力边界

核心主线：

> 线程池 worker 负责执行任务，future 负责把任务结果或异常带回调用者线程。

---

# 2. Day 7 线程池的问题

Day 7 的线程池只能提交：

```cpp
bool submit(std::function<void()> task);
````

也就是只能处理：

```cpp
pool.submit([] {
    std::cout << "hello\n";
});
```

这种任务可以执行，但调用方拿不到返回值。

实际工程中经常希望：

```cpp
auto f = pool.submitTask([] {
    return 100 + 200;
});

std::cout << f.get() << std::endl; // 300
```

所以 Day 8 的目标是：

```text
让线程池支持带返回值的任务。
```

---

# 3. std::future 是什么？

`std::future<T>` 可以理解为：

```text
一个未来会得到 T 类型结果的对象。
```

例如：

```cpp
std::future<int>
```

表示：

```text
未来会得到一个 int 类型结果。
```

通过：

```cpp
future.get();
```

获取结果。

如果结果还没准备好：

```text
get() 会阻塞等待。
```

---

## 3.1 future 自己不计算结果

`future` 只是结果接收端。

真正负责设置结果的通常是：

```text
std::promise
std::packaged_task
std::async
```

本日重点是：

```text
promise / packaged_task
```

---

# 4. promise + future 基础

## 4.1 promise 和 future 的角色

```text
promise：结果写入端
future：结果读取端
shared state：真正保存结果或异常的共享状态
```

关系可以理解为：

```text
promise  ----写入---->  shared state  ----读取---->  future
```

---

## 4.2 基本示例

```cpp
#include <chrono>
#include <future>
#include <iostream>
#include <thread>

int main() {
  std::promise<int> promise_;
  std::future<int> future_ = promise_.get_future();

  std::thread t([&promise_]() {
    int a = 10;
    int b = 20;

    std::this_thread::sleep_for(std::chrono::seconds(1));

    promise_.set_value(a + b);
  });

  int value = future_.get();

  std::cout << "value: " << value << std::endl;

  t.join();

  return 0;
}
```

执行逻辑：

```text
1. promise<int> promise_ 创建结果写入端
2. future<int> future_ 创建结果读取端
3. 子线程中 promise_.set_value(30)
4. 主线程 future_.get() 阻塞等待结果
5. set_value 后，future_.get() 返回 30
6. join 子线程
```

---

## 4.3 set_value 保存到哪里？

`promise.set_value(x)` 不是把结果直接保存到 `promise` 对象里，也不是直接保存到 `future` 对象里。

它是保存到二者共同关联的：

```text
shared state
```

也就是：

```text
promise.set_value(x)
    ↓
shared state
    ↓
future.get()
```

---

## 4.4 future.get() 只能调用一次

```cpp
int a = future_.get();
int b = future_.get(); // 错误
```

普通 `std::future` 的 `get()` 通常只能调用一次。

如果多个地方都要读取同一个结果，需要使用：

```cpp
std::shared_future<T>
```

本阶段暂不展开。

---

## 4.5 promise 是否可以拷贝？

`std::promise`：

```text
不可拷贝，可以移动
```

所以：

```cpp
std::promise<int> p1;
std::promise<int> p2 = p1;            // 错误
std::promise<int> p3 = std::move(p1); // 可以
```

---

# 5. std::packaged_task 基础

## 5.1 packaged_task 是什么？

`std::packaged_task<R(Args...)>` 可以理解为：

```text
把一个可调用对象包装成“执行后能把结果写入 future 的任务”。
```

例如：

```cpp
std::packaged_task<int()> task([] {
    return 100 + 200;
});
```

表示：

```text
这是一个无参数、返回 int 的任务。
```

可以从它获取：

```cpp
std::future<int> f = task.get_future();
```

执行任务：

```cpp
task();
```

然后：

```cpp
std::cout << f.get() << std::endl; // 300
```

---

## 5.2 最小示例：无参数 packaged_task

```cpp
#include <future>
#include <iostream>

int main() {
  std::packaged_task<int()> task([]() {
    return 100 + 200;
  });

  std::future<int> future = task.get_future();

  task();

  std::cout << future.get() << std::endl;

  return 0;
}
```

执行逻辑：

```text
1. packaged_task 包装 lambda
2. get_future() 拿到 future
3. task() 执行 lambda
4. lambda 返回值自动写入 shared state
5. future.get() 读取结果
```

---

## 5.3 有参数 packaged_task

`packaged_task` 本身支持带参数任务。

```cpp
#include <future>
#include <iostream>
#include <thread>

int main() {
  std::packaged_task<int(int, int)> task(
      [](int a, int b) {
        return a + b;
      });

  std::future<int> future = task.get_future();

  task(100, 200);

  std::cout << future.get() << std::endl;

  return 0;
}
```

这里：

```cpp
std::packaged_task<int(int, int)>
```

表示：

```text
接收两个 int 参数，返回 int。
```

调用方式是：

```cpp
task(100, 200);
```

---

## 5.4 packaged_task 放进 std::thread

`packaged_task` 不可拷贝，只能移动。

所以不能：

```cpp
std::thread t(task, 100, 200); // 错误，试图拷贝 task
```

应该：

```cpp
std::thread t(std::move(task), 100, 200);
```

完整示例：

```cpp
#include <future>
#include <iostream>
#include <thread>

int main() {
  std::packaged_task<int(int, int)> task(
      [](int a, int b) {
        return a + b;
      });

  std::future<int> future = task.get_future();

  std::thread t(std::move(task), 100, 200);

  std::cout << future.get() << std::endl;

  t.join();

  return 0;
}
```

---

# 6. get() 和 join() 的区别

```cpp
future.get();
```

等待的是：

```text
任务结果是否 ready
```

```cpp
thread.join();
```

等待的是：

```text
线程是否结束
```

两者不是一回事。

例如：

```cpp
std::thread t([&p]() {
    p.set_value(42);
    std::this_thread::sleep_for(std::chrono::seconds(5));
});
```

如果主线程：

```cpp
int value = f.get();
```

可能很快返回，因为结果已经设置好了。

但：

```cpp
t.join();
```

还要等线程真正结束。

---

# 7. 为什么线程池内部任务队列存 std::function<void()>？

Day 7 中的 `TaskQueue` 是：

```cpp
std::queue<std::function<void()>> tasks_;
```

worker 执行任务时只会：

```cpp
std::function<void()> task;

while (queue_.pop(task)) {
    task();
}
```

这说明线程池内部统一处理的是：

```text
无参数、无返回值的任务
```

也就是：

```cpp
void()
```

---

## 7.1 这是语言限制吗？

不是。

`packaged_task` 本身支持带参数任务，也支持有返回值任务。

我们把任务队列设计成 `std::function<void()>` 是一种工程设计选择。

原因：

```text
1. worker 逻辑简单，只需要 task()
2. 队列类型统一，不用存不同函数签名
3. 参数和返回值在 submit 阶段处理
4. 返回值通过 future/shared state 返回给调用者
```

一句话：

```text
不是 packaged_task 不能带参数；
是 worker 不想处理参数。
```

---

# 8. 把有返回值任务包装成 void() 任务

假设有任务：

```cpp
[] {
    return 100 + 200;
}
```

它是：

```text
int()
```

但线程池队列只能存：

```text
void()
```

解决方法：

```cpp
auto task = std::make_shared<std::packaged_task<int()>>([] {
    return 100 + 200;
});

std::future<int> future = task->get_future();

std::function<void()> wrapper = [task]() {
    (*task)();
};
```

这里：

```cpp
wrapper();
```

没有返回值。

但它内部执行：

```cpp
(*task)();
```

而 `packaged_task` 会把原始任务的返回值写入 shared state。

所以：

```text
wrapper 本身不返回值
future 仍然可以拿到结果
```

---

# 9. 为什么使用 shared_ptr<packaged_task>？

## 9.1 packaged_task 不可拷贝

```cpp
std::packaged_task<int()> task([] {
    return 1;
});

auto task2 = task;            // 错误
auto task3 = std::move(task); // 可以
```

`packaged_task` 是：

```text
不可拷贝，可移动
```

---

## 9.2 std::function 通常需要可拷贝 callable

如果写：

```cpp
std::packaged_task<int()> task([] {
    return 1;
});

std::function<void()> wrapper = [task]() {
    task();
};
```

这会有问题，因为 lambda 按值捕获 `task` 时需要拷贝 `packaged_task`。

而 `packaged_task` 不可拷贝。

即使使用 move 捕获：

```cpp
std::function<void()> wrapper = [task = std::move(task)]() mutable {
    task();
};
```

这个 lambda 会变成 move-only，而 `std::function` 在 C++17 及以前通常要求可复制 callable。

---

## 9.3 shared_ptr 的作用

使用：

```cpp
auto task = std::make_shared<std::packaged_task<int()>>(...);
```

然后：

```cpp
std::function<void()> wrapper = [task]() {
    (*task)();
};
```

这里 lambda 捕获的是：

```cpp
std::shared_ptr<std::packaged_task<int()>>
```

`shared_ptr` 可以拷贝。

所以：

```text
lambda 可以拷贝
std::function 可以保存这个 lambda
真正的 packaged_task 仍然只有一份，在堆上
```

因此：

```text
shared_ptr<packaged_task> 是为了适配 packaged_task 不可拷贝与 std::function 需要可拷贝 callable 的问题。
```

---

# 10. 最小包装示例：int 返回值

```cpp
#include <future>
#include <functional>
#include <iostream>
#include <memory>

int main() {
  auto task =
      std::make_shared<std::packaged_task<int()>>([]() {
        return 100 + 200;
      });

  std::future<int> future = task->get_future();

  std::function<void()> wrapper([task]() -> void {
    (*task)();
  });

  wrapper();

  std::cout << future.get() << std::endl;

  return 0;
}
```

输出：

```text
300
```

---

# 11. 最小包装示例：string 返回值

```cpp
#include <future>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

int main() {
  auto task = std::make_shared<std::packaged_task<std::string()>>(
      []() {
        return std::string("hello");
      });

  std::future<std::string> future = task->get_future();

  std::function<void()> wrapper([task]() -> void {
    (*task)();
  });

  wrapper();

  std::cout << future.get() << std::endl;

  return 0;
}
```

输出：

```text
hello
```

---

# 12. submitInt：写死 int 返回值版本

最开始可以先写一个固定返回值版本：

```cpp
std::future<int> submitInt(std::function<int()> func) {
  auto task = std::make_shared<std::packaged_task<int()>>(std::move(func));

  std::future<int> future = task->get_future();

  queue_.push([task]() {
    (*task)();
  });

  return future;
}
```

这个版本只支持：

```cpp
int()
```

任务。

例如：

```cpp
auto f1 = pool.submitInt([] {
    return 100 + 200;
});

std::cout << f1.get() << std::endl;
```

---

# 13. 无参数模板版 submitTask

为了支持不同返回值类型，可以写模板：

```cpp
template <typename F>
auto submitTask(F func) -> std::future<decltype(func())> {
  using RetType = decltype(func());

  auto task = std::make_shared<std::packaged_task<RetType()>>(
      std::move(func));

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

## 13.1 这段代码如何生成？

不要背整段代码，按 5 步生成。

### 第一步：推导返回值类型

当前版本只支持无参数任务：

```cpp
func()
```

所以：

```cpp
using RetType = decltype(func());
```

---

### 第二步：创建 packaged_task

```cpp
auto task = std::make_shared<std::packaged_task<RetType()>>(
    std::move(func));
```

这里：

```cpp
std::packaged_task<RetType()>
```

表示：

```text
无参数、返回 RetType 的任务
```

---

### 第三步：获取 future

```cpp
std::future<RetType> future = task->get_future();
```

这个 future 返回给调用者。

---

### 第四步：包装成 void() 任务并入队

```cpp
bool ok = queue_.push([task]() {
  (*task)();
});
```

这里入队的是：

```text
void() wrapper
```

---

### 第五步：返回 future

```cpp
return future;
```

---

# 14. 当前 submitTask 支持什么？

支持：

```cpp
auto f1 = pool.submitTask([] {
    return 100 + 200;
});

auto f2 = pool.submitTask([] {
    return std::string("hello");
});

auto f3 = pool.submitTask([] {
    std::cout << "void task\n";
});
```

对应返回类型：

```text
f1：std::future<int>
f2：std::future<std::string>
f3：std::future<void>
```

---

# 15. 带参数任务怎么处理？

当前版本：

```cpp
template <typename F>
auto submitTask(F func) -> std::future<decltype(func())>;
```

只支持：

```cpp
func()
```

所以不支持直接：

```cpp
pool.submitTask([](int a, int b) {
    return a + b;
});
```

因为这个 lambda 不能直接调用：

```cpp
func()
```

它需要：

```cpp
func(a, b)
```

---

## 15.1 当前阶段推荐写法：lambda 值捕获参数

```cpp
int a = 10;
int b = 20;

auto f = pool.submitTask([a, b]() {
    return a + b;
});
```

这样传给 `submitTask` 的任务仍然是：

```text
int()
```

也就是：

```text
无参数，返回 int
```

---

## 15.2 为什么值捕获可以解决参数问题？

lambda 值捕获会把外部变量保存到 lambda 对象内部，类似成员变量。

例如：

```cpp
[a, b]() {
    return a + b;
}
```

可以理解为：

```text
lambda 对象内部保存了一份 a 和 b
operator() 无参数
调用时直接使用内部保存的 a 和 b
```

所以它本身仍然满足：

```cpp
func()
```

---

## 15.3 为什么线程池任务默认推荐值捕获？

线程池任务可能稍后才执行。

如果引用捕获：

```cpp
auto f = pool.submitTask([&a, &b]() {
    return a + b;
});
```

当任务执行时，`a`、`b` 可能已经销毁。

这会导致：

```text
悬空引用
未定义行为
```

所以默认推荐：

```cpp
[a, b]
```

也就是值捕获。

---

# 16. future<void> 的作用

如果任务没有返回值：

```cpp
auto f = pool.submitTask([] {
    std::cout << "void task\n";
});
```

返回类型是：

```cpp
std::future<void>
```

调用：

```cpp
f.get();
```

作用是：

```text
等待任务完成。
如果任务中抛异常，get() 会重新抛出异常。
```

`future<void>` 没有返回值，但仍然可以表达：

```text
任务完成状态
任务异常状态
```

---

# 17. 异常传递

## 17.1 任务中抛异常

```cpp
auto f = pool.submitTask([]() -> int {
    throw std::runtime_error("task failed");
});
```

worker 执行任务时抛异常。

但因为任务被 `packaged_task` 包装，异常不会直接在 main 线程抛出。

`packaged_task` 会把异常保存到：

```text
shared state
```

之后：

```cpp
f.get();
```

会重新抛出这个异常。

---

## 17.2 示例

```cpp
auto f = pool.submitTask([]() -> int {
  throw std::runtime_error("task failed");
});

try {
  std::cout << f.get() << std::endl;
} catch (const std::exception& e) {
  std::cout << "catch exception: " << e.what() << std::endl;
}
```

输出：

```text
catch exception: task failed
```

---

## 17.3 future.get() 的两个作用

对于 `future<T>`：

```text
1. 获取 T 类型返回值
2. 如果任务中保存了异常，则重新抛出异常
```

对于 `future<void>`：

```text
1. 等待任务完成
2. 如果任务中保存了异常，则重新抛出异常
```

---

# 18. broken promise

如果结果写入端销毁了，但没有设置结果，`future.get()` 会抛出：

```text
std::future_error: broken promise
```

例如：

```cpp
std::future<int> f;

{
  std::promise<int> p;
  f = p.get_future();
} // p 销毁，没有 set_value

f.get(); // 抛出 broken promise
```

对于 `packaged_task` 也是类似：

```cpp
std::future<int> f;

{
  std::packaged_task<int()> task([] {
    return 123;
  });

  f = task.get_future();
} // task 没有执行就销毁

f.get(); // 抛出 broken promise
```

---

# 19. 当前完整 ThreadPool with future 代码

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

    cv_.wait(lock, [this]() {
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

  // test 1: int 返回值
  auto f1 = pool.submitTask([]() {
    return 100 + 100;
  });

  std::cout << f1.get() << std::endl;

  // test 2: string 返回值
  std::string name = "xizhe";

  auto f2 = pool.submitTask([name]() {
    return std::string("name: ") + name;
  });

  std::cout << f2.get() << std::endl;

  // test 3: void 返回值
  auto f3 = pool.submitTask([]() {
    std::cout << "thread id: "
              << std::this_thread::get_id()
              << std::endl;
  });

  f3.get();

  // test 4: 异常传递
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

# 20. 当前版本的能力边界

当前线程池支持：

```text
1. 固定数量 worker 线程
2. TaskQueue 阻塞队列
3. graceful shutdown
4. 无参数任务提交
5. int / string / void 等不同返回值
6. future.get() 获取结果
7. future.get() 等待 void 任务完成
8. packaged_task 捕获异常并通过 future.get() 重新抛出
9. 带参数逻辑通过 lambda 值捕获解决
```

当前线程池暂不支持：

```text
1. submit(func, arg1, arg2) 直接传参数
2. 变参模板 submit(F&&, Args&&...)
3. 动态扩缩容
4. 任务取消
5. immediate shutdown
6. 任务优先级
7. work stealing
8. 无锁队列
9. 析构期间并发 submit 的复杂生命周期保护
```

这是当前 2 周面试冲刺阶段的主动收束。

---

# 21. 今日重要面试问答

## Q1：future 是什么？

答：

`std::future<T>` 表示一个未来会得到 T 类型结果的对象。调用 `future.get()` 可以获取结果；如果结果还没有准备好，`get()` 会阻塞等待。普通 `std::future` 的 `get()` 通常只能调用一次。

---

## Q2：promise 和 future 的关系是什么？

答：

`promise` 是结果写入端，`future` 是结果读取端。二者通过同一个 shared state 关联。`promise.set_value()` 会把结果写入 shared state，`future.get()` 从 shared state 中取出结果。

---

## Q3：packaged_task 是什么？

答：

`std::packaged_task` 用来包装一个可调用对象。执行 packaged_task 时，它会调用内部的函数，并把返回值或异常写入 shared state。调用方可以通过 `get_future()` 得到对应的 future，并通过 `future.get()` 获取结果。

---

## Q4：packaged_task 和 promise 有什么区别？

答：

`promise` 需要手动调用 `set_value()` 设置结果。`packaged_task` 会自动执行被包装的函数，并把函数返回值写入 shared state。可以理解为 packaged_task 是把“函数执行”和“结果写入 future”绑定在一起的任务对象。

---

## Q5：为什么线程池任务队列里存 std::function<void()>？

答：

这是为了统一任务模型。worker 线程不关心任务原本有几个参数、返回什么类型，只需要从队列中取出一个任务并调用 `task()`。参数绑定和返回值处理都在 submit 阶段完成，任务队列内部只保存可直接执行的 `void()` 任务。

---

## Q6：为什么有返回值任务还能放进 std::function<void()> 队列？

答：

因为有返回值任务会先被 `packaged_task<RetType()>` 包装。然后再创建一个 `void()` wrapper，wrapper 内部执行 packaged_task。wrapper 本身没有返回值，但 packaged_task 会把原始任务的返回值写入 shared state，所以调用方仍然可以通过 future 获取结果。

---

## Q7：为什么使用 shared_ptr<packaged_task>？

答：

`packaged_task` 不可拷贝，只能移动；而 `std::function` 保存的 callable 通常要求可拷贝。使用 `shared_ptr<packaged_task>` 后，lambda 值捕获的是可拷贝的 shared_ptr，std::function 可以保存这个 lambda，而真正的 packaged_task 仍然只有一份，存放在堆上。

---

## Q8：future<void>::get() 有什么作用？

答：

`future<void>::get()` 没有返回值。它的作用是等待 void 任务执行完成。如果任务内部抛出异常，这个异常也会在 `future<void>::get()` 时重新抛出。

---

## Q9：任务里抛异常怎么办？

答：

如果任务被 packaged_task 包装，任务内部抛出的异常会被 packaged_task 捕获并保存到 shared state。调用方之后调用 `future.get()` 时，这个异常会在调用 get 的线程中重新抛出。

---

## Q10：当前 submitTask 为什么不能直接支持 submit(func, arg1, arg2)？

答：

当前 submitTask 只有一个模板参数 F，并使用 `decltype(func())` 推导返回值，因此要求 func 能够无参数调用。它没有接收 Args... 参数，也没有使用 `decltype(func(args...))` 或 `std::invoke_result_t` 推导带参数调用的返回值，所以不能直接支持 `submit(func, arg1, arg2)`。当前阶段可以用 lambda 值捕获参数来解决带参数逻辑。

---

# 22. 今日易错点总结

```text
1. future 自己不计算结果，它只是结果读取端。
2. promise / packaged_task / future 通过 shared state 关联。
3. future.get() 通常只能调用一次。
4. promise 不可拷贝，可以移动。
5. packaged_task 不可拷贝，可以移动。
6. packaged_task 支持带参数，不是语言限制。
7. 线程池队列设计成 std::function<void()> 是工程设计选择。
8. worker 统一调用 task()，不处理参数和返回值。
9. 有返回值任务通过 packaged_task 写入 shared state。
10. wrapper 是 void()，但内部执行 packaged_task。
11. shared_ptr<packaged_task> 是为了适配 std::function 的可拷贝要求。
12. future<void>::get() 用来等待任务完成和接收异常。
13. 任务抛异常会在 future.get() 中重新抛出。
14. 带参数任务当前用 lambda 值捕获解决。
15. 引用捕获提交到线程池时要警惕悬空引用。
16. 当前 submitTask 只支持 func()，不支持 func(args...)。
```

---

# 23. 今日核心代码片段

## 23.1 promise + future

```cpp
std::promise<int> p;
std::future<int> f = p.get_future();

std::thread t([&p]() {
    p.set_value(42);
});

std::cout << f.get() << std::endl;

t.join();
```

---

## 23.2 packaged_task 基础

```cpp
std::packaged_task<int()> task([] {
    return 100 + 200;
});

auto future = task.get_future();

task();

std::cout << future.get() << std::endl;
```

---

## 23.3 packaged_task 有参数

```cpp
std::packaged_task<int(int, int)> task([](int a, int b) {
    return a + b;
});

auto future = task.get_future();

task(100, 200);

std::cout << future.get() << std::endl;
```

---

## 23.4 shared_ptr<packaged_task> + wrapper

```cpp
auto task = std::make_shared<std::packaged_task<int()>>([] {
    return 100 + 200;
});

auto future = task->get_future();

std::function<void()> wrapper = [task]() {
    (*task)();
};

wrapper();

std::cout << future.get() << std::endl;
```

---

## 23.5 submitTask

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

## 23.6 lambda 捕获参数

```cpp
int a = 10;
int b = 20;

auto f = pool.submitTask([a, b]() {
    return a + b;
});

std::cout << f.get() << std::endl;
```

---

## 23.7 异常传递

```cpp
auto f = pool.submitTask([]() -> int {
    throw std::runtime_error("task failed");
});

try {
    std::cout << f.get() << std::endl;
} catch (const std::exception& e) {
    std::cout << "catch exception: " << e.what() << std::endl;
}
```

---

# 24. 明日开始前复习问题

```text
1. future 是什么？
2. future.get() 如果结果没准备好会怎样？
3. future.get() 可以调用多次吗？
4. promise 和 future 的关系是什么？
5. shared state 是什么？
6. packaged_task 是什么？
7. packaged_task 和 promise 有什么区别？
8. packaged_task 支持带参数吗？
9. 为什么线程池内部任务队列存 std::function<void()>？
10. 为什么有返回值任务可以包装成 void()？
11. 为什么使用 shared_ptr<packaged_task>？
12. future<void>::get() 有什么作用？
13. 任务中抛异常会在哪里重新抛出？
14. 当前 submitTask 如何推导返回值类型？
15. 为什么当前 submitTask 只支持无参数任务？
16. 带参数任务当前如何提交？
17. lambda 值捕获为什么在线程池中更安全？
18. 当前 ThreadPool with future 版本有哪些能力边界？
```

---

# 25. Day 8 总结

Day 8 的核心收获：

```text
future：
    异步结果读取端

promise：
    手动写入结果

packaged_task：
    包装任务，自动把返回值或异常写入 shared state

shared state：
    promise / packaged_task 和 future 之间共享的结果状态

submitTask：
    推导任务返回值类型
    用 packaged_task 包装任务
    返回 future
    把 packaged_task 包成 void() wrapper 放入队列
```

最终模型：

```text
submitTask(func)
    ↓
推导 RetType = decltype(func())
    ↓
packaged_task<RetType()> 包装 func
    ↓
future<RetType> 返回给调用者
    ↓
packaged_task 包成 void() wrapper
    ↓
wrapper 放入 TaskQueue
    ↓
worker 执行 wrapper
    ↓
packaged_task 执行 func
    ↓
返回值或异常写入 shared state
    ↓
future.get() 获取结果或重新抛出异常
```

最重要的一句话：

> worker 负责执行任务，future 负责把任务结果或异常带回调用者线程。
