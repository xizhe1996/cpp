# Day 1｜C++ 对象生命周期、RAII、Rule of Three/Five/Zero

# 1. 学习目标

核心目标不是大量刷题，而是先掌握 C++ 资源管理的基础能力：

- 理解构造函数、析构函数、拷贝构造、拷贝赋值的区别
- 理解浅拷贝、深拷贝、double free 的来源
- 掌握 RAII 的基本思想
- 理解 Rule of Three / Rule of Five / Rule of Zero
- 初步理解 move-only 资源管理类的设计方式
- 使用 AddressSanitizer 检查内存错误
- 理解 `explicit operator bool() const` 的作用

核心结论：

> C++ 中只要一个类直接管理资源，就必须认真考虑对象生命周期、拷贝行为、移动行为和析构行为。

---

# 2. 构造函数与析构函数

## 2.1 构造函数

构造函数在对象创建时自动调用，用于初始化对象。

```cpp
class Person {
public:
    Person() {
        std::cout << "constructor\n";
    }
};
````

使用：

```cpp
Person p;
```

对象 `p` 创建时，构造函数自动执行。

---

## 2.2 析构函数

析构函数在对象生命周期结束时自动调用，用于释放资源。

```cpp
class Person {
public:
    ~Person() {
        std::cout << "destructor\n";
    }
};
```

局部对象离开作用域时，析构函数自动执行。

```cpp
void func() {
    Person p;
} // p 在这里析构
```

---

# 3. RAII 基本思想

RAII 全称：

```text
Resource Acquisition Is Initialization
```

可以理解为：

> 将资源的申请和释放绑定到对象生命周期上。

常见资源包括：

* 堆内存
* 文件描述符 fd
* socket
* mutex
* thread
* 文件句柄
* 数据库连接

典型写法：

```cpp
class Buffer {
public:
    Buffer() {
        data_ = new char[1024];
    }

    ~Buffer() {
        delete[] data_;
    }

private:
    char* data_;
};
```

构造函数申请资源，析构函数释放资源。

---

# 4. String 类练习

## 4.1 基础版本

```cpp
#include <cstring>
#include <iostream>

class String {
public:
    String() {
        data = new char[1];
        data[0] = '\0';
    }

    String(const char* s) {
        data = new char[std::strlen(s) + 1];
        std::strcpy(data, s);
    }

    ~String() {
        delete[] data;
    }

    String(const String& other) {
        data = new char[std::strlen(other.c_str()) + 1];
        std::strcpy(data, other.c_str());
    }

    String& operator=(const String& other) {
        if (this == &other) {
            return *this;
        }

        char* new_data = new char[std::strlen(other.c_str()) + 1];
        std::strcpy(new_data, other.c_str());

        delete[] data;
        data = new_data;

        return *this;
    }

    const char* c_str() const {
        return data;
    }

private:
    char* data;
};
```

---

# 5. 默认构造为什么要申请 1 个字符？

推荐写法：

```cpp
String() {
    data = new char[1];
    data[0] = '\0';
}
```

原因：

如果 `data = nullptr`，那么后续调用：

```cpp
strlen(data);
std::cout << data;
```

可能出现未定义行为。

使用空字符串 `"\0"` 可以让对象始终处于可用状态。

---

# 6. `strlen` 和 `sizeof` 的区别

在构造函数中：

```cpp
String(const char* s) {
    data = new char[strlen(s) + 1];
}
```

不能用：

```cpp
sizeof(s)
```

原因是 `s` 是一个指针。

```cpp
const char* s = "hello";

sizeof(s); // 通常是 8，表示指针大小
strlen(s); // 是 5，表示字符串长度，不包含 '\0'
```

所以申请字符串空间时应该使用：

```cpp
strlen(s) + 1
```

`+1` 是为了存放字符串结尾的 `'\0'`。

---

# 7. 拷贝构造与拷贝赋值

## 7.1 拷贝构造

```cpp
String s1("hello");
String s2 = s1;
```

调用：

```cpp
String(const String& other)
```

原因：

`s2` 是新对象，这行代码是在创建并初始化 `s2`。

注意：

```cpp
String s2 = s1;
```

虽然有 `=`，但它不是拷贝赋值，而是拷贝初始化，对应拷贝构造。

---

## 7.2 拷贝赋值

```cpp
String s1("hello");
String s3;

s3 = s1;
```

调用：

```cpp
String& operator=(const String& other)
```

原因：

`s3` 已经存在，这里是在修改一个已有对象。

---

## 7.3 核心区别

```text
新对象初始化 → 拷贝构造
已有对象重新赋值 → 拷贝赋值
```

示例：

```cpp
String s2 = s1; // 拷贝构造
s3 = s1;        // 拷贝赋值
```

---

# 8. 为什么拷贝构造中不能 `delete[] data`

错误写法：

```cpp
String(const String& other) {
    delete[] data;
    data = new char[strlen(other.c_str()) + 1];
    strcpy(data, other.c_str());
}
```

原因：

拷贝构造函数执行时，当前对象正在被创建，`data` 还没有被初始化成有效指针。

此时执行：

```cpp
delete[] data;
```

就是释放未初始化的野指针，属于未定义行为。

正确写法：

```cpp
String(const String& other) {
    data = new char[strlen(other.c_str()) + 1];
    strcpy(data, other.c_str());
}
```

---

# 9. 深拷贝与浅拷贝

## 9.1 浅拷贝

浅拷贝只是复制指针值。

```cpp
String(const String& other) {
    data = other.data;
}
```

结果：

```text
s1.data 和 s2.data 指向同一块堆内存
```

析构时：

```text
s2 析构 → delete[] data
s1 析构 → 再次 delete[] data
```

会造成 double free。

---

## 9.2 深拷贝

深拷贝会重新申请内存，并复制内容。

```cpp
String(const String& other) {
    data = new char[strlen(other.data) + 1];
    strcpy(data, other.data);
}
```

结果：

```text
s1.data 和 s2.data 指向不同内存
两块内存内容相同
```

这样析构时不会重复释放同一块内存。

---

# 10. 拷贝赋值中的自赋值问题

错误风险：

```cpp
s3 = s3;
```

如果没有判断自赋值，代码可能变成：

```cpp
delete[] data;
data = new char[strlen(other.c_str()) + 1];
```

但此时 `other` 就是当前对象自己，`other.c_str()` 指向的内存已经被释放，会造成 use-after-free。

正确写法：

```cpp
String& operator=(const String& other) {
    if (this == &other) {
        return *this;
    }

    char* new_data = new char[strlen(other.c_str()) + 1];
    strcpy(new_data, other.c_str());

    delete[] data;
    data = new_data;

    return *this;
}
```

---

# 11. 为什么拷贝赋值中要先 new，再 delete？

更安全的顺序：

```cpp
char* new_data = new char[strlen(other.c_str()) + 1];
strcpy(new_data, other.c_str());

delete[] data;
data = new_data;
```

原因：

如果先执行：

```cpp
delete[] data;
```

再执行：

```cpp
data = new char[...];
```

当 `new` 失败时，当前对象内部的旧资源已经被破坏，`data` 可能变成悬空指针。

更稳妥的原则：

```text
先准备新资源
再释放旧资源
最后切换指针
```

这样即使申请新资源失败，原对象也仍然保持原状态。

---

# 12. AddressSanitizer 使用

## 12.1 编译命令

```bash
g++ -std=c++11 -g -fsanitize=address -fno-omit-frame-pointer main.cpp -o string_test
```

运行：

```bash
./string_test
```

参数说明：

```text
-std=c++11：使用 C++11 标准
-g：保留调试信息
-fsanitize=address：开启 AddressSanitizer
-fno-omit-frame-pointer：让错误堆栈更清楚
```

---

## 12.2 浅拷贝导致 double free

错误拷贝构造：

```cpp
String(const String& other) {
    data = other.data;
}
```

ASan 报错示例：

```text
ERROR: AddressSanitizer: attempting double-free
```

含义：

```text
程序正在尝试释放一块已经被释放过的堆内存。
```

如果字符串是 `"hello"`，ASan 可能显示：

```text
6-byte region
```

因为：

```text
h e l l o \0
```

一共 6 个字节。

---

# 13. Rule of Three

Rule of Three 指的是：

如果一个类需要自定义下面三者之一：

```cpp
~T();                  // 析构函数
T(const T&);            // 拷贝构造函数
T& operator=(const T&); // 拷贝赋值函数
```

通常也需要考虑另外两个。

原因：

一旦需要自己写析构函数，通常说明类直接管理某种资源。

如果不自定义拷贝构造和拷贝赋值，编译器默认生成的版本通常只是浅拷贝，可能导致：

* double free
* 资源泄漏
* 悬空指针
* 重复关闭资源

---

# 14. Rule of Five

C++11 引入移动语义后，Rule of Three 扩展为 Rule of Five。

五个函数分别是：

```cpp
~T();                  // 析构函数

T(const T&);            // 拷贝构造函数
T& operator=(const T&); // 拷贝赋值函数

T(T&&);                 // 移动构造函数
T& operator=(T&&);      // 移动赋值函数
```

Rule of Five 的核心：

> 如果一个类直接管理资源，除了拷贝和析构，还要考虑资源所有权能否移动。

---

# 15. String 的移动构造与移动赋值

## 15.1 移动构造

```cpp
String(String&& other) noexcept {
    data = other.data;
    other.data = nullptr;
}
```

含义：

```text
当前对象接管 other 的资源。
other 被置空，避免析构时重复释放。
```

---

## 15.2 移动赋值

```cpp
String& operator=(String&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    delete[] data;

    data = other.data;
    other.data = nullptr;

    return *this;
}
```

含义：

```text
当前对象先释放自己的旧资源。
然后接管 other 的资源。
最后让 other 不再拥有资源。
```

---

## 15.3 moved-from 对象

移动后的对象应该处于：

```text
有效但状态未指定 / 不再拥有原资源
```

对 `String` 来说，可以将其 `data` 设置为：

```cpp
nullptr
```

然后 `c_str()` 需要考虑空指针：

```cpp
const char* c_str() const {
    return data ? data : "";
}
```

---

# 16. Rule of Zero

Rule of Zero 是现代 C++ 更推荐的设计原则。

核心思想：

> 业务类不要直接管理裸资源，而是使用已经实现 RAII 的标准库类型，让编译器自动生成析构、拷贝、赋值和移动函数。

例如不推荐：

```cpp
class User {
private:
    char* name_;
};
```

推荐：

```cpp
#include <string>

class User {
private:
    std::string name_;
};
```

再比如：

```cpp
#include <memory>
#include <string>

class TcpConnection {
private:
    std::unique_ptr<Buffer> inputBuffer_;
    std::unique_ptr<Buffer> outputBuffer_;
    std::string peerAddress_;
};
```

这里业务类本身不需要手写析构函数，因为：

* `unique_ptr` 会释放 `Buffer`
* `string` 会释放字符串内存

---

# 17. Rule of Zero 和 RAII 的关系

Rule of Zero 不是不用 RAII。

它的意思是：

```text
业务类本身不要直接管理裸资源；
把资源交给专门的 RAII 类型管理。
```

例如：

```cpp
class User {
private:
    std::string name_;
};
```

`User` 遵守 Rule of Zero，但 `std::string` 内部仍然使用 RAII 管理内存。

---

# 18. fd 为什么也是资源？

文件描述符 fd 虽然表现为一个 `int`：

```cpp
int fd = open("a.txt", O_RDONLY);
```

但它代表内核中的一个打开文件资源。

它也有完整生命周期：

```text
open / socket 创建资源
read / write / send / recv 使用资源
close 释放资源
```

所以 fd 适合用 RAII 管理。

---

# 19. FdGuard：fd 的 RAII 封装

## 19.1 为什么 fd 不能拷贝？

如果允许拷贝：

```cpp
FdGuard a(fd);
FdGuard b = a;
```

那么 `a` 和 `b` 都持有同一个 fd。

析构时：

```text
b 析构 → close(fd)
a 析构 → 再 close(fd)
```

这叫 double close。

本质类似于 String 中的 double free。

---

## 19.2 为什么 fd 可以移动？

fd 是独占资源句柄。

它不应该被多个对象共同拥有，但可以转移所有权。

```cpp
FdGuard a(open("a.txt", O_RDONLY));
FdGuard b(std::move(a));
```

语义：

```text
b 接管 fd
a 不再拥有 fd
最终只有 b 负责 close
```

---

# 20. FdGuard 推荐写法

```cpp
#include <unistd.h>

class FdGuard {
public:
    explicit FdGuard(int fd = -1) : fd_(fd) {}

    ~FdGuard() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;

    FdGuard(FdGuard&& other) noexcept
        : fd_(other.fd_) {
        other.fd_ = -1;
    }

    FdGuard& operator=(FdGuard&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        if (fd_ >= 0) {
            close(fd_);
        }

        fd_ = other.fd_;
        other.fd_ = -1;

        return *this;
    }

    int get() const {
        return fd_;
    }

    bool valid() const {
        return fd_ >= 0;
    }

    explicit operator bool() const {
        return valid();
    }

private:
    int fd_;
};
```

---

# 21. FdGuard 为什么禁用拷贝？

```cpp
FdGuard(const FdGuard&) = delete;
FdGuard& operator=(const FdGuard&) = delete;
```

原因：

fd 是独占资源。

如果允许拷贝，多个对象会认为自己拥有同一个 fd，析构时可能重复 `close`。

---

# 22. FdGuard 为什么支持移动？

移动构造：

```cpp
FdGuard(FdGuard&& other) noexcept
    : fd_(other.fd_) {
    other.fd_ = -1;
}
```

作用：

```text
当前对象接管 other 的 fd。
other.fd_ 设置为 -1，表示不再拥有有效 fd。
```

移动赋值：

```cpp
FdGuard& operator=(FdGuard&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    if (fd_ >= 0) {
        close(fd_);
    }

    fd_ = other.fd_;
    other.fd_ = -1;

    return *this;
}
```

作用：

```text
先释放当前对象已有 fd，避免资源泄漏。
再接管 other 的 fd。
最后将 other.fd_ 设置为 -1。
```

---

# 23. 为什么 moved-from 的 fd 要设置为 -1？

fd 的有效值通常是：

```text
0, 1, 2, 3, ...
```

其中：

```text
0 = stdin
1 = stdout
2 = stderr
```

所以通常用：

```cpp
-1
```

表示无效 fd。

移动后：

```cpp
other.fd_ = -1;
```

是为了防止 `other` 析构时再次 `close` 原 fd。

---

# 24. 为什么移动构造 / 移动赋值要加 noexcept？

```cpp
FdGuard(FdGuard&& other) noexcept;
FdGuard& operator=(FdGuard&& other) noexcept;
```

原因：

很多标准库容器在扩容或移动元素时，会优先使用 `noexcept` 的移动构造。

对于资源句柄类来说，移动通常只是转移一个整数或指针，不应该抛异常。

所以资源句柄类的移动操作通常应该标记为：

```cpp
noexcept
```

---

# 25. `explicit operator bool() const`

代码：

```cpp
explicit operator bool() const {
    return fd_ >= 0;
}
```

作用：

允许对象在条件判断中被当作 bool 使用。

例如：

```cpp
FdGuard fd(open("a.txt", O_RDONLY));

if (fd) {
    // fd 有效
} else {
    // fd 无效
}
```

等价于：

```cpp
if (fd.get() >= 0) {
}
```

或者：

```cpp
if (fd.valid()) {
}
```

---

## 25.1 为什么写法奇怪？

它是类型转换运算符，不是普通函数。

普通函数：

```cpp
bool valid() const {
    return fd_ >= 0;
}
```

调用：

```cpp
if (fd.valid()) {
}
```

类型转换运算符：

```cpp
explicit operator bool() const {
    return fd_ >= 0;
}
```

调用：

```cpp
if (fd) {
}
```

---

## 25.2 为什么要加 explicit？

不加 `explicit` 可能导致对象在一些非预期场景下隐式转换为 bool。

加上 `explicit` 后，主要支持条件判断场景：

```cpp
if (fd) {
}

while (fd) {
}
```

更安全。

---

# 26. 今日重要面试问答

## Q1：什么是 RAII？

答：

RAII 是 C++ 中管理资源的一种方式。它将资源的申请绑定到对象构造函数，将资源的释放绑定到析构函数。这样对象生命周期结束时，资源会自动释放，可以减少内存泄漏、fd 泄漏以及异常路径下忘记释放资源的问题。

---

## Q2：为什么 String 类需要自己写拷贝构造函数？

答：

因为 String 内部持有通过 `new[]` 申请的堆内存。如果使用编译器默认生成的拷贝构造函数，只会复制 `char*` 指针值，导致两个对象指向同一块堆内存。析构时两个对象都会执行 `delete[]`，从而造成 double free。所以需要自定义拷贝构造函数，重新申请内存并复制字符串内容，实现深拷贝。

---

## Q3：拷贝构造和拷贝赋值有什么区别？

答：

拷贝构造用于初始化一个新对象，例如：

```cpp
String s2 = s1;
```

此时 `s2` 原来不存在，是新对象。

拷贝赋值用于修改一个已经存在的对象，例如：

```cpp
String s3;
s3 = s1;
```

此时 `s3` 已经构造完成，所以调用的是拷贝赋值函数。

---

## Q4：为什么拷贝赋值要判断自赋值？

答：

如果出现：

```cpp
s = s;
```

并且拷贝赋值函数中先释放旧资源，再从右侧对象拷贝数据，那么右侧对象其实就是自己，旧资源释放后再访问就会出现 use-after-free。因此需要判断：

```cpp
if (this == &other) {
    return *this;
}
```

---

## Q5：为什么拷贝赋值中最好先申请新资源，再释放旧资源？

答：

这是为了保证异常安全。如果先释放旧资源，再申请新资源，一旦 `new` 失败，当前对象内部资源已经被破坏，可能进入无效状态。先申请新资源，成功后再释放旧资源，可以保证申请失败时原对象仍保持原状态。

---

## Q6：什么是 Rule of Three？

答：

Rule of Three 指的是，如果一个类需要自定义析构函数、拷贝构造函数或拷贝赋值函数中的任意一个，通常也需要考虑另外两个。因为这类通常直接管理资源，如果依赖默认拷贝，可能导致浅拷贝、double free、资源泄漏等问题。

---

## Q7：什么是 Rule of Five？

答：

Rule of Five 是 C++11 后对 Rule of Three 的扩展。除了析构函数、拷贝构造和拷贝赋值，还需要考虑移动构造和移动赋值。对于直接管理资源的类，移动语义可以通过转移资源所有权避免不必要的深拷贝。

---

## Q8：什么是 Rule of Zero？

答：

Rule of Zero 是现代 C++ 推荐的设计原则。业务类尽量不要直接管理裸资源，而是使用 `std::string`、`std::vector`、`std::unique_ptr`、`std::shared_ptr` 等已经实现 RAII 的类型。这样业务类通常不需要手写析构、拷贝、赋值或移动函数，减少资源管理错误。

---

## Q9：为什么 fd 适合用 RAII 管理？

答：

fd 虽然表现为一个 int，但它代表内核中的一个打开文件资源。它通过 `open` 或 `socket` 创建，通过 `read/write/send/recv` 使用，通过 `close` 释放。如果中间出现多个 return 或异常路径，容易忘记 close。用 RAII 封装后，可以在析构函数中自动 close，避免 fd 泄漏。

---

## Q10：为什么 FdGuard 要禁用拷贝？

答：

fd 是独占资源。如果允许拷贝，两个 FdGuard 对象可能持有同一个 fd，析构时都会调用 close，导致 double close。因此应该通过 `= delete` 禁用拷贝构造和拷贝赋值。

---

## Q11：为什么 FdGuard 可以支持移动？

答：

fd 不能被多个对象共同拥有，但可以转移所有权。移动构造或移动赋值可以让一个 FdGuard 接管另一个 FdGuard 的 fd，并将源对象的 fd 设置为 -1。这样最终只有一个对象负责 close，不会发生 double close。

---

## Q12：`explicit operator bool() const` 有什么作用？

答：

它允许资源管理对象在条件判断中被当作 bool 使用。例如：

```cpp
if (fd) {
    // fd 有效
}
```

它本质是一个安全的 bool 类型转换函数，通常用于表示资源对象当前是否有效。加 `explicit` 可以避免过多隐式转换。

---

# 27. 今日容易踩坑总结

## 27.1 拷贝构造里不能 delete

错误：

```cpp
String(const String& other) {
    delete[] data;
}
```

原因：

新对象还没有旧资源，`data` 未初始化。

---

## 27.2 `sizeof(char*)` 不是字符串长度

错误：

```cpp
new char[sizeof(s)];
```

正确：

```cpp
new char[strlen(s) + 1];
```

---

## 27.3 拷贝赋值必须考虑自赋值

正确：

```cpp
if (this == &other) {
    return *this;
}
```

---

## 27.4 `new[]` 必须对应 `delete[]`

```cpp
data = new char[10];
delete[] data;
```

不能写成：

```cpp
delete data;
```

---

## 27.5 默认拷贝是浅拷贝

如果类里有裸指针资源，默认拷贝通常危险。

---

## 27.6 fd 不是普通 int

fd 虽然类型是 int，但代表内核资源，必须 close。

---

# 28. 今日核心代码清单

## 28.1 String：Rule of Three 版本

```cpp
#include <cstring>
#include <iostream>

class String {
public:
    String() {
        data = new char[1];
        data[0] = '\0';
    }

    String(const char* s) {
        data = new char[std::strlen(s) + 1];
        std::strcpy(data, s);
    }

    ~String() {
        delete[] data;
    }

    String(const String& other) {
        data = new char[std::strlen(other.c_str()) + 1];
        std::strcpy(data, other.c_str());
    }

    String& operator=(const String& other) {
        if (this == &other) {
            return *this;
        }

        char* new_data = new char[std::strlen(other.c_str()) + 1];
        std::strcpy(new_data, other.c_str());

        delete[] data;
        data = new_data;

        return *this;
    }

    const char* c_str() const {
        return data;
    }

private:
    char* data;
};
```

---

## 28.2 FdGuard：move-only RAII wrapper

```cpp
#include <unistd.h>

class FdGuard {
public:
    explicit FdGuard(int fd = -1) : fd_(fd) {}

    ~FdGuard() {
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;

    FdGuard(FdGuard&& other) noexcept
        : fd_(other.fd_) {
        other.fd_ = -1;
    }

    FdGuard& operator=(FdGuard&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        if (fd_ >= 0) {
            close(fd_);
        }

        fd_ = other.fd_;
        other.fd_ = -1;

        return *this;
    }

    int get() const {
        return fd_;
    }

    bool valid() const {
        return fd_ >= 0;
    }

    explicit operator bool() const {
        return valid();
    }

private:
    int fd_;
};
```

---

# 29. 今日复习重点

明天开始前建议先复习下面这些问题：

```text
1. 拷贝构造和拷贝赋值的区别是什么？
2. 为什么 String 类需要深拷贝？
3. 为什么拷贝构造函数里不能 delete[] data？
4. 为什么拷贝赋值函数要处理自赋值？
5. 为什么拷贝赋值中更推荐先 new 再 delete？
6. Rule of Three / Five / Zero 分别是什么？
7. fd 为什么不能拷贝，但可以移动？
8. moved-from 对象为什么要置空或置为 -1？
9. explicit operator bool() const 是什么？
10. ASan 如何检查 double free？
```

---

# 30. Day 1 总结

Day 1 的核心收获：

> C++ 资源管理的本质是所有权管理。

对于 `String`：

```text
资源 = 堆内存
释放动作 = delete[]
风险 = shallow copy → double free
```

对于 `FdGuard`：

```text
资源 = fd
释放动作 = close
风险 = copy → double close
```

因此：

```text
可复制资源 → 考虑深拷贝
独占资源 → 禁用拷贝，支持移动
业务类 → 尽量使用 Rule of Zero
```
