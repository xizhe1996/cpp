# Day 6｜C++ 多线程同步：condition_variable / unique_lock / BlockingQueue / TaskQueue

# 1. 今日学习目标

Day 6 的核心目标：

- 理解为什么只靠 `mutex` 不够
- 掌握 `std::condition_variable` 的等待与通知机制
- 理解 `std::unique_lock` 为什么配合 `condition_variable` 使用
- 掌握 `wait(lock, predicate)` 的正确写法
- 理解虚假唤醒和丢失唤醒
- 实现一个基础阻塞队列 `BlockingQueue`
- 给阻塞队列增加 `close / stop` 机制
- 将 `BlockingQueue<int>` 泛化为线程池任务队列 `TaskQueue`
- 理解任务队列和线程池 worker 循环的关系

核心主线：

> mutex 解决共享数据的互斥访问；condition_variable 解决线程之间的等待与通知；BlockingQueue / TaskQueue 是线程池的核心基础组件。

---

# 2. 为什么只靠 mutex 不够？

## 2.1 mutex 解决什么问题？

`mutex` 解决的是：

```text
多个线程访问同一份共享数据时的互斥问题。
````

例如：

```cpp
std::mutex mtx;
int counter = 0;

void increment() {
    std::lock_guard<std::mutex> lock(mtx);
    ++counter;
}
```

含义：

```text
同一时刻只允许一个线程进入临界区修改 counter。
```

所以：

```text
mutex 管的是“访问共享数据是否安全”。
```

---

## 2.2 但有些场景需要“等待条件成立”

例如生产者-消费者模型：

```text
生产者：向队列 push 数据
消费者：从队列 pop 数据
```

如果消费者发现队列为空，不能直接：

```cpp
q.front();
q.pop();
```

这会出错。

也不应该一直循环检查：

```cpp
while (true) {
    std::lock_guard<std::mutex> lock(mtx);
    if (!q.empty()) {
        // consume
    }
}
```

这种方式叫：

```text
busy waiting / 忙等
```

问题：

```text
即使队列为空，消费者线程也会一直循环、抢锁、检查条件，浪费 CPU。
```

---

## 2.3 更合理的方式

理想模型：

```text
消费者发现队列为空：
    释放锁
    进入睡眠

生产者放入数据：
    通知消费者醒来
```

这就需要：

```cpp
std::condition_variable
```

---

# 3. condition_variable 的作用

`condition_variable` 用来：

```text
让线程等待某个条件成立，并在条件可能成立时被其他线程通知唤醒。
```

例如：

```text
消费者等待：队列非空
生产者通知：我 push 了数据，队列可能非空了
```

注意：

```text
condition_variable 不是 mutex 的替代品。
condition_variable 通常必须和 mutex 配合使用。
```

原因：

```text
condition_variable 等待的条件通常依赖共享数据。
共享数据的检查和修改必须由 mutex 保护。
```

---

# 4. mutex 与 condition_variable 的区别

```text
mutex 负责：共享数据访问是否安全。
condition_variable 负责：线程什么时候睡眠、什么时候被唤醒。
```

以队列为例：

```text
mutex：
    保护 q.push / q.pop / q.empty 这些共享数据操作。

condition_variable：
    队列为空时让消费者睡眠。
    队列有数据时通知消费者醒来。
```

---

# 5. condition_variable 基本模型

## 5.1 共享数据

```cpp
#include <condition_variable>
#include <mutex>
#include <queue>

std::mutex mtx;
std::condition_variable cv;
std::queue<int> q;
```

含义：

```text
mtx：保护共享队列 q
cv：用于等待和通知
q：共享数据队列
```

---

## 5.2 生产者

```cpp
void producer() {
    {
        std::lock_guard<std::mutex> lock(mtx);
        q.push(42);
    }

    cv.notify_one();
}
```

逻辑：

```text
1. 加锁
2. 修改共享队列
3. 解锁
4. 通知一个等待线程
```

`notify_one()` 通常放在锁作用域外：

```cpp
{
    std::lock_guard<std::mutex> lock(mtx);
    q.push(42);
}

cv.notify_one();
```

原因：

```text
这样被唤醒的消费者不用马上和生产者抢同一把还没释放的锁。
```

这不是绝对必须，但很常见。

---

## 5.3 消费者

```cpp
void consumer() {
    std::unique_lock<std::mutex> lock(mtx);

    cv.wait(lock, [] {
        return !q.empty();
    });

    int value = q.front();
    q.pop();

    std::cout << value << std::endl;
}
```

重点：

```cpp
cv.wait(lock, [] {
    return !q.empty();
});
```

含义：

```text
如果队列非空，直接继续执行。
如果队列为空，释放 mutex，并让当前线程睡眠。
被 notify 唤醒后，重新获取 mutex，再检查队列是否非空。
条件满足才继续执行。
```

---

# 6. unique_lock 为什么配合 condition_variable 使用？

`condition_variable::wait()` 内部需要：

```text
释放 mutex
线程睡眠
被唤醒后重新获取 mutex
```

所以它需要能手动 unlock / lock 的锁类型：

```cpp
std::unique_lock<std::mutex>
```

不能用：

```cpp
std::lock_guard<std::mutex>
```

因为 `lock_guard` 太简单：

```text
构造时 lock
析构时 unlock
中途不能手动 unlock / lock
```

因此：

```cpp
std::unique_lock<std::mutex> lock(mtx);
cv.wait(lock, predicate);
```

是标准写法。

---

# 7. wait(lock, predicate) 的执行过程

```cpp
cv.wait(lock, [] {
    return !q.empty();
});
```

可以理解为：

```text
1. 调用 wait 前，线程已经持有 mutex。
2. 检查 predicate。
3. 如果 predicate 为 true，直接继续执行。
4. 如果 predicate 为 false：
   - wait 内部释放 mutex
   - 当前线程进入睡眠
5. 被 notify 或虚假唤醒后：
   - wait 内部先重新获取 mutex
   - 再重新检查 predicate
6. predicate 为 true 才继续执行。
7. predicate 为 false 则继续释放锁并睡眠。
```

关键顺序：

```text
被唤醒
→ 重新竞争 mutex
→ 拿到 mutex
→ 检查 predicate
→ 条件满足才继续执行
```

---

# 8. wait 等的是通知还是条件？

重要结论：

```text
wait 最终等的是“条件成立”，不是单纯等通知。
notify 只是让等待线程醒来重新检查条件。
```

可以记成：

```text
通知只是闹钟。
条件才是通行证。
```

比如：

```cpp
cv.wait(lock, [] {
    return !q.empty();
});
```

真正可靠的是：

```cpp
!q.empty()
```

而不是：

```cpp
notify_one()
```

---

# 9. producer 先 notify，consumer 后 wait，会不会卡死？

如果使用 predicate 版本：

```cpp
cv.wait(lock, [] {
    return !q.empty();
});
```

不会。

原因：

```text
condition_variable 不保存通知。
但共享状态 queue 会保留。
```

如果 producer 已经先执行：

```cpp
q.push(42);
cv.notify_one();
```

即使 consumer 后进入 wait，它也会先检查：

```cpp
!q.empty()
```

此时条件已经成立，所以不会睡眠。

核心结论：

```text
notify 可能丢。
状态不会丢。
wait 应该等状态条件，而不是等通知本身。
```

---

# 10. 虚假唤醒

## 10.1 什么是虚假唤醒？

虚假唤醒可以简单理解为：

```text
线程从 condition_variable::wait 返回了，
但等待的条件并没有真正成立。
```

C++ 允许这种情况存在。

所以：

```text
wait 返回不代表条件一定成立。
```

---

## 10.2 错误写法：if + wait

```cpp
std::unique_lock<std::mutex> lock(mtx);

if (q.empty()) {
    cv.wait(lock);
}

int value = q.front();
q.pop();
```

这个写法不安全。

原因：

```text
wait 返回后没有再次检查条件。
```

可能出现：

```text
1. 虚假唤醒
2. notify_all 唤醒多个消费者，但任务被其他消费者抢走
3. 条件被其他线程改变
```

---

## 10.3 正确写法 1：while + wait

```cpp
std::unique_lock<std::mutex> lock(mtx);

while (q.empty()) {
    cv.wait(lock);
}

int value = q.front();
q.pop();
```

含义：

```text
只要队列为空，就继续等待。
每次 wait 返回后，都重新检查条件。
```

---

## 10.4 正确写法 2：predicate 版本

推荐：

```cpp
cv.wait(lock, [] {
    return !q.empty();
});
```

它等价于：

```cpp
while (!predicate()) {
    cv.wait(lock);
}
```

也就是：

```cpp
while (q.empty()) {
    cv.wait(lock);
}
```

更简洁，也更不容易写错。

---

# 11. 多消费者为什么必须重新检查条件？

假设队列里只有一个任务：

```text
1. producer push 一个任务
2. notify_all 唤醒多个消费者
3. consumer A 抢到锁，pop 任务
4. consumer B 后抢到锁，此时队列已经空了
```

如果 consumer B 不重新检查条件，直接：

```cpp
q.front();
q.pop();
```

就会出错。

所以即使被通知，也必须重新检查：

```cpp
cv.wait(lock, [] {
    return !q.empty();
});
```

---

# 12. 没有队列时，也应该有状态变量

如果不是队列场景，而是“收到通知后做事”，也不应该只写：

```cpp
cv.wait(lock);
doSomething();
```

应该设计状态变量：

```cpp
bool ready = false;
```

示例：

```cpp
std::mutex mtx;
std::condition_variable cv;
bool ready = false;

void worker() {
    std::unique_lock<std::mutex> lock(mtx);

    cv.wait(lock, [] {
        return ready;
    });

    std::cout << "worker do something\n";
}

int main() {
    std::thread t(worker);

    {
        std::lock_guard<std::mutex> lock(mtx);
        ready = true;
    }

    cv.notify_one();

    t.join();
}
```

核心：

```text
ready = true 才是“事情发生了”的状态。
notify_one() 只是提醒 worker 醒来检查 ready。
```

---

# 13. 多次通知时用计数器或队列

## 13.1 用计数器

```cpp
int pending = 0;
```

生产者：

```cpp
{
    std::lock_guard<std::mutex> lock(mtx);
    ++pending;
}
cv.notify_one();
```

消费者：

```cpp
std::unique_lock<std::mutex> lock(mtx);

cv.wait(lock, [] {
    return pending > 0;
});

--pending;

lock.unlock();

doSomething();
```

---

## 13.2 用队列

```cpp
std::queue<Task> tasks;
```

生产者：

```cpp
{
    std::lock_guard<std::mutex> lock(mtx);
    tasks.push(task);
}
cv.notify_one();
```

消费者：

```cpp
std::unique_lock<std::mutex> lock(mtx);

cv.wait(lock, [] {
    return !tasks.empty();
});

Task task = std::move(tasks.front());
tasks.pop();

lock.unlock();

task();
```

队列就是更通用的：

```text
状态条件 + 数据载体
```

---

# 14. BlockingQueue 基础版

## 14.1 类结构

```cpp
#include <condition_variable>
#include <mutex>
#include <queue>

class BlockingQueue {
public:
    void push(int value);
    int pop();

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<int> queue_;
};
```

---

## 14.2 push

```cpp
void push(int value) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(value);
    }

    cv_.notify_one();
}
```

逻辑：

```text
1. 加锁
2. push 数据到队列
3. 解锁
4. notify_one 通知消费者
```

---

## 14.3 pop

```cpp
int pop() {
    std::unique_lock<std::mutex> lock(mutex_);

    cv_.wait(lock, [this] {
        return !queue_.empty();
    });

    int value = queue_.front();
    queue_.pop();

    return value;
}
```

逻辑：

```text
1. 加锁
2. 如果队列为空，释放锁并睡眠
3. 队列非空后重新获得锁
4. 取出 front
5. pop 队列
6. 返回数据
```

---

## 14.4 基础版 BlockingQueue 的问题

基础版：

```cpp
int pop();
```

语义是：

```text
只要队列为空，就一直等待。
```

问题：

```text
如果生产者不再 push，消费者可能永远阻塞在 pop() 中。
线程池析构时，如果 worker 卡在 pop()，join 会永远等待，程序卡死。
```

所以需要：

```text
close / stop / shutdown 机制
```

---

# 15. BlockingQueue close 版本

## 15.1 新接口

```cpp
class BlockingQueue {
public:
    bool push(int value);
    bool pop(int& value);
    void close();

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<int> queue_;
    bool closed_ = false;
};
```

语义：

```text
push(value):
    队列未关闭：插入数据，返回 true。
    队列已关闭：插入失败，返回 false。

pop(value):
    队列有数据：取出数据，返回 true。
    队列为空但未关闭：阻塞等待。
    队列为空且已关闭：返回 false。

close():
    标记队列关闭。
    唤醒所有等待的消费者。
```

---

## 15.2 push

```cpp
bool push(int value) {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (closed_) {
            return false;
        }

        queue_.push(value);
    }

    cv_.notify_one();
    return true;
}
```

为什么 `closed_ == true` 时返回 false？

```text
close 表示队列不再接受新任务。
关闭后继续 push 会让退出逻辑变得混乱。
```

---

## 15.3 pop

```cpp
bool pop(int& value) {
    std::unique_lock<std::mutex> lock(mutex_);

    cv_.wait(lock, [this] {
        return closed_ || !queue_.empty();
    });

    if (queue_.empty()) {
        return false;
    }

    value = queue_.front();
    queue_.pop();
    return true;
}
```

等待条件：

```cpp
closed_ || !queue_.empty()
```

含义：

```text
队列有数据：可以消费。
队列关闭：即使没有数据，也要醒来退出。
```

为什么 `queue_.empty()` 时返回 false？

因为 wait 已经保证：

```cpp
closed_ || !queue_.empty()
```

如果此时：

```cpp
queue_.empty()
```

那说明：

```cpp
closed_ == true
```

所以应该返回 false，告诉消费者退出。

---

## 15.4 close

```cpp
void close() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
    }

    cv_.notify_all();
}
```

为什么用 `notify_all()`？

```text
可能有多个消费者线程都在等待。
关闭队列时，必须唤醒所有等待线程，让它们检查 closed_ 并退出。
```

如果只用 `notify_one()`：

```text
可能只唤醒一个消费者，其他消费者继续睡眠，程序 join 时卡住。
```

---

## 15.5 close 后是否继续消费剩余数据？

当前设计：

```text
close 后不再接受新数据。
但队列中已有的数据仍然可以继续消费。
队列彻底为空后，pop 返回 false。
```

这很适合任务队列：

```text
关闭队列
处理完已经提交的任务
worker 退出
```

---

## 15.6 多消费者下是否会异常退出？

不会。

因为：

```cpp
cv_.wait(lock, [this] {
    return closed_ || !queue_.empty();
});
```

只有下面任一条件成立，wait 才会返回：

```text
closed_ == true
queue_ 非空
```

如果当前是：

```text
closed_ == false
queue_.empty() == true
```

消费者不会从 wait 返回。

所以：

```cpp
if (queue_.empty()) return false;
```

只有在：

```text
queue_ 为空且 closed_ 为 true
```

时才会发生。

正常未关闭状态下，即使虚假唤醒或多个消费者竞争，也不会异常退出。

---

## 15.7 完整 BlockingQueue 代码

```cpp
#include <condition_variable>
#include <mutex>
#include <queue>

class BlockingQueue {
public:
    bool push(int value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (closed_) {
                return false;
            }

            queue_.push(value);
        }

        cv_.notify_one();
        return true;
    }

    bool pop(int& value) {
        std::unique_lock<std::mutex> lock(mutex_);

        cv_.wait(lock, [this] {
            return closed_ || !queue_.empty();
        });

        if (queue_.empty()) {
            return false;
        }

        value = queue_.front();
        queue_.pop();
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
    std::queue<int> queue_;
    bool closed_ = false;
};
```

---

# 16. 从 BlockingQueue 到 TaskQueue

线程池中，队列里通常不存 `int`，而是存任务：

```cpp
std::function<void()>
```

任务可以是：

```cpp
[] { std::cout << "task\n"; }
```

也可以是：

```cpp
[conn] { conn->handleRead(); }
```

也可以是：

```cpp
std::bind(&Worker::run, worker)
```

这些都可以包装成：

```cpp
std::function<void()>
```

worker 只需要统一执行：

```cpp
task();
```

---

# 17. TaskQueue 类结构

```cpp
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>

class TaskQueue {
public:
    bool push(std::function<void()> task);
    bool pop(std::function<void()>& task);
    void close();

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::function<void()>> tasks_;
    bool closed_ = false;
};
```

---

# 18. TaskQueue 实现

```cpp
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
```

---

# 19. 为什么 push 中要 move task？

```cpp
tasks_.push(std::move(task));
```

原因：

```text
task 是 push 函数按值接收的参数。
进入函数后已经是当前函数自己的局部对象。
把它放入队列后，当前函数不再需要它。
所以可以 move 进队列，避免不必要的 std::function 拷贝。
```

---

# 20. 为什么 pop 中要 move tasks_.front()？

```cpp
task = std::move(tasks_.front());
tasks_.pop();
```

原因：

```text
队列头部任务马上要被 pop 删除。
可以把它 move 到输出参数 task 中，避免拷贝。
```

---

# 21. worker 如何使用 TaskQueue？

```cpp
void worker(TaskQueue& queue) {
    std::function<void()> task;

    while (queue.pop(task)) {
        task();
    }

    std::cout << "worker exit\n";
}
```

逻辑：

```text
1. 从任务队列 pop 一个任务。
2. 如果 pop 返回 true，执行 task()。
3. 如果 pop 返回 false，说明队列关闭且没有任务了，退出 worker 循环。
```

---

# 22. 为什么 task() 必须在锁外执行？

`pop()` 内部只在取任务时加锁。

```cpp
while (queue.pop(task)) {
    task();
}
```

当 `pop()` 返回后，锁已经释放。

所以：

```cpp
task();
```

是在锁外执行。

这是正确设计。

原因：

```text
锁只应该保护任务队列的 push/pop 操作。
任务执行可能很耗时。
如果持锁执行任务：
    其他线程不能 push 新任务
    其他 worker 不能 pop 任务
    队列并发度极差
    甚至可能死锁
```

原则：

```text
锁只保护共享数据结构，不包裹耗时业务逻辑。
```

---

# 23. 最小 TaskQueue 测试程序

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

int main() {
    TaskQueue queue;

    std::vector<std::thread> workers;

    for (int i = 0; i < 2; ++i) {
        workers.emplace_back([&queue, i]() {
            std::function<void()> task;

            while (queue.pop(task)) {
                task();
            }

            std::cout << "worker " << i << " exit\n";
        });
    }

    for (int i = 0; i < 5; ++i) {
        queue.push([i]() {
            std::cout << "task " << i << std::endl;
        });
    }

    queue.close();

    for (auto& worker : workers) {
        worker.join();
    }

    return 0;
}
```

---

# 24. TaskQueue 和线程池的关系

一个最小线程池大概就是：

```text
TaskQueue queue_;

多个 worker thread:
    while (queue_.pop(task)) {
        task();
    }

submit(task):
    queue_.push(task)

shutdown:
    queue_.close()
    join 所有 worker
```

所以：

```text
TaskQueue 是线程池的核心组件之一。
worker 线程循环从 TaskQueue 中取任务并执行。
close 用来让 worker 退出。
```

Day 6 写的内容，已经是线程池的一半。

---

# 25. 今日重要面试问答

## Q1：mutex 和 condition_variable 分别解决什么问题？

答：

`mutex` 解决共享数据的互斥访问问题，保证同一时刻只有一个线程进入临界区访问共享资源。`condition_variable` 解决线程之间的等待与通知问题，让线程在条件不满足时睡眠，在条件可能满足时被通知唤醒。二者通常配合使用，因为等待条件通常依赖被 mutex 保护的共享数据。

---

## Q2：为什么 condition_variable 通常要和 mutex 一起使用？

答：

因为等待的条件通常依赖共享状态，例如队列是否为空。这个共享状态可能被多个线程读写，必须由 mutex 保护。wait 时也需要在释放锁、睡眠、被唤醒后重新加锁的过程中保持状态检查的正确性。

---

## Q3：为什么 wait 需要 unique_lock 而不是 lock_guard？

答：

`condition_variable::wait()` 在等待时需要临时释放 mutex，让其他线程可以修改共享状态；被唤醒后又要重新获取 mutex。`lock_guard` 只能构造时加锁、析构时解锁，不能中途 unlock/lock；`unique_lock` 支持灵活的 lock/unlock，所以 wait 需要 `unique_lock`。

---

## Q4：wait(lock, predicate) 大概做了什么？

答：

它会先检查 predicate。如果条件已经成立，直接继续执行；如果条件不成立，就释放 mutex 并让线程睡眠。被 notify 或虚假唤醒后，线程会重新获取 mutex，再次检查 predicate。只有 predicate 为 true 时才继续执行，否则继续等待。

---

## Q5：wait 唤醒后是先拿锁还是先检查条件？

答：

先重新获取 mutex，再检查 predicate。因为 predicate 通常访问共享数据，比如队列是否为空，必须在 mutex 保护下检查。

---

## Q6：wait 等的是通知还是条件？

答：

wait 最终等的是条件成立，不是单纯等通知。notify 只是让等待线程醒来重新检查条件。condition_variable 不保存通知，真正可靠的是被 mutex 保护的共享状态。

---

## Q7：什么是虚假唤醒？

答：

虚假唤醒是指线程从 condition_variable::wait 返回了，但等待的条件并没有真正成立。C++ 允许这种情况存在，因此 wait 返回后必须重新检查条件。

---

## Q8：为什么不能写 if(q.empty()) cv.wait(lock)？

答：

因为 wait 返回不代表条件一定成立。可能发生虚假唤醒，也可能多个消费者被唤醒后任务已被其他消费者取走。使用 if 只检查一次条件，wait 返回后就直接执行，可能访问空队列。应该使用 while 循环反复检查，或者使用 `wait(lock, predicate)`。

---

## Q9：producer 先 notify，consumer 后 wait，会不会丢通知导致卡死？

答：

如果只依赖通知，可能有丢失唤醒问题。condition_variable 不保存通知。但如果使用 predicate，例如 `wait(lock, [] { return !q.empty(); })`，consumer 后进入 wait 时会先检查队列状态。如果队列已经非空，就不会睡眠，所以不会卡死。可靠的是共享状态，不是通知本身。

---

## Q10：BlockingQueue 的 close 机制解决什么问题？

答：

基础 BlockingQueue 在队列为空时会一直等待，如果生产者不再 push，消费者可能永远阻塞。close 机制提供退出条件：队列关闭且为空时，pop 返回 false，消费者退出循环。这样线程池析构时可以唤醒 worker 并 join 线程，避免卡死。

---

## Q11：pop 等待条件为什么是 closed_ || !queue_.empty()？

答：

消费者醒来的原因有两个：一是队列有数据，可以消费；二是队列关闭，即使没有数据也需要退出。如果只等 `!queue_.empty()`，队列关闭且为空时消费者无法被唤醒退出。

---

## Q12：close 为什么 notify_all 而不是 notify_one？

答：

因为可能有多个消费者或 worker 都阻塞在 pop 中。关闭队列时必须唤醒所有等待线程，让它们检查到 closed_ 并在队列空时退出。如果只 notify_one，其他等待线程可能永远睡眠，导致 join 卡住。

---

## Q13：close 后如果队列还有任务，是否应该直接退出？

答：

当前设计中不直接退出。close 表示不再接收新任务，但队列中已有任务仍然会被消费。只有队列为空且 closed_ 为 true 时，pop 才返回 false，让消费者退出。

---

## Q14：为什么 TaskQueue 适合存 std::function<void()>？

答：

`std::function<void()>` 可以包装不同形式的可调用对象，比如普通函数、lambda、bind 后的函数对象等。线程池 worker 不关心任务具体类型，只需要统一调用 `task()` 执行任务。

---

## Q15：为什么任务执行 task() 要放在锁外？

答：

锁应该只保护任务队列的 push/pop 操作。任务本身可能很耗时，如果持锁执行 task，会阻塞其他线程提交任务或 worker 取任务，降低并发度，甚至可能引发死锁。因此 pop 出任务后应释放锁，再执行 task。

---

# 26. 今日易错点总结

```text
1. mutex 解决互斥访问，不解决等待通知问题。
2. condition_variable 解决等待和通知，但不保护共享数据。
3. condition_variable 通常必须和 mutex 一起使用。
4. wait 需要 unique_lock，因为 wait 内部要释放锁并重新加锁。
5. wait 返回不代表条件一定成立。
6. condition_variable 允许虚假唤醒。
7. 不要用 if + wait，应该用 while + wait 或 wait(lock, predicate)。
8. wait 最终等的是条件成立，不是通知。
9. condition_variable 不保存通知，notify 可能丢。
10. 可靠的是共享状态，比如 ready、pending、queue。
11. producer 先 notify 不会导致 predicate 版本 wait 卡死，因为 wait 会先检查条件。
12. notify_one 通常用于新增一个任务。
13. close/shutdown 通常要 notify_all。
14. close 后已有任务通常继续消费。
15. pop 返回 false 表示队列已关闭且没有数据。
16. 多消费者下，predicate 会防止队列空时异常退出。
17. 任务队列中 task() 必须在锁外执行。
18. TaskQueue 是线程池 worker 循环的核心基础。
```

---

# 27. 今日核心代码片段

## 27.1 condition_variable 基本等待

```cpp
std::unique_lock<std::mutex> lock(mtx);

cv.wait(lock, [] {
    return !q.empty();
});
```

---

## 27.2 while + wait

```cpp
std::unique_lock<std::mutex> lock(mtx);

while (q.empty()) {
    cv.wait(lock);
}
```

---

## 27.3 predicate wait

```cpp
cv.wait(lock, [] {
    return !q.empty();
});
```

---

## 27.4 producer

```cpp
{
    std::lock_guard<std::mutex> lock(mtx);
    q.push(value);
}

cv.notify_one();
```

---

## 27.5 close

```cpp
void close() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
    }

    cv_.notify_all();
}
```

---

## 27.6 BlockingQueue pop with close

```cpp
bool pop(int& value) {
    std::unique_lock<std::mutex> lock(mutex_);

    cv_.wait(lock, [this] {
        return closed_ || !queue_.empty();
    });

    if (queue_.empty()) {
        return false;
    }

    value = queue_.front();
    queue_.pop();
    return true;
}
```

---

## 27.7 TaskQueue push

```cpp
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
```

---

## 27.8 TaskQueue pop

```cpp
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
```

---

## 27.9 worker loop

```cpp
std::function<void()> task;

while (queue.pop(task)) {
    task();
}

std::cout << "worker exit\n";
```

---

# 28. 明日开始前复习问题

```text
1. mutex 和 condition_variable 分别解决什么问题？
2. 为什么 condition_variable 通常要配合 mutex？
3. wait 为什么需要 unique_lock？
4. wait(lock, predicate) 的执行流程是什么？
5. wait 被唤醒后，是先拿锁还是先检查 predicate？
6. wait 等的是通知还是条件？
7. 什么是虚假唤醒？
8. 为什么 if + wait 不安全？
9. 为什么推荐 wait(lock, predicate)？
10. producer 先 notify，consumer 后 wait，为什么不一定卡死？
11. condition_variable 是否保存通知？
12. 如果没有队列，只有“收到通知做事”，应该如何设计状态变量？
13. BlockingQueue 基础版有什么问题？
14. close 机制解决什么问题？
15. pop 的等待条件为什么是 closed_ || !queue_.empty()？
16. close 为什么要 notify_all？
17. close 后队列还有任务，应该直接退出吗？
18. TaskQueue 为什么存 std::function<void()>？
19. push/pop 中为什么要 move task？
20. 为什么 task() 必须在锁外执行？
```

---

# 29. Day 6 总结

Day 6 的核心收获：

```text
mutex：保护共享数据。
condition_variable：等待条件成立并通知线程醒来。
unique_lock：支持 wait 期间释放锁和重新加锁。
predicate：防止虚假唤醒和丢失唤醒问题。
BlockingQueue：生产者消费者模型基础。
close：让等待中的消费者可以退出。
TaskQueue：线程池任务队列的核心。
```

最终形成的工程判断：

```text
只靠 mutex 会导致消费者忙等。
condition_variable 让消费者在条件不满足时睡眠。
wait 不是等通知，而是等条件成立。
notify 只是提醒线程重新检查共享状态。
共享状态必须由 mutex 保护。
不要用 if + wait，要用 while + wait 或 wait(lock, predicate)。
队列关闭时要 notify_all 唤醒所有等待线程。
任务执行必须在锁外进行。
TaskQueue + worker loop 就是线程池的核心骨架。
```
