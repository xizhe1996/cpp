# Day 5｜C++ 多线程基础：thread / join / detach / mutex / lock_guard

# 1. 今日学习目标

Day 5 的核心目标：

- 掌握 `std::thread` 的基本创建方式
- 理解 `join()`、`detach()`、`joinable()` 的语义
- 理解 `std::thread` 不可拷贝但可移动的原因
- 掌握线程函数传参的默认行为
- 理解 `std::ref` / `std::cref`
- 掌握线程 lambda 捕获方式和生命周期风险
- 理解数据竞争 data race
- 掌握 `std::mutex` 和 `std::lock_guard`
- 用 RAII 思想实现线程安全计数器

核心主线：

> 多线程代码的核心不是“能创建线程”，而是要正确管理线程生命周期、对象生命周期和共享数据访问。

---

# 2. std::thread 基础

## 2.1 最简单的线程

```cpp
#include <iostream>
#include <thread>

void worker() {
    std::cout << "worker thread running\n";
}

int main() {
    std::thread t(worker);

    t.join();

    std::cout << "main thread end\n";
    return 0;
}
````

这里：

```cpp
std::thread t(worker);
```

表示：

```text
创建一个新线程，在线程中执行 worker 函数。
```

---

## 2.2 std::thread 创建后是否立即执行？

`std::thread` 对象构造成功后，新线程就被创建，并可以开始执行线程函数。

但要注意：

```text
具体什么时候真正获得 CPU 执行，由操作系统调度决定。
```

所以不能假设新线程一定先于 main 线程执行。

---

# 3. join()

## 3.1 join 的作用

```cpp
t.join();
```

含义：

```text
当前线程等待 t 所代表的线程执行完成。
等线程结束后，std::thread 对象不再关联该线程。
joinable() 变为 false。
```

执行流程：

```text
1. main 创建 worker 线程
2. main 调用 join
3. main 阻塞等待 worker 执行完
4. worker 执行结束
5. t 不再关联线程
6. main 继续执行
```

---

## 3.2 join 的语义

```text
我关心这个线程的生命周期。
我要等它执行完。
```

适合：

```text
主线程需要等待工作线程完成。
需要保证任务完成后再退出。
```

---

# 4. detach()

## 4.1 detach 的作用

```cpp
t.detach();
```

含义：

```text
std::thread 对象立刻和真实线程解除关联。
真实线程继续在后台运行。
当前线程不再等待它。
```

调用 `detach()` 后：

```cpp
t.joinable(); // false
```

---

## 4.2 detach 后为什么不一定看到线程输出？

示例：

```cpp
#include <iostream>
#include <thread>
#include <chrono>

void worker() {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::cout << "worker thread running\n";
}

int main() {
    std::thread t(worker);

    t.detach();

    std::cout << "main thread end\n";
    return 0;
}
```

可能只看到：

```text
main thread end
```

原因：

```text
detach 线程仍然属于当前进程。
但 main 函数返回后，进程通常开始结束。
进程结束时，其他仍在运行的线程也会被终止。
worker 可能还没来得及执行打印，进程就结束了。
```

---

## 4.3 detach 的风险

```text
detach 后很难控制线程生命周期。
如果后台线程访问已经销毁的局部变量、this 指针或对象，就会产生悬空引用 / use-after-free。
```

初学和面试中，优先使用：

```text
join
```

谨慎使用：

```text
detach
```

---

# 5. join 和 detach 的区别

| 操作         | 是否等待线程结束 | 调用后 thread 是否 joinable | 语义           |
| ---------- | -------- | ---------------------- | ------------ |
| `join()`   | 是        | 否                      | 等线程结束，回收管理关系 |
| `detach()` | 否        | 否                      | 放弃管理，线程后台运行  |

共同点：

```text
join 和 detach 调用后，std::thread 对象都不再管理该线程。
```

区别：

```text
join 是等线程执行结束后断开关联。
detach 是不等线程结束，直接断开关联。
```

---

# 6. joinable()

```cpp
if (t.joinable()) {
    t.join();
}
```

`joinable()` 表示：

```text
这个 std::thread 对象当前是否还关联着一个尚未 join/detach 的线程。
```

如果 `std::thread` 对象析构时仍然 `joinable()`，会调用：

```text
std::terminate
```

程序直接异常终止。

---

# 7. std::thread 不可拷贝但可以移动

## 7.1 不可拷贝

错误：

```cpp
std::thread t1(worker);
std::thread t2 = t1; // 编译错误
```

原因：

```text
std::thread 表示对真实线程执行流的独占管理权。
一个真实线程不能同时被两个 thread 对象管理。
否则 join/detach 的责任会不清楚。
```

---

## 7.2 可以移动

正确：

```cpp
std::thread t1(worker);
std::thread t2 = std::move(t1);
```

移动后：

```text
t2 接管线程管理权。
t1 不再关联线程。
```

示例：

```cpp
std::cout << t1.joinable() << std::endl; // false
std::cout << t2.joinable() << std::endl; // true

t2.join();
```

类比：

```text
unique_ptr 独占管理堆对象。
thread 独占管理线程执行流。
```

---

# 8. std::thread 传参

## 8.1 默认是拷贝参数

示例：

```cpp
#include <iostream>
#include <thread>

void add(int x) {
    x += 10;
    std::cout << "worker x = " << x << std::endl;
}

int main() {
    int n = 1;

    std::thread t(add, n);
    t.join();

    std::cout << "main n = " << n << std::endl;

    return 0;
}
```

输出：

```text
worker x = 11
main n = 1
```

原因：

```text
std::thread 默认会把传入参数拷贝或移动到线程内部。
worker 拿到的是 n 的副本。
```

---

## 8.2 线程函数参数是引用时，需要 std::ref

```cpp
#include <iostream>
#include <thread>
#include <functional>

void addRef(int& x) {
    x += 10;
}

int main() {
    int n = 1;

    std::thread t(addRef, std::ref(n));
    t.join();

    std::cout << "main n = " << n << std::endl;

    return 0;
}
```

输出：

```text
main n = 11
```

---

## 8.3 std::ref

```cpp
std::ref(n)
```

作用：

```text
告诉 std::thread：不要拷贝 n，而是在线程函数中以普通引用方式传入 n。
```

适合：

```cpp
void f(T& x);
```

---

## 8.4 std::cref

```cpp
std::cref(name)
```

作用：

```text
告诉 std::thread：不要拷贝 name，而是在线程函数中以 const 引用方式传入 name。
```

适合：

```cpp
void f(const T& x);
```

---

## 8.5 const 引用参数和 std::thread

普通函数调用：

```cpp
void printName(const std::string& name);

std::string s = "alice";
printName(s);
```

这里不会拷贝，`const std::string&` 直接绑定外部 `s`。

但在线程中：

```cpp
std::thread t(printName, s);
```

`std::thread` 会先拷贝一份 `s` 保存在线程内部，然后 `printName` 的 const 引用绑定这份内部拷贝。

如果不想拷贝外部对象：

```cpp
std::thread t(printName, std::cref(s));
```

---

## 8.6 std::ref / std::cref 简单记忆

```text
std::thread 默认拷贝参数。
std::ref  表示按普通引用传参。
std::cref 表示按 const 引用传参。
使用 ref / cref 时，必须保证被引用对象在线程执行期间仍然活着。
```

---

# 9. lambda 在线程中的使用

## 9.1 最简单的线程 lambda

```cpp
#include <iostream>
#include <thread>

int main() {
    std::thread t([]() {
        std::cout << "worker running\n";
    });

    t.join();

    std::cout << "main end\n";
    return 0;
}
```

lambda 可以直接作为线程函数。

---

## 9.2 值捕获

```cpp
int x = 10;

std::thread t([x]() {
    std::cout << "thread x = " << x << std::endl;
});

t.join();
```

含义：

```text
[x] 会把 x 拷贝进 lambda 对象内部。
线程中使用的是副本。
```

特点：

```text
更安全。
修改不到外部变量。
```

---

## 9.3 引用捕获

```cpp
int x = 10;

std::thread t([&x]() {
    x += 10;
});

t.join();

std::cout << x << std::endl; // 20
```

含义：

```text
[&x] 捕获外部 x 的引用。
线程中访问的是外部 x 本身。
```

特点：

```text
可以修改外部变量。
必须保证外部变量在线程执行期间仍然活着。
```

---

# 10. detach + 引用捕获的风险

危险代码：

```cpp
#include <iostream>
#include <thread>
#include <chrono>

void startThread() {
    int x = 10;

    std::thread t([&x]() {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << x << std::endl;
    });

    t.detach();
}
```

问题：

```text
x 是 startThread 的局部变量。
startThread 返回后，x 已销毁。
detach 线程还可能继续运行。
线程之后访问 x，会产生悬空引用。
```

正确做法之一：

```cpp
void startThread() {
    int x = 10;

    std::thread t([x]() {
        std::cout << x << std::endl;
    });

    t.detach();
}
```

值捕获让 lambda 拥有自己的副本。

---

# 11. 捕获 this 的风险

危险示例：

```cpp
class Worker {
public:
    void start() {
        std::thread t([this]() {
            run();
        });

        t.detach();
    }

    void run() {
        std::cout << "running\n";
    }
};
```

风险：

```text
[this] 捕获的是当前对象的 this 指针。
如果线程还没执行完，Worker 对象已经销毁。
线程再调用 run()，就是访问已经销毁的对象。
```

典型危险场景：

```cpp
void foo() {
    Worker w;
    w.start();
} // w 析构，但 detach 线程可能还在跑
```

---

# 12. shared_ptr / weak_ptr 在线程 lambda 中的用法

## 12.1 值捕获 shared_ptr 保证对象存活

```cpp
std::shared_ptr<Worker> worker = std::make_shared<Worker>();

std::thread t([worker]() {
    worker->run();
});

t.detach();
```

效果：

```text
lambda 值捕获 shared_ptr。
强引用计数 +1。
只要线程 lambda 还活着，对象就不会被释放。
```

适合：

```text
线程执行期间必须保证对象存活。
```

---

## 12.2 捕获 weak_ptr 观察对象

```cpp
std::weak_ptr<Worker> weakWorker = worker;

std::thread t([weakWorker]() {
    if (auto worker = weakWorker.lock()) {
        worker->run();
    } else {
        std::cout << "worker expired\n";
    }
});

t.detach();
```

语义：

```text
线程不拥有 Worker。
不强行延长对象生命周期。
执行时先检查对象是否还活着。
活着就使用。
不活着就放弃。
```

适合：

```text
不想延长对象生命周期，但又想安全访问对象。
```

---

# 13. 数据竞争 data race

## 13.1 问题示例

```cpp
#include <iostream>
#include <thread>
#include <vector>

int counter = 0;

void increment() {
    for (int i = 0; i < 100000; ++i) {
        ++counter;
    }
}

int main() {
    std::vector<std::thread> threads;

    for (int i = 0; i < 4; ++i) {
        threads.emplace_back(increment);
    }

    for (auto& t : threads) {
        t.join();
    }

    std::cout << counter << std::endl;

    return 0;
}
```

可能期望：

```text
400000
```

但实际可能不是。

---

## 13.2 为什么 ++counter 不安全？

```cpp
++counter;
```

不是原子操作，大致分成：

```text
1. 读取 counter
2. 加 1
3. 写回 counter
```

多个线程交错执行时可能更新丢失：

```text
counter = 100

线程 A 读取 100
线程 B 读取 100

线程 A 写回 101
线程 B 写回 101
```

本来应该变成 `102`，结果只变成 `101`。

---

## 13.3 什么是数据竞争？

数据竞争通常满足：

```text
1. 多个线程访问同一份共享数据
2. 至少有一个线程执行写操作
3. 没有使用 mutex / atomic 等同步机制
```

标准表达：

```text
多个线程并发访问同一对象，其中至少一个访问是写，并且这些访问之间没有同步关系，就会形成数据竞争。
```

在 C++ 中，数据竞争会导致：

```text
未定义行为
```

---

# 14. mutex

## 14.1 mutex 的作用

```cpp
std::mutex mtx;
```

作用：

```text
保护临界区，保证同一时刻只有一个线程进入访问共享资源的代码区域。
```

---

## 14.2 手动 lock/unlock

```cpp
mtx.lock();
++counter;
mtx.unlock();
```

可以保护共享数据，但不推荐作为常规写法。

原因：

```cpp
mtx.lock();

doSomething(); // 如果这里抛异常

mtx.unlock();  // 可能执行不到
```

风险：

```text
忘记 unlock。
异常导致 unlock 执行不到。
其他线程永久阻塞。
```

---

# 15. lock_guard 与 RAII 加锁

## 15.1 lock_guard 基本用法

```cpp
std::lock_guard<std::mutex> lock(mtx);
++counter;
```

构造时：

```text
调用 mtx.lock()
```

析构时：

```text
调用 mtx.unlock()
```

所以它是典型 RAII：

```text
对象创建时获取锁。
对象离开作用域时释放锁。
```

---

## 15.2 lock_guard 的好处

```text
1. 避免忘记 unlock
2. 异常情况下也能自动释放锁
3. 代码更清晰
4. 和 RAII 思想一致
```

---

## 15.3 lock_guard 作用域

锁的作用域应该尽量小，只保护真正需要同步的共享数据访问。

较小锁粒度：

```cpp
void increment() {
    for (int i = 0; i < 100000; ++i) {
        std::lock_guard<std::mutex> lock(mtx);
        ++counter;
    }
}
```

较大锁粒度：

```cpp
void increment() {
    std::lock_guard<std::mutex> lock(mtx);

    for (int i = 0; i < 100000; ++i) {
        ++counter;
    }
}
```

两者都可能正确，但含义不同：

```text
锁粒度越大，其他线程等待时间越长，并发度越低。
```

原则：

```text
锁保护共享数据即可，不要随便扩大锁作用域。
```

---

# 16. 线程安全计数器 SafeCounter

## 16.1 类定义

```cpp
#include <mutex>

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
```

---

## 16.2 increment() 为什么要加锁？

```text
++count_ 不是原子操作。
多个线程同时执行可能产生更新丢失。
加锁可以保护这段临界区，避免数据竞争。
```

---

## 16.3 value() 只是读，为什么也要加锁？

```text
虽然 value() 只是读取 count_，但如果其他线程可能同时调用 increment() 修改 count_，读写并发仍然会形成数据竞争。
因此读取共享变量时也应该用同一把 mutex 保护。
```

---

## 16.4 value() const 中为什么 mutex_ 要 mutable？

```cpp
int value() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return count_;
}
```

`value() const` 中：

```text
this 被视为 const SafeCounter*。
普通成员变量不能被修改。
```

但加锁会修改 `mutex_` 的内部状态。

所以：

```cpp
mutable std::mutex mutex_;
```

表示：

```text
即使在 const 成员函数中，也允许修改 mutex_。
```

这是 `mutable` 的典型使用场景。

---

## 16.5 main 测试示例

```cpp
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
```

预期：

```text
counter = 400000
```

---

# 17. 今日重要面试问答

## Q1：join 和 detach 有什么区别？

答：

`join` 会阻塞当前线程，等待目标线程执行结束，之后 `std::thread` 对象不再关联该线程。`detach` 不等待线程结束，而是让 `std::thread` 对象立刻和真实线程分离，线程在后台继续运行。两者调用后 `joinable()` 都会变为 false。区别是 `join` 保证线程完成，`detach` 不再控制线程生命周期。

---

## Q2：std::thread 析构时仍然 joinable 会怎样？

答：

如果 `std::thread` 对象析构时仍然 `joinable()`，会调用 `std::terminate`，程序直接异常终止。因此线程对象销毁前必须调用 `join()` 或 `detach()`。

---

## Q3：std::thread 为什么不能拷贝，但可以移动？

答：

`std::thread` 表示对真实线程执行流的独占管理权。一个真实线程不能同时被两个 `std::thread` 对象管理，否则 join/detach 的责任不清楚。因此 `std::thread` 禁止拷贝。但管理权可以转移，所以支持移动。移动后目标 thread 接管线程，源 thread 不再 joinable。

---

## Q4：std::thread 传参默认是拷贝还是引用？

答：

默认是拷贝或移动到线程内部存储。即使线程函数参数是 `const T&`，如果直接传外部对象，`std::thread` 也会先保存一份副本，然后在线程函数中用 const 引用绑定这份内部副本。如果想按引用传外部对象，需要显式使用 `std::ref` 或 `std::cref`。

---

## Q5：std::ref / std::cref 有什么作用？

答：

`std::ref` 用于告诉 `std::thread` 按普通引用传参，不要拷贝外部对象；`std::cref` 用于按 const 引用传参。使用它们时，必须保证被引用对象在线程执行期间仍然活着，否则会产生悬空引用。

---

## Q6：detach + 引用捕获局部变量为什么危险？

答：

`detach` 后线程在后台运行，当前函数可能已经返回，局部变量已经销毁。如果线程 lambda 按引用捕获了这个局部变量，之后再访问它就会产生悬空引用，属于未定义行为。

---

## Q7：线程中捕获 this 有什么风险？

答：

`[this]` 捕获的是当前对象的裸指针。如果线程还没执行完，对象已经析构，线程再通过 this 调用成员函数或访问成员变量，就会访问已销毁对象，产生 use-after-free。

---

## Q8：为什么线程 lambda 中值捕获 shared_ptr 可以保证对象存活？

答：

值捕获 `shared_ptr` 会拷贝一份 `shared_ptr` 到 lambda 内部，使强引用计数加 1。只要线程 lambda 还活着，这份 `shared_ptr` 就还在，对象就不会被释放。

---

## Q9：如果不想延长对象生命周期，但又想安全访问对象，可以用什么？

答：

可以使用 `weak_ptr`。线程中捕获 `weak_ptr`，使用前调用 `lock()` 尝试获取 `shared_ptr`。如果对象还活着，就安全访问；如果对象已经释放，`lock()` 返回空，线程放弃访问。

---

## Q10：什么是数据竞争？

答：

多个线程并发访问同一对象，其中至少一个访问是写操作，并且这些访问之间没有 mutex、atomic 等同步机制，就会形成数据竞争。在 C++ 中，数据竞争会导致未定义行为。

---

## Q11：为什么多个线程同时执行 ++counter 不安全？

答：

`++counter` 不是原子操作，通常包含读取、加一、写回三个步骤。多个线程交错执行时可能读取到相同旧值，导致更新丢失。因此需要 mutex 或 atomic 保护。

---

## Q12：mutex 的作用是什么？

答：

`mutex` 用来保护临界区，保证同一时刻只有一个线程进入访问共享资源的代码区域，从而避免并发读写共享数据导致的数据竞争。

---

## Q13：为什么不推荐手动 lock/unlock？

答：

手动 `lock/unlock` 容易忘记 unlock。如果临界区中间发生异常，unlock 可能执行不到，导致锁一直不释放，其他线程永久阻塞。推荐使用 `lock_guard` 通过 RAII 自动管理锁。

---

## Q14：lock_guard 为什么符合 RAII？

答：

`lock_guard` 构造时调用 mutex 的 `lock()` 获取锁，析构时调用 `unlock()` 释放锁。它把锁的生命周期绑定到对象生命周期，离开作用域时自动释放锁，即使发生异常也能正确释放。

---

## Q15：value() 只是读，为什么也要加锁？

答：

如果其他线程可能同时写同一个变量，读操作也需要同步。否则一个线程读、另一个线程写，没有同步关系，也会形成数据竞争。因此读取共享数据时也应该使用同一把 mutex 保护。

---

# 18. 今日易错点总结

```text
1. std::thread 创建后，新线程可以开始执行，但具体执行顺序由操作系统调度决定。
2. std::thread 析构前如果仍然 joinable，会调用 std::terminate。
3. join 会等待线程结束，detach 不等待线程结束。
4. join/detach 调用后，thread 对象都不再 joinable。
5. detach 线程仍属于当前进程，进程结束时它也会被终止。
6. std::thread 不可拷贝，但可以移动。
7. std::thread 默认拷贝参数。
8. 需要引用传参时必须显式使用 std::ref / std::cref。
9. 使用 std::ref / std::cref 时必须保证外部对象生命周期足够长。
10. detach + 引用捕获局部变量非常危险。
11. 捕获 this 本质上是捕获裸指针，也有生命周期风险。
12. shared_ptr 值捕获可以延长对象生命周期。
13. weak_ptr 可以安全观察对象而不延长生命周期。
14. ++counter 不是原子操作。
15. 多线程读写共享变量必须同步。
16. mutex 保护临界区。
17. 不推荐手动 lock/unlock，推荐 lock_guard。
18. value() 这种读函数在并发写存在时也需要加锁。
19. const 成员函数中使用 mutex，mutex 通常要声明为 mutable。
20. 锁作用域不要随便放太大。
```

---

# 19. 今日核心代码片段

## 19.1 创建线程并 join

```cpp
std::thread t(worker);

if (t.joinable()) {
    t.join();
}
```

---

## 19.2 detach

```cpp
std::thread t(worker);
t.detach();
```

---

## 19.3 thread move

```cpp
std::thread t1(worker);
std::thread t2 = std::move(t1);

std::cout << t1.joinable() << std::endl; // false
std::cout << t2.joinable() << std::endl; // true

t2.join();
```

---

## 19.4 std::ref

```cpp
void addRef(int& x) {
    x += 10;
}

int n = 1;

std::thread t(addRef, std::ref(n));
t.join();
```

---

## 19.5 std::cref

```cpp
void printName(const std::string& name) {
    std::cout << name << std::endl;
}

std::string name = "alice";

std::thread t(printName, std::cref(name));
t.join();
```

---

## 19.6 线程 lambda 值捕获

```cpp
int x = 10;

std::thread t([x]() {
    std::cout << x << std::endl;
});

t.join();
```

---

## 19.7 线程 lambda 引用捕获

```cpp
int x = 10;

std::thread t([&x]() {
    x += 10;
});

t.join();
```

---

## 19.8 shared_ptr 捕获

```cpp
auto worker = std::make_shared<Worker>();

std::thread t([worker]() {
    worker->run();
});

t.detach();
```

---

## 19.9 weak_ptr 捕获

```cpp
std::weak_ptr<Worker> weakWorker = worker;

std::thread t([weakWorker]() {
    if (auto worker = weakWorker.lock()) {
        worker->run();
    }
});

t.detach();
```

---

## 19.10 lock_guard

```cpp
std::mutex mtx;
int counter = 0;

void increment() {
    std::lock_guard<std::mutex> lock(mtx);
    ++counter;
}
```

---

## 19.11 SafeCounter

```cpp
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
```

---

# 20. 明日开始前复习问题

```text
1. std::thread 创建后是否立即开始执行？
2. join() 的作用是什么？
3. detach() 的作用是什么？
4. join 和 detach 的共同点和区别是什么？
5. thread 析构时仍然 joinable 会发生什么？
6. std::thread 为什么不能拷贝但可以移动？
7. std::thread 传参默认是拷贝还是引用？
8. std::ref 和 std::cref 的作用是什么？
9. 使用 std::ref / std::cref 时最需要注意什么？
10. detach + 引用捕获局部变量为什么危险？
11. 捕获 this 在线程中有什么风险？
12. shared_ptr 值捕获为什么能保证对象存活？
13. weak_ptr 在线程中怎么安全访问对象？
14. 什么是数据竞争？
15. 为什么 ++counter 不是线程安全的？
16. mutex 的作用是什么？
17. 为什么不推荐手动 lock/unlock？
18. lock_guard 为什么符合 RAII？
19. value() 只是读，为什么也要加锁？
20. value() const 中为什么 mutex_ 要 mutable？
```

---

# 21. Day 5 总结

Day 5 的核心收获：

```text
thread：管理线程执行流。
join：等待线程完成。
detach：放弃管理，后台运行。
ref/cref：显式引用传参。
lambda：注意捕获对象生命周期。
mutex：保护共享数据。
lock_guard：用 RAII 管理锁。
```

最终形成的工程判断：

```text
线程对象销毁前必须 join 或 detach。
优先 join，谨慎 detach。
线程默认拷贝参数，需要引用时用 ref/cref。
引用捕获和 this 捕获都要特别注意生命周期。
线程中需要对象存活，可以捕获 shared_ptr。
不想延长生命周期，可以捕获 weak_ptr 并 lock。
共享数据并发读写必须同步。
手动 lock/unlock 不如 lock_guard 安全。
锁的作用域应该尽量小，只保护必要的共享数据访问。
```