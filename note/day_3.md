# Day 3｜move 语义、lambda、STL 算法与关联容器

# 1. 今日学习目标

Day 3 的核心目标：

- 进一步理解拷贝与移动的区别
- 理解 `std::move` 的正确使用和常见误用
- 掌握 lambda 基础语法、捕获方式和生命周期风险
- 掌握 lambda 在 `sort / find_if / remove_if / for_each` 中的常见用法
- 掌握 `unordered_map` 的基础操作和 `operator[]` 的副作用
- 掌握 `map / unordered_map` 的常用接口、返回值与使用场景
- 理解 `map` 与 `unordered_map` 的区别

核心主线：

> move 语义解决的是资源所有权转移问题；lambda 解决的是局部函数对象表达问题；map/unordered_map 解决的是 key-value 查找问题。

---

# 2. 拷贝与移动的本质区别

## 2.1 拷贝

拷贝的本质：

```text
复制资源内容。
两个对象各自拥有独立资源。
````

例如：

```text
b1.data_ → 内存 A
b2.data_ → 内存 B

A 和 B 内容相同，但不是同一块内存。
```

---

## 2.2 移动

移动的本质：

```text
转移资源所有权。
新对象接管旧对象的资源。
旧对象变成有效但不再拥有原资源的状态。
```

例如：

```text
移动前：
b1.data_ → 内存 A

移动后：
b2.data_ → 内存 A
b1.data_ → nullptr
```

移动通常比拷贝更高效，因为不需要重新申请资源和复制内容。

---

# 3. 栈对象能不能移动？

可以。

但要注意：

```text
移动的不是对象本体所在的栈内存，而是对象内部管理的资源所有权。
```

例如：

```cpp
Buffer b1(1024);
Buffer b3 = std::move(b1);
```

`b1` 和 `b3` 都是栈对象。

移动前：

```text
b1 在栈上：
size_ = 1024
data_ = 0x1000 → 堆内存 A
```

移动后：

```text
b1 在栈上：
size_ = 0
data_ = nullptr

b3 在栈上：
size_ = 1024
data_ = 0x1000 → 堆内存 A
```

结论：

```text
栈对象本体没有被搬走。
被转移的是它内部管理的堆资源、fd、socket、buffer 等资源所有权。
```

---

# 4. 哪些对象移动有意义？

移动语义主要对内部管理资源的对象有价值，例如：

* `std::string`
* `std::vector`
* `std::unique_ptr`
* `std::fstream`
* 自定义 `Buffer`
* 自定义 `FdGuard`
* socket wrapper

对于只包含普通值类型的对象，例如：

```cpp
struct Point {
    int x;
    int y;
};
```

移动和拷贝通常没有本质区别。

---

# 5. Buffer 示例：观察拷贝与移动

```cpp
#include <cstring>
#include <iostream>

class Buffer {
public:
    Buffer(size_t size)
        : size_(size), data_(new char[size]) {
        std::memset(data_, 0, size_);
        std::cout << "constructor size=" << size_ << std::endl;
    }

    ~Buffer() {
        std::cout << "destructor size=" << size_ << std::endl;
        delete[] data_;
    }

    Buffer(const Buffer& other)
        : size_(other.size_), data_(new char[other.size_]) {
        std::memcpy(data_, other.data_, size_);
        std::cout << "copy constructor size=" << size_ << std::endl;
    }

    Buffer& operator=(const Buffer& other) {
        std::cout << "copy assignment\n";

        if (this == &other) {
            return *this;
        }

        char* new_data = new char[other.size_];
        std::memcpy(new_data, other.data_, other.size_);

        delete[] data_;

        data_ = new_data;
        size_ = other.size_;

        return *this;
    }

    Buffer(Buffer&& other) noexcept
        : size_(other.size_), data_(other.data_) {
        std::cout << "move constructor size=" << size_ << std::endl;

        other.size_ = 0;
        other.data_ = nullptr;
    }

    Buffer& operator=(Buffer&& other) noexcept {
        std::cout << "move assignment\n";

        if (this == &other) {
            return *this;
        }

        delete[] data_;

        size_ = other.size_;
        data_ = other.data_;

        other.size_ = 0;
        other.data_ = nullptr;

        return *this;
    }

    size_t size() const {
        return size_;
    }

private:
    size_t size_;
    char* data_;
};
```

测试：

```cpp
int main() {
    std::cout << "=== construct b1 ===\n";
    Buffer b1(1024);

    std::cout << "=== copy construct b2 from b1 ===\n";
    Buffer b2 = b1;

    std::cout << "=== move construct b3 from b1 ===\n";
    Buffer b3 = std::move(b1);

    std::cout << "=== construct b4 ===\n";
    Buffer b4(2048);

    std::cout << "=== copy assign b4 = b2 ===\n";
    b4 = b2;

    std::cout << "=== move assign b4 = std::move(b3) ===\n";
    b4 = std::move(b3);

    std::cout << "=== end ===\n";
    return 0;
}
```

---

# 6. 拷贝构造、移动构造、拷贝赋值、移动赋值的判断

## 6.1 拷贝构造

```cpp
Buffer b2 = b1;
```

调用：

```cpp
Buffer(const Buffer& other);
```

原因：

```text
b2 是新对象，这行代码是在构造 b2。
```

---

## 6.2 移动构造

```cpp
Buffer b3 = std::move(b1);
```

调用：

```cpp
Buffer(Buffer&& other) noexcept;
```

原因：

```text
b3 是新对象，并且使用右值初始化。
```

---

## 6.3 拷贝赋值

```cpp
b4 = b2;
```

调用：

```cpp
Buffer& operator=(const Buffer& other);
```

原因：

```text
b4 已经存在，这里是用 b2 重新赋值 b4。
```

---

## 6.4 移动赋值

```cpp
b4 = std::move(b3);
```

调用：

```cpp
Buffer& operator=(Buffer&& other) noexcept;
```

原因：

```text
b4 已经存在，这里是接管 b3 的资源。
```

---

# 7. 移动构造中为什么要把源对象置空？

例如：

```cpp
Buffer(Buffer&& other) noexcept
    : size_(other.size_), data_(other.data_) {
    other.size_ = 0;
    other.data_ = nullptr;
}
```

原因：

```text
移动后，当前对象已经接管了 other 的资源。
如果不把 other.data_ 置空，两个对象会指向同一块资源。
析构时会发生 double free。
```

更准确的说法：

```text
置空不是为了让源对象失效，而是为了让源对象不再拥有原资源。
源对象仍然有效，可以析构、重新赋值，但不能再假设它拥有原资源。
```

---

# 8. std::move 的本质

`std::move` 本身不移动资源。

它只是：

```text
把对象转换成右值，使其可以匹配移动构造或移动赋值。
```

真正发生资源转移的是：

```text
目标类型的移动构造函数或移动赋值函数。
```

示例：

```cpp
Buffer b3 = std::move(b1);
```

实际过程：

```text
std::move(b1) 把 b1 转成右值
Buffer 的移动构造函数接管 b1 的资源
b1 被置为空状态
```

---

# 9. std::move 后源对象还能不能使用？

可以，但有限制。

移动后的源对象：

```text
仍然是有效对象。
可以析构。
可以重新赋值。
可以做状态判断。
但不应该依赖它仍然拥有原来的值或资源。
```

例如：

```cpp
std::string s = "hello";
std::string t = std::move(s);

s = "world"; // 可以
```

但不应该写：

```cpp
std::cout << s << std::endl; // 不应该假设还是 "hello"
```

---

# 10. std::move 常见误用

## 10.1 move 后继续依赖源对象原值

错误倾向：

```cpp
std::string s = "hello";
std::string t = std::move(s);

std::cout << s << std::endl; // 不应依赖 s 的原值
```

移动后 `s` 仍然有效，但状态不应被假设。

---

## 10.2 对 const 对象使用 std::move

```cpp
const std::string s = "hello";
std::string t = std::move(s);
```

这通常不会真正移动，而是拷贝。

原因：

```text
std::move(s) 得到 const std::string&&。
移动构造通常需要 std::string&&。
移动需要修改源对象，但 const 对象不能被修改。
所以通常只能调用拷贝构造。
```

核心记忆：

```text
const 对象不能被真正移动，因为移动通常要修改源对象。
```

---

## 10.3 return 局部变量时乱用 std::move

推荐：

```cpp
std::string makeName() {
    std::string s = "hello";
    return s;
}
```

不推荐：

```cpp
std::string makeName() {
    std::string s = "hello";
    return std::move(s);
}
```

原因：

```text
直接 return 局部变量时，编译器通常可以做 RVO / NRVO。
手动 std::move 可能破坏 NRVO，反而导致一次移动。
```

一般规则：

```text
返回局部变量时，优先直接 return x，不要手动 return std::move(x)。
```

---

## 10.4 对 int 使用 std::move

```cpp
int a = 10;
int b = std::move(a);
```

基本没有意义。

原因：

```text
int 没有内部资源可以转移。
移动 int 本质上还是拷贝数值。
```

---

# 11. lambda 是什么？

lambda 可以理解为：

```text
可以写在代码现场的匿名函数对象。
```

最简单例子：

```cpp
auto f = []() {
    std::cout << "hello lambda\n";
};

f();
```

---

# 12. lambda 基本结构

```cpp
[capture](parameters) -> return_type {
    body
};
```

示例：

```cpp
auto add = [](int a, int b) -> int {
    return a + b;
};
```

返回类型能推导时可以省略：

```cpp
auto add = [](int a, int b) {
    return a + b;
};
```

---

# 13. lambda 捕获方式

## 13.1 不捕获 `[]`

```cpp
int x = 10;

auto f = []() {
    // std::cout << x; // 编译错误
};
```

`[]` 表示不捕获外部变量。

---

## 13.2 值捕获 `[x]`

```cpp
int x = 10;

auto f = [x]() {
    std::cout << x << std::endl;
};

x = 20;
f(); // 输出 10
```

原因：

```text
[x] 会在 lambda 创建时，把外部 x 拷贝一份到 lambda 对象内部。
外部 x 后续变化，不影响内部副本。
```

---

## 13.3 引用捕获 `[&x]`

```cpp
int x = 10;

auto f = [&x]() {
    std::cout << x << std::endl;
};

x = 20;
f(); // 输出 20
```

原因：

```text
[&x] 捕获的是外部变量的引用。
lambda 内访问的是外部 x 本身。
```

---

## 13.4 mutable

默认情况下，值捕获的变量在 lambda 内部不能修改。

```cpp
int z = 10;

auto h = [z]() mutable {
    z = 30;
    std::cout << "inside z = " << z << std::endl;
};

h();
std::cout << "outside z = " << z << std::endl;
```

输出：

```text
inside z = 30
outside z = 10
```

原因：

```text
mutable 允许修改 lambda 内部按值捕获的副本，不会修改外部变量。
```

---

## 13.5 默认值捕获 `[=]`

```cpp
int x = 10;
int y = 20;

auto f = [=]() {
    std::cout << x + y << std::endl;
};
```

含义：

```text
默认按值捕获 lambda 体内实际使用到的外部变量。
```

---

## 13.6 默认引用捕获 `[&]`

```cpp
int x = 10;
int y = 20;

auto f = [&]() {
    x++;
    y++;
};
```

含义：

```text
默认按引用捕获 lambda 体内实际使用到的外部变量。
```

---

## 13.7 混合捕获

```cpp
int x = 10;
int y = 20;

auto f = [x, &y]() {
    std::cout << x << std::endl;
    y++;
};
```

含义：

```text
x 按值捕获
y 按引用捕获
```

也可以：

```cpp
[=, &y] // 其他默认按值，y 按引用
[&, x]  // 其他默认按引用，x 按值
```

---

# 14. lambda 本质上是什么？

lambda 本质上可以理解为：

```text
编译器生成的匿名函数对象。
捕获的变量会成为这个对象的成员变量。
operator() 是它的函数调用运算符。
```

例如：

```cpp
int x = 10;

auto f = [x]() {
    std::cout << x << std::endl;
};
```

可以粗略理解为：

```cpp
class LambdaObject {
public:
    LambdaObject(int x) : x_(x) {}

    void operator()() const {
        std::cout << x_ << std::endl;
    }

private:
    int x_;
};
```

引用捕获：

```cpp
auto f = [&x]() {
    std::cout << x << std::endl;
};
```

可以粗略理解为：

```cpp
class LambdaObject {
public:
    LambdaObject(int& x) : x_(x) {}

    void operator()() const {
        std::cout << x_ << std::endl;
    }

private:
    int& x_;
};
```

---

# 15. lambda 引用捕获的生命周期风险

危险代码：

```cpp
#include <functional>
#include <iostream>

std::function<void()> makeFunc() {
    int x = 10;

    auto f = [&x]() {
        std::cout << x << std::endl;
    };

    return f;
}

int main() {
    auto func = makeFunc();
    func();

    return 0;
}
```

问题：

```text
x 是 makeFunc 的局部变量。
makeFunc 返回后，x 已经销毁。
lambda 按引用捕获了 x。
之后调用 func() 会访问已经销毁的变量，产生悬空引用。
```

正确写法：

```cpp
std::function<void()> makeFunc() {
    int x = 10;

    auto f = [x]() {
        std::cout << x << std::endl;
    };

    return f;
}
```

---

# 16. 多线程 / 异步场景慎用引用捕获

危险示例：

```cpp
void startThread() {
    int x = 10;

    std::thread t([&x]() {
        std::cout << x << std::endl;
    });

    t.detach();
}
```

问题：

```text
startThread 可能很快返回，局部变量 x 被销毁。
detached 线程稍后执行 lambda，访问悬空引用。
```

更安全：

```cpp
std::thread t([x]() {
    std::cout << x << std::endl;
});
```

结论：

```text
引用捕获不会延长变量生命周期。
如果 lambda 生命周期可能超过被引用变量，就不要用引用捕获。
```

---

# 17. lambda 与 STL 算法

## 17.1 sort

升序：

```cpp
std::sort(v.begin(), v.end(), [](int a, int b) {
    return a < b;
});
```

降序：

```cpp
std::sort(v.begin(), v.end(), [](int a, int b) {
    return a > b;
});
```

---

## 17.2 sort 比较函数不能写 <= 或 >=

错误：

```cpp
std::sort(v.begin(), v.end(), [](int a, int b) {
    return a <= b;
});
```

原因：

当 `a == b` 时：

```cpp
a <= b // true
b <= a // true
```

这等于同时告诉排序算法：

```text
a 应该排在 b 前面
b 也应该排在 a 前面
```

比较关系自相矛盾，破坏排序算法要求。

正确比较函数应满足：

```text
相等元素之间，comp(a, b) 和 comp(b, a) 都应该是 false。
```

所以应写：

```cpp
return a < b;
```

或者：

```cpp
return a > b;
```

---

## 17.3 pair 多条件排序

需求：

```text
分数高的在前。
分数相同，名字字典序小的在前。
```

代码：

```cpp
std::vector<std::pair<std::string, int>> scores = {
    {"alice", 90},
    {"bob", 80},
    {"tom", 95},
    {"jack", 90}
};

std::sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) {
    if (a.second != b.second) {
        return a.second > b.second;
    }
    return a.first < b.first;
});
```

多条件排序标准写法：

```text
先判断主条件是否相等。
主条件不相等，按主条件排序。
主条件相等，再按次条件排序。
```

---

## 17.4 find_if

```cpp
std::vector<int> v = {1, 3, 5, 8, 9};

auto it = std::find_if(v.begin(), v.end(), [](int x) {
    return x % 2 == 0;
});

if (it != v.end()) {
    std::cout << *it << std::endl;
}
```

如果找不到：

```text
find_if 返回 end()。
```

所以不能直接解引用，必须判断：

```cpp
if (it != v.end()) {
}
```

---

## 17.5 for_each 修改元素

```cpp
std::vector<int> a = {1, 2, 3};

std::for_each(a.begin(), a.end(), [](int& x) {
    x *= 2;
});
```

注意：

```cpp
[](int x) {
    x *= 2;
}
```

只是修改元素副本，不影响容器。

必须写：

```cpp
[](int& x) {
    x *= 2;
}
```

这里是 lambda 参数使用引用，不是引用捕获。

---

# 18. unordered_map 基础

## 18.1 unordered_map 是什么？

```cpp
std::unordered_map<Key, Value>
```

通常基于哈希表实现。

特点：

```text
key 无序
平均查找 / 插入 / 删除 O(1)
最坏可能 O(n)
```

---

## 18.2 常用头文件

```cpp
#include <unordered_map>
#include <string>
#include <iostream>
```

---

# 19. unordered_map 常用接口

下面以：

```cpp
std::unordered_map<std::string, int> scores;
```

为例。

---

## 19.1 operator[]

### 用法

```cpp
scores["alice"] = 90;
scores["bob"] = 80;
```

### 行为

如果 key 不存在：

```text
自动插入 key，并默认构造 value。
```

对于 `int`，默认值是 `0`。

```cpp
std::cout << scores["tom"] << std::endl;
```

如果 `"tom"` 不存在，会插入：

```text
"tom" -> 0
```

然后返回 value 的引用。

### 返回值

```cpp
scores["alice"]
```

返回：

```text
对应 value 的引用。
```

类型是：

```cpp
int&
```

### 使用场景

适合：

```cpp
scores["alice"] = 100; // 插入或更新
cnt[c]++;              // 计数
```

不适合：

```text
纯判断 key 是否存在。
```

原因：

```text
operator[] 会产生插入副作用。
```

---

## 19.2 find()

### 用法

```cpp
auto it = scores.find("alice");

if (it != scores.end()) {
    std::cout << it->first << ":" << it->second << std::endl;
}
```

### 返回值

```text
如果找到：返回指向该元素的迭代器。
如果找不到：返回 scores.end()。
```

### 使用场景

适合：

```text
只查找，不希望修改 map。
需要访问 key 和 value。
```

示例：

```cpp
auto it = scores.find("tom");
if (it == scores.end()) {
    std::cout << "not found\n";
}
```

---

## 19.3 count()

### 用法

```cpp
if (scores.count("alice") > 0) {
    std::cout << "exists\n";
}
```

### 返回值

对于 `map / unordered_map`：

```text
key 存在：返回 1
key 不存在：返回 0
```

因为普通 `map / unordered_map` 不允许重复 key。

### 使用场景

适合：

```text
只判断 key 是否存在，不需要访问 value。
```

如果需要访问 value，更推荐 `find()`，避免查两次。

---

## 19.4 at()

### 用法

```cpp
std::cout << scores.at("alice") << std::endl;
```

### 返回值

```text
返回 key 对应 value 的引用。
```

类型是：

```cpp
int&
```

如果是 const map，则返回：

```cpp
const int&
```

### key 不存在时

```text
抛出 std::out_of_range 异常。
```

### 使用场景

适合：

```text
确认 key 一定存在，希望访问 value。
不希望 key 不存在时自动插入。
```

---

## 19.5 insert()

`insert` 是非常重要的接口。

### 用法 1：插入 pair

```cpp
auto ret = scores.insert({"alice", 90});
```

### 返回值

对 `map / unordered_map`：

```cpp
std::pair<iterator, bool>
```

含义：

```text
ret.first  ：指向 map 中该 key 对应元素的迭代器
ret.second ：是否插入成功
```

### 示例

```cpp
std::unordered_map<std::string, int> scores;

auto ret1 = scores.insert({"alice", 90});

if (ret1.second) {
    std::cout << "insert success\n";
}

auto ret2 = scores.insert({"alice", 100});

if (!ret2.second) {
    std::cout << "insert failed, key already exists\n";
    std::cout << "old value = " << ret2.first->second << std::endl;
}
```

第二次插入 `"alice"` 不会覆盖原来的 value。

最终：

```text
alice 仍然是 90，不会变成 100。
```

### 使用场景

适合：

```text
只想在 key 不存在时插入。
不希望覆盖已有 value。
需要知道是否插入成功。
```

---

## 19.6 emplace()

### 用法

```cpp
auto ret = scores.emplace("alice", 90);
```

### 返回值

和 `insert` 类似：

```cpp
std::pair<iterator, bool>
```

含义：

```text
ret.first  ：指向元素的迭代器
ret.second ：是否插入成功
```

### 使用场景

适合：

```text
直接在容器内部构造元素。
避免某些临时对象构造。
```

示例：

```cpp
std::unordered_map<std::string, int> scores;

auto ret = scores.emplace("alice", 90);

if (ret.second) {
    std::cout << "insert success\n";
}
```

注意：

如果 key 已存在，`emplace` 不会覆盖旧值。

---

## 19.7 insert_or_assign() C++17

### 用法

```cpp
scores.insert_or_assign("alice", 100);
```

### 行为

```text
key 不存在：插入。
key 已存在：更新 value。
```

### 返回值

```cpp
std::pair<iterator, bool>
```

含义：

```text
ret.first  ：指向元素的迭代器
ret.second ：true 表示插入，false 表示赋值更新
```

### 使用场景

适合：

```text
明确想要“插入或覆盖”。
```

与 `operator[]` 的区别：

```text
insert_or_assign 不要求 value 先默认构造。
语义更明确。
```

---

## 19.8 try_emplace() C++17

### 用法

```cpp
scores.try_emplace("alice", 90);
```

### 行为

```text
key 不存在：原地构造 value。
key 已存在：什么都不做。
```

### 返回值

```cpp
std::pair<iterator, bool>
```

### 使用场景

适合：

```text
value 构造成本较高，只想在 key 不存在时构造。
```

对于简单 `int` 意义不大，但对于复杂对象很有用。

---

# 20. unordered_map erase 接口

`erase` 有多个重载，返回值不同，非常容易考。

---

## 20.1 erase(key)

### 用法

```cpp
size_t n = scores.erase("alice");
```

### 返回值

```text
返回删除的元素个数。
```

对于 `unordered_map / map`：

```text
key 存在：返回 1
key 不存在：返回 0
```

### 示例

```cpp
std::unordered_map<std::string, int> scores;
scores["alice"] = 90;

size_t n1 = scores.erase("alice"); // 1
size_t n2 = scores.erase("tom");   // 0
```

### 使用场景

适合：

```text
按 key 删除。
需要知道是否真的删除成功。
```

---

## 20.2 erase(iterator)

### 用法

```cpp
auto it = scores.find("alice");

if (it != scores.end()) {
    it = scores.erase(it);
}
```

### 返回值

从 C++11 起：

```text
返回被删除元素后面的下一个合法迭代器。
```

### 使用场景

适合：

```text
遍历过程中删除当前元素。
```

示例：

```cpp
for (auto it = scores.begin(); it != scores.end(); ) {
    if (it->second < 60) {
        it = scores.erase(it);
    } else {
        ++it;
    }
}
```

注意：

```text
erase(it) 后，原来的 it 失效。
必须使用返回的新迭代器继续遍历。
```

---

## 20.3 erase(first, last)

### 用法

```cpp
scores.erase(first, last);
```

### 返回值

C++11 起一般返回：

```text
指向 last 后续位置的迭代器，也就是删除区间后的下一个合法位置。
```

但实际日常使用中，范围删除更多用于顺序容器或已确定范围的关联容器。

对于 `map`：

```cpp
auto first = m.lower_bound(10);
auto last = m.lower_bound(20);
m.erase(first, last);
```

表示删除 key 在 `[10, 20)` 范围内的元素。

### 使用场景

适合：

```text
删除一段迭代器区间。
```

---

# 21. unordered_map 其他常用接口

## 21.1 size()

```cpp
scores.size();
```

返回：

```text
当前元素个数。
```

---

## 21.2 empty()

```cpp
scores.empty();
```

返回：

```text
容器是否为空。
```

---

## 21.3 clear()

```cpp
scores.clear();
```

作用：

```text
删除所有元素。
```

返回值：

```text
void
```

---

## 21.4 begin() / end()

```cpp
for (auto it = scores.begin(); it != scores.end(); ++it) {
    std::cout << it->first << ":" << it->second << std::endl;
}
```

注意：

```text
unordered_map 遍历顺序不稳定。
```

---

# 22. unordered_map 接口综合示例

```cpp
#include <iostream>
#include <string>
#include <unordered_map>

int main() {
    std::unordered_map<std::string, int> scores;

    scores["alice"] = 90;
    scores["bob"] = 80;

    auto ret = scores.insert({"tom", 70});
    if (ret.second) {
        std::cout << "insert tom success\n";
    }

    auto ret2 = scores.insert({"tom", 100});
    if (!ret2.second) {
        std::cout << "insert tom failed, old value = "
                  << ret2.first->second << std::endl;
    }

    auto it = scores.find("alice");
    if (it != scores.end()) {
        std::cout << "alice score = " << it->second << std::endl;
    }

    size_t n = scores.erase("bob");
    std::cout << "erase bob count = " << n << std::endl;

    for (auto it = scores.begin(); it != scores.end(); ) {
        if (it->second < 80) {
            it = scores.erase(it);
        } else {
            ++it;
        }
    }

    return 0;
}
```

---

# 23. map 基础

## 23.1 map 是什么？

```cpp
std::map<Key, Value>
```

通常基于红黑树等平衡二叉搜索树实现。

特点：

```text
key 有序
查找 / 插入 / 删除 O(log n)
遍历时按 key 升序
支持 lower_bound / upper_bound
```

---

## 23.2 常用头文件

```cpp
#include <map>
#include <string>
#include <iostream>
```

---

# 24. map 常用接口

`map` 的大部分接口和 `unordered_map` 很像。

下面以：

```cpp
std::map<int, std::string> m;
```

为例。

---

## 24.1 operator[]

### 用法

```cpp
m[10] = "alice";
m[20] = "bob";
```

### 行为

```text
key 不存在：插入 key，并默认构造 value。
key 存在：返回已有 value 的引用。
```

### 返回值

```text
value 的引用。
```

例如：

```cpp
std::string& name = m[10];
```

### 使用场景

适合：

```text
插入或更新。
计数。
```

不适合：

```text
纯判断 key 是否存在。
```

---

## 24.2 find()

### 用法

```cpp
auto it = m.find(10);

if (it != m.end()) {
    std::cout << it->first << ":" << it->second << std::endl;
}
```

### 返回值

```text
找到：返回指向元素的迭代器。
找不到：返回 m.end()。
```

### 使用场景

```text
只查询，不插入。
```

---

## 24.3 count()

```cpp
if (m.count(10) > 0) {
    std::cout << "exists\n";
}
```

返回值：

```text
普通 map 中 key 存在返回 1，不存在返回 0。
```

---

## 24.4 insert()

### 用法

```cpp
auto ret = m.insert({10, "alice"});
```

### 返回值

```cpp
std::pair<iterator, bool>
```

含义：

```text
ret.first  ：指向该 key 对应元素的迭代器
ret.second ：是否插入成功
```

### 示例

```cpp
std::map<int, std::string> m;

auto r1 = m.insert({10, "alice"});
auto r2 = m.insert({10, "tom"});

std::cout << r1.second << std::endl; // true
std::cout << r2.second << std::endl; // false

std::cout << r2.first->second << std::endl; // alice
```

第二次插入不会覆盖旧值。

---

## 24.5 emplace()

```cpp
auto ret = m.emplace(10, "alice");
```

返回值：

```cpp
std::pair<iterator, bool>
```

行为：

```text
key 不存在时原地构造。
key 已存在时插入失败，不覆盖旧值。
```

---

## 24.6 erase(key)

### 用法

```cpp
size_t n = m.erase(10);
```

### 返回值

```text
删除的元素个数。
普通 map 中 key 存在返回 1，不存在返回 0。
```

---

## 24.7 erase(iterator)

### 用法

```cpp
auto it = m.find(10);

if (it != m.end()) {
    it = m.erase(it);
}
```

### 返回值

从 C++11 起：

```text
返回被删除元素后面的下一个合法迭代器。
```

### 使用场景

适合遍历删除：

```cpp
for (auto it = m.begin(); it != m.end(); ) {
    if (it->first < 10) {
        it = m.erase(it);
    } else {
        ++it;
    }
}
```

---

## 24.8 erase(first, last)

### 用法

```cpp
auto first = m.lower_bound(10);
auto last = m.lower_bound(20);

m.erase(first, last);
```

删除范围：

```text
[first, last)
```

在这个例子里，删除：

```text
key >= 10 且 key < 20 的元素。
```

### 返回值

C++11 起通常返回：

```text
删除区间后的下一个合法迭代器。
```

但日常很多时候不接返回值。

---

## 24.9 lower_bound()

### 用法

```cpp
auto it = m.lower_bound(25);
```

### 返回值

```text
返回第一个 key >= 25 的元素迭代器。
如果不存在，返回 m.end()。
```

示例：

```cpp
std::map<int, std::string> m;

m[10] = "a";
m[20] = "b";
m[30] = "c";

auto it = m.lower_bound(25);

if (it != m.end()) {
    std::cout << it->first << ":" << it->second << std::endl;
}
```

输出：

```text
30:c
```

### 使用场景

适合：

```text
有序查找。
范围查询。
查找不小于某个 key 的第一个元素。
```

---

## 24.10 upper_bound()

### 用法

```cpp
auto it = m.upper_bound(20);
```

### 返回值

```text
返回第一个 key > 20 的元素迭代器。
如果不存在，返回 m.end()。
```

示例：

```cpp
std::map<int, std::string> m;

m[10] = "a";
m[20] = "b";
m[30] = "c";

auto it = m.upper_bound(20);
```

返回 key 为：

```text
30
```

---

## 24.11 equal_range()

### 用法

```cpp
auto range = m.equal_range(20);
```

返回值：

```cpp
std::pair<iterator, iterator>
```

含义：

```text
range.first  = lower_bound(key)
range.second = upper_bound(key)
```

对普通 `map` 来说，因为 key 唯一，所以范围最多包含一个元素。

---

# 25. map 接口综合示例

```cpp
#include <iostream>
#include <map>
#include <string>

int main() {
    std::map<int, std::string> m;

    m[20] = "bob";
    m[10] = "alice";
    m[30] = "tom";

    auto ret = m.insert({20, "jack"});
    if (!ret.second) {
        std::cout << "insert failed, old value = "
                  << ret.first->second << std::endl;
    }

    auto it = m.lower_bound(25);
    if (it != m.end()) {
        std::cout << "lower_bound(25): "
                  << it->first << ":" << it->second << std::endl;
    }

    auto first = m.lower_bound(10);
    auto last = m.lower_bound(30);

    m.erase(first, last); // 删除 key 在 [10, 30) 的元素

    for (const auto& kv : m) {
        std::cout << kv.first << ":" << kv.second << std::endl;
    }

    return 0;
}
```

---

# 26. map 与 unordered_map 的接口差异

## 26.1 二者共同接口

`map` 和 `unordered_map` 都支持：

```text
operator[]
at
find
count
insert
emplace
erase
clear
size
empty
begin / end
```

---

## 26.2 map 独有或更有意义的接口

由于 `map` 有序，所以支持：

```text
lower_bound
upper_bound
equal_range
按 key 有序遍历
范围删除
```

---

## 26.3 unordered_map 特有的哈希桶接口

`unordered_map` 还有一些哈希相关接口，例如：

```text
bucket_count
load_factor
max_load_factor
rehash
reserve
```

这些短期面试不需要深挖，但要知道：

```text
unordered_map 底层是哈希表。
当元素变多时，可能 rehash。
rehash 会导致迭代器失效。
```

---

# 27. map 与 unordered_map 的区别

| 容器              | 常见底层结构      | key 是否有序 | 查找复杂度           | 适用场景                  |
| --------------- | ----------- | -------- | --------------- | --------------------- |
| `map`           | 红黑树 / 平衡搜索树 | 有序       | O(log n)        | 有序遍历、范围查询、lower_bound |
| `unordered_map` | 哈希表         | 无序       | 平均 O(1)，最坏 O(n) | 快速查找、计数、缓存、映射表        |

---

# 28. 什么时候用 unordered_map？

适合：

```text
只需要快速查找。
不关心 key 顺序。
key 可以 hash。
算法题计数 / 查表。
连接表 / 缓存表 / ID 映射。
```

例如：

```cpp
std::unordered_map<int, Connection*> fdToConn;
std::unordered_map<std::string, int> wordCount;
```

---

# 29. 什么时候用 map？

适合：

```text
需要按 key 有序遍历。
需要 lower_bound / upper_bound。
需要范围查询。
希望性能稳定为 O(log n)。
```

例如：

```cpp
std::map<int, std::string> timeline;

auto it = timeline.lower_bound(100);
```

---

# 30. map / unordered_map 常用接口速查表

## 30.1 查询类

| 接口           | 返回值                   | 是否修改容器      | 使用场景         |
| ------------ | --------------------- | ----------- | ------------ |
| `find(key)`  | 找到返回迭代器，找不到返回 `end()` | 否           | 查找并访问 value  |
| `count(key)` | 存在返回 1，不存在返回 0        | 否           | 只判断是否存在      |
| `at(key)`    | 返回 value 引用，不存在抛异常    | 否           | 确认 key 存在时访问 |
| `operator[]` | 返回 value 引用           | key 不存在时会插入 | 插入、更新、计数     |

---

## 30.2 插入类

| 接口                        | 返回值                    | key 已存在时行为 | 使用场景         |
| ------------------------- | ---------------------- | ---------- | ------------ |
| `insert({k, v})`          | `pair<iterator, bool>` | 插入失败，不覆盖   | 只想插入，不想覆盖    |
| `emplace(k, v)`           | `pair<iterator, bool>` | 插入失败，不覆盖   | 原地构造         |
| `insert_or_assign(k, v)`  | `pair<iterator, bool>` | 更新 value   | 插入或覆盖        |
| `try_emplace(k, args...)` | `pair<iterator, bool>` | 什么都不做      | value 构造成本高时 |

---

## 30.3 删除类

| 接口                   | 返回值                 | 使用场景      |
| -------------------- | ------------------- | --------- |
| `erase(key)`         | 删除个数，存在返回 1，不存在返回 0 | 按 key 删除  |
| `erase(iterator)`    | 下一个合法迭代器            | 遍历时删除当前元素 |
| `erase(first, last)` | 删除区间后的下一个合法迭代器      | 删除一段范围    |

---

# 31. erase 遍历删除标准写法

## 31.1 map

```cpp
for (auto it = m.begin(); it != m.end(); ) {
    if (it->first < 10) {
        it = m.erase(it);
    } else {
        ++it;
    }
}
```

---

## 31.2 unordered_map

```cpp
for (auto it = um.begin(); it != um.end(); ) {
    if (it->second == 0) {
        it = um.erase(it);
    } else {
        ++it;
    }
}
```

核心：

```text
erase(it) 后原来的 it 失效。
必须使用 erase 返回的新迭代器继续遍历。
```

---

# 32. insert vs operator[] vs insert_or_assign

## 32.1 operator[]

```cpp
scores["alice"] = 90;
```

特点：

```text
key 不存在：插入默认 value，再赋值。
key 存在：修改 value。
```

适合：

```text
简单插入 / 更新 / 计数。
```

---

## 32.2 insert

```cpp
scores.insert({"alice", 90});
```

特点：

```text
key 不存在：插入。
key 存在：不覆盖。
```

适合：

```text
只想插入，不想修改已有值。
```

---

## 32.3 insert_or_assign

```cpp
scores.insert_or_assign("alice", 100);
```

特点：

```text
key 不存在：插入。
key 存在：覆盖。
```

适合：

```text
明确要插入或更新。
```

---

# 33. 今日重要面试问答

## Q1：拷贝和移动的区别是什么？

答：

拷贝是复制资源内容，两个对象拥有各自独立的资源；移动是转移资源所有权，新对象接管源对象的资源，源对象变成有效但不再拥有原资源的状态。移动通常避免重新分配和复制资源，因此对管理堆内存、fd、socket、vector buffer 等资源的对象更高效。

---

## Q2：栈对象可以移动吗？

答：

可以。移动的不是对象本体所在的栈内存，而是对象内部管理的资源所有权。比如栈上的 `Buffer` 对象内部持有堆内存指针，移动构造时新对象接管这块堆内存，源对象指针被置空。源对象本身仍然存在，直到离开作用域才析构。

---

## Q3：`std::move` 后源对象还能使用吗？

答：

源对象仍然有效，可以析构、重新赋值或做状态判断，但不应该依赖它仍然拥有原来的值或资源。比如 `unique_ptr` 移动后通常变为空，`string` 移动后内容处于有效但未指定状态。

---

## Q4：为什么对 const 对象使用 std::move 通常不会移动？

答：

因为移动通常需要修改源对象，例如转移内部指针并将源对象置空。但 const 对象不能被修改。`std::move(const T)` 得到的是 `const T&&`，而移动构造通常需要 `T&&`，所以通常无法调用移动构造，只能调用拷贝构造。

---

## Q5：为什么 return 局部变量时不建议 `return std::move(x)`？

答：

直接 `return x` 时，编译器通常可以进行 RVO / NRVO，直接在返回值位置构造对象，避免拷贝和移动。手动 `return std::move(x)` 可能破坏 NRVO，导致反而执行一次移动构造。因此返回局部变量时一般直接 `return x`。

---

## Q6：lambda 捕获 `[x]` 和 `[&x]` 的区别？

答：

`[x]` 是值捕获，在 lambda 创建时拷贝一份外部变量，之后外部变量变化不会影响 lambda 内部副本。`[&x]` 是引用捕获，lambda 内部访问的是外部变量本身，外部变量变化会反映到 lambda 中，但如果外部变量生命周期结束，lambda 会产生悬空引用风险。

---

## Q7：lambda 本质是什么？

答：

lambda 可以理解为编译器生成的匿名函数对象。捕获的变量会成为该对象的成员变量，lambda 体对应其 `operator()` 函数。

---

## Q8：lambda 引用捕获有什么风险？

答：

引用捕获不会延长外部变量生命周期。如果 lambda 的生命周期超过被引用变量的生命周期，就会产生悬空引用。例如在函数中按引用捕获局部变量并返回 lambda，函数结束后局部变量销毁，再调用 lambda 就是未定义行为。异步和多线程场景尤其要慎用引用捕获。

---

## Q9：sort 比较函数为什么不能写 `<=`？

答：

`sort` 的比较函数表示“a 是否应该排在 b 前面”。如果写 `<=`，当 `a == b` 时，`comp(a,b)` 和 `comp(b,a)` 都为 true，相当于同时认为 a 应该在 b 前、b 也应该在 a 前，比较关系自相矛盾，破坏排序算法要求。正确写法应使用 `<` 或 `>`，让相等元素比较时返回 false。

---

## Q10：find_if 找不到返回什么？

答：

返回区间的 `end()` 迭代器。因此使用前必须判断 `it != end()`，不能直接解引用。

---

## Q11：为什么 `operator[]` 不适合判断 unordered_map 中 key 是否存在？

答：

因为 `operator[]` 在 key 不存在时会自动插入一个默认构造的 value，并返回其引用。纯查询场景不应该修改 map 状态，所以应该使用 `find` 或 `count` 判断是否存在。

---

## Q12：map 和 unordered_map 有什么区别？

答：

`map` 通常基于红黑树等平衡搜索树实现，key 有序，查找、插入、删除复杂度是 O(log n)，支持按 key 顺序遍历和 `lower_bound / upper_bound`。`unordered_map` 基于哈希表，key 无序，平均查找、插入、删除复杂度是 O(1)，但最坏可能因哈希冲突退化到 O(n)。如果需要有序遍历或范围查询，用 `map`；如果只需要快速查找且不关心顺序，用 `unordered_map`。

---

## Q13：insert 和 operator[] 有什么区别？

答：

`operator[]` 在 key 不存在时会插入默认 value，并返回 value 引用；key 存在时可以修改 value。`insert` 只在 key 不存在时插入，如果 key 已存在，插入失败，不会覆盖原值，并通过返回值中的 bool 告诉是否插入成功。

---

## Q14：map/unordered_map 的 erase 返回值是什么？

答：

`erase(key)` 返回删除元素个数，普通 `map/unordered_map` 中 key 存在返回 1，不存在返回 0。`erase(iterator)` 返回被删除元素后面的下一个合法迭代器，适合遍历时删除。`erase(first, last)` 删除一个迭代器区间，返回删除区间后的下一个合法迭代器。

---

# 34. 今日易错点总结

```text
1. std::move 不移动资源，只做右值转换。
2. 移动的是资源所有权，不是栈对象本体。
3. 移动后的对象仍然有效，但不要依赖原值。
4. const 对象通常不能真正移动。
5. return 局部变量时通常不要 return std::move(x)。
6. lambda 值捕获是副本，引用捕获是外部变量本身。
7. mutable 修改的是 lambda 内部副本，不是外部变量。
8. 引用捕获不会延长变量生命周期。
9. 异步 / 多线程场景慎用引用捕获。
10. sort 比较函数不要写 <= 或 >=。
11. for_each 修改元素时，lambda 参数要写 T&。
12. unordered_map 的 operator[] 会自动插入 key。
13. unordered_map 遍历顺序不稳定。
14. unordered_map 平均 O(1)，最坏 O(n)。
15. map 有序，支持 lower_bound / upper_bound。
16. insert 不覆盖已有 key，operator[] 可以插入或更新。
17. erase(key) 返回删除数量。
18. erase(iterator) 返回下一个合法迭代器。
19. 遍历删除时必须使用 it = erase(it)。
```

---

# 35. 今日核心代码片段

## 35.1 move 构造

```cpp
Buffer(Buffer&& other) noexcept
    : size_(other.size_), data_(other.data_) {
    other.size_ = 0;
    other.data_ = nullptr;
}
```

---

## 35.2 move 赋值

```cpp
Buffer& operator=(Buffer&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    delete[] data_;

    size_ = other.size_;
    data_ = other.data_;

    other.size_ = 0;
    other.data_ = nullptr;

    return *this;
}
```

---

## 35.3 lambda 值捕获

```cpp
int x = 10;

auto f = [x]() {
    std::cout << x << std::endl;
};

x = 20;
f(); // 10
```

---

## 35.4 lambda 引用捕获

```cpp
int x = 10;

auto f = [&x]() {
    std::cout << x << std::endl;
};

x = 20;
f(); // 20
```

---

## 35.5 sort 多条件排序

```cpp
std::sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) {
    if (a.second != b.second) {
        return a.second > b.second;
    }
    return a.first < b.first;
});
```

---

## 35.6 find_if

```cpp
auto it = std::find_if(v.begin(), v.end(), [](int x) {
    return x % 2 == 0;
});

if (it != v.end()) {
    std::cout << *it << std::endl;
}
```

---

## 35.7 for_each 修改元素

```cpp
std::for_each(v.begin(), v.end(), [](int& x) {
    x *= 2;
});
```

---

## 35.8 unordered_map 计数

```cpp
std::unordered_map<char, int> cnt;

for (char c : s) {
    cnt[c]++;
}
```

---

## 35.9 unordered_map 查找

```cpp
auto it = scores.find("tom");

if (it == scores.end()) {
    std::cout << "not found\n";
}
```

---

## 35.10 unordered_map insert

```cpp
std::unordered_map<std::string, int> scores;

auto ret = scores.insert({"alice", 90});

if (ret.second) {
    std::cout << "insert success\n";
} else {
    std::cout << "already exists, value = "
              << ret.first->second << std::endl;
}
```

---

## 35.11 map lower_bound

```cpp
std::map<int, std::string> m;

m[10] = "a";
m[20] = "b";
m[30] = "c";

auto it = m.lower_bound(25);

if (it != m.end()) {
    std::cout << it->first << std::endl; // 30
}
```

---

## 35.12 map 遍历删除

```cpp
for (auto it = m.begin(); it != m.end(); ) {
    if (it->first < 10) {
        it = m.erase(it);
    } else {
        ++it;
    }
}
```

---

# 36. 明日开始前复习问题

```text
1. 拷贝和移动的本质区别是什么？
2. 栈对象移动时，移动的到底是什么？
3. std::move 本身是否移动资源？
4. const 对象为什么通常不能真正移动？
5. return 局部变量时为什么不建议 std::move？
6. lambda 的值捕获和引用捕获有什么区别？
7. lambda 为什么可能产生悬空引用？
8. sort 比较函数为什么不能写 <=？
9. find_if 找不到返回什么？
10. for_each 修改元素时为什么参数要写引用？
11. unordered_map 的 operator[] 有什么副作用？
12. map 和 unordered_map 分别适合什么场景？
13. insert 的返回值是什么？
14. insert 和 operator[] 的区别是什么？
15. erase(key) 的返回值是什么？
16. erase(iterator) 的返回值是什么？
17. 遍历 map/unordered_map 时删除元素的标准写法是什么？
18. lower_bound 和 upper_bound 的区别是什么？
```

---

# 37. Day 3 总结

Day 3 的核心收获：

```text
move 语义：理解资源所有权转移。
lambda：理解捕获和生命周期。
STL 算法：用 lambda 表达局部逻辑。
unordered_map/map：理解查找结构、常用接口和返回值。
```

最终形成的工程判断：

```text
管理资源的大对象 → 移动比拷贝更有价值。
移动后源对象 → 仍然有效，但不要依赖原值。
lambda 生命周期可能变长 → 谨慎引用捕获。
排序比较函数 → 必须满足严格弱序，不写 <= / >=。
只查 unordered_map 是否存在 → 用 find/count，不用 []。
需要快速查找 → unordered_map。
需要有序遍历 / 范围查询 → map。
只想插入不覆盖 → insert / emplace。
想插入或更新 → operator[] / insert_or_assign。
遍历删除 → it = erase(it)。
```
