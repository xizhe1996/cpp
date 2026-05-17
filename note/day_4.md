# Day 4｜const、引用、指针与函数接口设计

# 1. 今日学习目标

Day 4 的核心目标：

- 掌握 `const` 和指针组合的含义
- 理解函数参数传值、传引用、传 const 引用、传指针的语义
- 理解 const 成员函数、const 对象、const 成员变量、mutable
- 掌握引用和指针的区别
- 理解初始化列表和构造函数体的区别
- 能根据需求设计合适的函数参数类型
- 建立“接口表达语义”的意识

核心主线：

> C++ 函数签名不是随便写的，它应该表达是否修改对象、对象是否可为空、是否接管所有权、是否共享生命周期。

---

# 2. const 基础

## 2.1 普通 const 变量

```cpp
const int x = 10;
````

含义：

```text
x 初始化后不能再被修改。
```

错误：

```cpp
x = 20; // 编译错误
```

---

# 3. const 与指针

## 3.1 判断规则

简单规则：

```text
const 在 * 左边：修饰指向的内容。
const 在 * 右边：修饰指针本身。
```

---

## 3.2 `const int* p`

```cpp
int a = 10;
int b = 20;

const int* p = &a;
```

等价于：

```cpp
int const* p = &a;
```

含义：

```text
p 是指针。
不能通过 p 修改它指向的 int。
但 p 自己可以改指向。
```

示例：

```cpp
// *p = 30; // 错误，不能通过 p 修改指向内容
p = &b;     // 正确，p 可以改指向
```

注意：

```cpp
a = 30; // 正确
```

因为 `a` 本身不是 const，只是不能通过 `p` 修改。

---

## 3.3 `int* const p`

```cpp
int a = 10;
int b = 20;

int* const p = &a;
```

含义：

```text
p 本身是 const。
p 不能改指向。
但可以通过 p 修改它指向的 int。
```

示例：

```cpp
*p = 30;  // 正确，可以修改 a
// p = &b; // 错误，p 不能改指向
```

---

## 3.4 `const int* const p`

```cpp
int a = 10;
int b = 20;

const int* const p = &a;
```

含义：

```text
p 本身不能改指向。
也不能通过 p 修改它指向的 int。
```

错误：

```cpp
// *p = 20; // 错误
// p = &b;  // 错误
```

---

## 3.5 一句话记忆

```cpp
const int* p;        // 内容不能改，指针能改
int* const p;        // 指针不能改，内容能改
const int* const p;  // 内容和指针都不能改
```

---

# 4. 函数参数传递方式

## 4.1 传值：`T x`

```cpp
void f(std::string s);
```

语义：

```text
函数拿到的是一份副本。
函数内部修改 s，不影响外部对象。
```

特点：

```text
会发生拷贝或移动。
```

适合：

```text
1. 小对象，比如 int、double、bool、char
2. 函数确实需要保留一份副本
3. 后续要 move 进成员变量或容器的场景
```

示例：

```cpp
void setAge(int age) {
    age_ = age;
}
```

---

## 4.2 普通引用：`T& x`

```cpp
void modify(std::string& s) {
    s += "_modified";
}
```

语义：

```text
函数操作的是外部对象本身。
函数可能修改外部对象。
```

特点：

```text
不拷贝。
不能接收 const 对象。
不能接收临时对象。
参数语义上一定存在。
```

示例：

```cpp
std::string name = "alice";
modify(name);

std::cout << name << std::endl; // alice_modified
```

---

## 4.3 const 引用：`const T& x`

```cpp
void print(const std::string& s);
```

语义：

```text
只读借用。
不拷贝。
函数不能修改外部对象。
```

特点：

```text
可以接收普通对象。
可以接收 const 对象。
可以接收临时对象。
适合大对象只读参数。
```

示例：

```cpp
void print(const std::string& s) {
    std::cout << s << std::endl;
}

std::string a = "alice";
const std::string b = "bob";

print(a);            // 可以
print(b);            // 可以
print("temporary");  // 可以
```

---

## 4.4 指针参数：`T* p`

```cpp
void handle(User* user);
```

语义：

```text
参数可能为空。
函数可能修改对象。
函数不拥有对象。
```

函数内部通常要判断：

```cpp
void handle(User* user) {
    if (user) {
        user->hello();
    }
}
```

---

## 4.5 const 指针参数：`const T* p`

```cpp
void dumpUser(const User* user);
```

语义：

```text
参数可能为空。
如果不为空，函数只读取对象，不修改。
函数不拥有对象。
```

示例：

```cpp
void dumpUser(const User* user) {
    if (!user) {
        return;
    }

    std::cout << user->name() << std::endl;
}
```

---

## 4.6 参数传递方式对比

| 方式           | 是否拷贝 | 是否能修改外部对象 | 是否可为空 | 典型语义        |
| ------------ | ---- | --------- | ----- | ----------- |
| `T x`        | 是    | 否，改的是副本   | 否     | 拿一份副本       |
| `T& x`       | 否    | 是         | 否     | 修改外部对象      |
| `const T& x` | 否    | 否         | 否     | 只读借用        |
| `T* x`       | 否    | 可以        | 是     | 可选对象 / 可能为空 |
| `const T* x` | 否    | 否         | 是     | 可选只读对象      |

---

# 5. const 成员函数

## 5.1 成员函数后面的 const

```cpp
class User {
public:
    int age() const {
        return age_;
    }

private:
    int age_ = 0;
};
```

这里的：

```cpp
int age() const
```

表示：

```text
这个成员函数承诺不修改当前对象的可观察状态。
```

更底层一点：

```text
在 const 成员函数里，this 被视为 const User* const this。
```

因此不能修改普通成员变量：

```cpp
age_ = 10; // 错误
```

也不能调用非 const 成员函数，因为非 const 成员函数可能修改对象。

---

## 5.2 普通成员函数中的 this

普通成员函数中，`this` 大致可以理解为：

```cpp
User* const this
```

含义：

```text
this 指针本身不能改指向。
但可以通过 this 修改对象成员。
```

---

## 5.3 const 成员函数中的 this

const 成员函数中，`this` 大致可以理解为：

```cpp
const User* const this
```

含义：

```text
this 指针本身不能改指向。
也不能通过 this 修改普通成员变量。
```

---

## 5.4 const 对象只能调用 const 成员函数

```cpp
class User {
public:
    void setAge(int age) {
        age_ = age;
    }

    int age() const {
        return age_;
    }

private:
    int age_ = 0;
};

int main() {
    const User u;

    u.age();       // 可以
    // u.setAge(10); // 错误
}
```

原因：

```text
const 对象不能被修改。
非 const 成员函数不承诺不修改对象。
所以 const 对象不能调用非 const 成员函数。
```

---

## 5.5 非 const 对象可以调用 const 成员函数

```cpp
User u;

u.age();       // 可以
u.setAge(10);  // 也可以
```

非 const 对象既可以调用 const 成员函数，也可以调用非 const 成员函数。

---

# 6. const 重载

C++ 支持根据成员函数是否 const 进行重载。

```cpp
class Buffer {
public:
    char& at(size_t i) {
        return data_[i];
    }

    const char& at(size_t i) const {
        return data_[i];
    }

private:
    std::vector<char> data_;
};
```

使用：

```cpp
Buffer b;
b.at(0) = 'a'; // 调用非 const 版本，可以修改

const Buffer cb;
char c = cb.at(0); // 调用 const 版本，只能读取
```

作用：

```text
const 对象和非 const 对象可以调用不同版本的成员函数。
非 const 版本可以返回可修改引用。
const 版本返回只读引用。
```

STL 中常见的 `operator[]`、`begin()`、`end()` 都经常有 const / 非 const 版本。

---

# 7. mutable

## 7.1 mutable 的作用

`mutable` 允许某个成员变量在 const 成员函数中被修改。

```cpp
class Counter {
public:
    int get() const {
        ++access_count_;
        return value_;
    }

private:
    int value_ = 0;
    mutable int access_count_ = 0;
};
```

使用场景：

```text
缓存
访问计数
调试统计
mutex
```

---

## 7.2 mutable mutex 示例

```cpp
#include <mutex>

class SafeData {
public:
    int get() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return value_;
    }

private:
    int value_ = 0;
    mutable std::mutex mutex_;
};
```

`get() const` 逻辑上只是读取数据，但加锁会修改 mutex 内部状态，所以 `mutex_` 通常声明为 `mutable`。

---

# 8. const 成员变量

## 8.1 const 成员变量是什么？

```cpp
class User {
public:
    User(int id) : id_(id) {}

private:
    const int id_;
};
```

`const int id_` 表示：

```text
每个 User 对象内部都有一个 id_。
id_ 初始化之后不能再被修改。
```

---

## 8.2 const 成员变量必须在初始化列表中初始化

正确：

```cpp
class User {
public:
    User(int id) : id_(id) {}

private:
    const int id_;
};
```

错误：

```cpp
class User {
public:
    User(int id) {
        id_ = id; // 错误
    }

private:
    const int id_;
};
```

原因：

```text
构造函数体执行时，成员变量已经完成初始化。
const 成员变量不能先默认初始化，再赋值。
它必须在初始化列表阶段直接初始化。
```

---

## 8.3 const 成员变量初始化后不能修改

```cpp
class User {
public:
    User(int id) : id_(id) {}

    void setId(int id) {
        id_ = id; // 错误
    }

private:
    const int id_;
};
```

即使在普通非 const 成员函数中，也不能修改 const 成员变量。

---

## 8.4 const 成员变量和 const 成员函数区别

| 概念         | 限制对象                        |
| ---------- | --------------------------- |
| const 成员变量 | 某个成员变量初始化后不能修改              |
| const 成员函数 | 该成员函数承诺不修改当前对象              |
| const 对象   | 整个对象被视为不可修改，只能调用 const 成员函数 |
| mutable 成员 | 即使在 const 成员函数中也允许修改        |

---

## 8.5 const 成员变量会影响赋值运算符

如果类里有 const 成员变量：

```cpp
class User {
public:
    User(int id) : id_(id) {}

private:
    const int id_;
};
```

那么默认拷贝赋值可能会被删除：

```cpp
User u1(1);
User u2(2);

u2 = u1; // 可能编译错误
```

原因：

```text
赋值的语义是修改一个已经存在的对象。
但 const 成员 id_ 在对象构造后不能再被修改。
所以默认 operator= 无法给 const 成员重新赋值。
```

---

## 8.6 const 成员变量的工程建议

`const` 成员变量适合表示对象创建后不应该改变的属性，例如：

```cpp
class User {
public:
    User(int id) : id_(id) {}

    int id() const {
        return id_;
    }

private:
    const int id_;
};
```

但要小心：

```text
如果这个类需要支持赋值操作，const 成员变量会带来麻烦。
```

很多工程代码中会避免在普通业务类中大量使用 const 成员变量，而是通过接口控制不让外部修改：

```cpp
class User {
public:
    int id() const {
        return id_;
    }

private:
    int id_;
};
```

只提供 getter，不提供 setter，也能达到“外部不能修改”的效果，同时不影响赋值语义。

---

# 9. 初始化列表与构造函数体

## 9.1 初始化列表是初始化

```cpp
class User {
public:
    User(int id, const std::string& name)
        : id_(id), name_(name) {
    }

private:
    const int id_;
    std::string name_;
};
```

这里：

```cpp
: id_(id), name_(name)
```

是在初始化成员变量。

---

## 9.2 构造函数体内是赋值或额外逻辑

```cpp
class User {
public:
    User(int id, const std::string& name) {
        name_ = name;
    }

private:
    std::string name_;
};
```

过程是：

```text
1. name_ 先默认构造
2. 构造函数体中再赋值
```

因此通常不如初始化列表直接初始化高效。

---

## 9.3 初始化列表和构造函数体的核心区别

```text
初始化列表：成员变量初始化阶段。
构造函数体：成员变量已经初始化完成后的执行阶段。
```

可以简单理解为：

```text
初始化列表 = 出生时初始化
构造函数体 = 出生后再赋值或执行逻辑
```

---

## 9.4 必须使用初始化列表的场景

以下成员必须在初始化列表中初始化：

```text
const 成员变量
引用成员变量
没有默认构造函数的成员对象
```

---

### const 成员

```cpp
class User {
public:
    User(int id) : id_(id) {}

private:
    const int id_;
};
```

---

### 引用成员

```cpp
class Holder {
public:
    Holder(int& x) : ref_(x) {}

private:
    int& ref_;
};
```

错误写法：

```cpp
class Holder {
public:
    Holder(int& x) {
        ref_ = x; // 错误：引用成员必须初始化时绑定
    }

private:
    int& ref_;
};
```

原因：

```text
引用必须在定义时初始化。
引用成员也一样，必须在对象构造时就绑定。
```

---

### 没有默认构造函数的成员

```cpp
class Engine {
public:
    Engine(int power) {}
};

class Car {
public:
    Car() : engine_(100) {}

private:
    Engine engine_;
};
```

如果写成：

```cpp
class Car {
public:
    Car() {
        engine_ = Engine(100); // 错误：engine_ 进入函数体前必须先构造
    }

private:
    Engine engine_;
};
```

不行，因为进入构造函数体之前，`engine_` 必须先被构造。但 `Engine` 没有默认构造函数，所以必须在初始化列表里直接构造。

---

## 9.5 初始化列表通常更高效

```cpp
class User {
public:
    User(const std::string& name)
        : name_(name) {}

private:
    std::string name_;
};
```

这是直接构造 `name_`。

而：

```cpp
class User {
public:
    User(const std::string& name) {
        name_ = name;
    }

private:
    std::string name_;
};
```

过程是：

```text
1. name_ 先默认构造
2. 构造函数体里再拷贝赋值
```

所以通常建议：

```text
成员变量尽量使用初始化列表初始化。
```

---

# 10. 引用和指针区别

## 10.1 引用必须初始化

```cpp
int a = 10;
int& r = a; // 正确
```

错误：

```cpp
int& r; // 错误
```

指针可以为空：

```cpp
int* p = nullptr;
```

---

## 10.2 引用不能重新绑定

```cpp
int a = 10;
int b = 20;

int& r = a;
r = b;
```

这里：

```cpp
r = b;
```

不是让 `r` 改为引用 `b`。

而是：

```text
把 b 的值赋给 a。
r 仍然是 a 的别名。
```

执行后：

```text
a = 20
b = 20
r 仍然绑定 a
```

指针可以改变指向：

```cpp
int* p = &a;
p = &b; // 可以
```

---

## 10.3 引用语义上不能为空

```cpp
int& r = a;
```

引用语义上表示一定绑定了有效对象。

指针可以为空：

```cpp
int* p = nullptr;
```

所以：

```text
参数一定存在 → 用引用。
参数可能为空 → 用指针。
```

---

## 10.4 访问方式不同

引用像普通对象一样使用：

```cpp
int a = 10;
int& r = a;

r = 20;
```

指针需要解引用：

```cpp
int* p = &a;

*p = 30;
```

---

## 10.5 sizeof 不同

```cpp
int a = 10;
int& r = a;
int* p = &a;

sizeof(r); // 等于 sizeof(int)
sizeof(p); // 64 位平台通常是 8
```

原因：

```text
sizeof(r) 得到的是被引用对象的大小。
sizeof(p) 得到的是指针变量本身的大小。
```

---

## 10.6 面试回答

```text
引用是对象的别名，定义时必须初始化，初始化后不能重新绑定，语义上一般不为空，使用时像普通对象一样访问。指针是保存地址的变量，可以为空，可以改变指向，使用时需要解引用。函数参数中，如果对象一定存在，通常用引用；如果参数可能为空，通常用指针。
```

---

# 11. Part 6：C++ 函数接口设计总规则

今天所有内容，其实都可以收束到一个问题：

> 函数参数类型应该如何表达语义？

C++ 里函数签名不是随便写的，它应该告诉调用者：

```text
这个函数会不会修改对象？
这个参数能不能为空？
这个函数会不会接管所有权？
这个函数会不会共享生命周期？
这个函数只是临时借用，还是要保存下来？
```

---

## 11.1 只读借用：`const T&`

### 场景

对象一定存在，函数只读取，不修改。

```cpp
void printUser(const User& user);
void printVector(const std::vector<int>& v);
void sendMessage(const std::string& msg);
```

语义：

```text
我只是临时读取这个对象。
我不会修改它。
我不接管它的生命周期。
```

适合：

```text
大对象只读参数。
对象一定不为空。
不希望拷贝。
```

---

## 11.2 可修改借用：`T&`

### 场景

对象一定存在，函数需要修改它。

```cpp
void sortVector(std::vector<int>& v);
void resetConnection(Connection& conn);
void updateUser(User& user);
```

语义：

```text
我临时使用这个对象。
我可能修改它。
但我不接管它的生命周期。
```

---

## 11.3 可选只读对象：`const T*`

### 场景

参数可能为空，只读取，不修改。

```cpp
void dumpUser(const User* user);
void printConnection(const Connection* conn);
```

语义：

```text
这个参数可能为空。
如果不为空，我只读取它。
我不接管生命周期。
```

示例：

```cpp
void dumpUser(const User* user) {
    if (!user) {
        return;
    }

    std::cout << user->name() << std::endl;
}
```

---

## 11.4 可选可修改对象：`T*`

### 场景

参数可能为空，如果不为空，函数可能修改它。

```cpp
void handleConnection(Connection* conn);
void resetUser(User* user);
```

语义：

```text
这个参数可能为空。
如果不为空，我可能修改它。
我不拥有它。
```

---

## 11.5 小对象：传值 `T`

### 场景

对象很小，拷贝成本低。

```cpp
void setFlag(bool enabled);
void setAge(int age);
void moveTo(double x, double y);
```

语义：

```text
我只需要这个值本身。
拷贝成本很低。
```

---

## 11.6 需要保存一份副本：可以传值再 move

### 场景

函数最终要把参数保存到成员变量里。

传统写法：

```cpp
class User {
public:
    void setName(const std::string& name) {
        name_ = name;
    }

private:
    std::string name_;
};
```

现代常见写法：

```cpp
class User {
public:
    void setName(std::string name) {
        name_ = std::move(name);
    }

private:
    std::string name_;
};
```

语义：

```text
调用者传左值时，拷贝进参数，再 move 到成员。
调用者传右值时，可以移动进参数，再 move 到成员。
```

这个写法适合：

```text
函数需要保留一份参数。
参数类型支持高效移动。
```

初期可以优先使用：

```cpp
void setName(const std::string& name);
```

等熟练后再使用“传值 + move”的写法。

---

## 11.7 接管独占所有权：`std::unique_ptr<T>`

### 场景

函数要拿走对象所有权。

```cpp
void takeTask(std::unique_ptr<Task> task);
void addConnection(std::unique_ptr<Connection> conn);
```

调用：

```cpp
auto task = std::make_unique<Task>();
takeTask(std::move(task));
```

调用后：

```text
外部 task 变为空。
函数参数 task 接管对象。
对象生命周期由函数内部决定。
```

语义非常明确：

```text
我接管这个对象。
调用者不再拥有它。
```

---

## 11.8 修改调用者的 unique_ptr：`std::unique_ptr<T>&`

### 场景

函数要改变调用者手里的智能指针本身。

```cpp
void resetTask(std::unique_ptr<Task>& task) {
    task = std::make_unique<Task>();
}
```

语义：

```text
我不是只使用 Task。
我要修改你手里的 unique_ptr。
```

注意：

如果函数只是临时使用对象，不应该写：

```cpp
void useTask(std::unique_ptr<Task>& task);
```

而应该写：

```cpp
void useTask(Task& task);
```

因为临时使用对象不需要知道它是不是由 `unique_ptr` 管理。

---

## 11.9 共享所有权：`std::shared_ptr<T>`

### 场景

函数需要保存对象，延长生命周期。

```cpp
void saveConnection(std::shared_ptr<Connection> conn) {
    connections_.push_back(conn);
}
```

语义：

```text
我也要参与这个对象的生命周期管理。
我会保存一份 shared_ptr。
```

如果函数只是临时访问对象，不应该传：

```cpp
void printUser(std::shared_ptr<User> user);
```

更应该传：

```cpp
void printUser(const User& user);
```

---

## 11.10 观察生命周期但不拥有：`std::weak_ptr<T>`

### 场景

函数或对象需要观察某个由 `shared_ptr` 管理的对象，但不想延长其生命周期。

```cpp
class Session {
public:
    void setOwner(std::weak_ptr<User> owner) {
        owner_ = owner;
    }

    void notifyOwner() {
        if (auto owner = owner_.lock()) {
            owner->notify();
        }
    }

private:
    std::weak_ptr<User> owner_;
};
```

语义：

```text
我知道这个对象可能存在。
我不拥有它。
使用前我要检查它是否还活着。
```

---

## 11.11 智能指针不是普通参数的替代品

非常重要：

```text
智能指针不是为了替代所有指针/引用参数。
智能指针应该只在需要表达所有权关系时出现在接口里。
```

错误倾向：

```cpp
void useTask(std::unique_ptr<Task>& task);
void printUser(std::shared_ptr<User> user);
```

如果只是临时使用对象，应该写：

```cpp
void useTask(Task& task);
void printUser(const User& user);
```

---

## 11.12 函数接口设计总表

| 场景                | 推荐参数                  |
| ----------------- | --------------------- |
| 小对象，只需要值          | `T`                   |
| 大对象，只读，一定存在       | `const T&`            |
| 大对象，要修改，一定存在      | `T&`                  |
| 可能为空，只读           | `const T*`            |
| 可能为空，要修改          | `T*`                  |
| 接管独占所有权           | `std::unique_ptr<T>`  |
| 修改调用者的 unique_ptr | `std::unique_ptr<T>&` |
| 共享并保存生命周期         | `std::shared_ptr<T>`  |
| 观察但不拥有            | `std::weak_ptr<T>`    |

---

# 12. 综合接口设计示例

## 12.1 打印 vector，不修改，一定存在

```cpp
void printVector(const std::vector<int>& v);
```

原因：

```text
一定存在 → 引用
只读 → const
避免拷贝 → &
```

---

## 12.2 排序 vector，需要修改，一定存在

```cpp
void sortVector(std::vector<int>& v);
```

原因：

```text
需要修改 → 非 const 引用
一定存在 → 引用
```

---

## 12.3 User 可能为空，只读取

```cpp
void dumpUser(const User* user);
```

原因：

```text
可能为空 → 指针
只读 → const
```

---

## 12.4 重置 Connection，一定存在，需要修改

```cpp
void resetConnection(Connection& conn);
```

原因：

```text
一定存在 → 引用
需要修改 → 非 const
```

---

## 12.5 接管独占 Task 所有权

```cpp
void takeTask(std::unique_ptr<Task> task);
```

原因：

```text
接管独占所有权 → unique_ptr 按值传递
```

调用：

```cpp
auto task = std::make_unique<Task>();
takeTask(std::move(task));
```

---

## 12.6 临时使用 Task，不接管所有权，一定存在

```cpp
void useTask(Task& task);
```

如果只读：

```cpp
void useTask(const Task& task);
```

原因：

```text
只是临时使用对象 → 引用
不接管所有权 → 不传智能指针
```

---

# 13. 今日重要面试问答

## Q1：`const int* p`、`int* const p`、`const int* const p` 区别？

答：

`const int* p` 表示指向 const int 的指针，不能通过 p 修改指向内容，但 p 可以改指向。`int* const p` 表示 const 指针，p 不能改指向，但可以通过 p 修改内容。`const int* const p` 表示指针本身和指向内容都不能通过 p 修改。

---

## Q2：函数参数什么时候用 `const T&`？

答：

当参数是较大对象、函数只读取不修改、并且对象一定存在时，使用 `const T&`。它避免拷贝，表达只读语义，也可以接收普通对象、const 对象和临时对象。

---

## Q3：函数参数什么时候用指针？

答：

当参数可能为空时，用指针表达 nullable 语义。如果只读，用 `const T*`；如果可能修改，用 `T*`。函数内部通常需要判断是否为 nullptr。

---

## Q4：成员函数后面的 const 表示什么？

答：

成员函数后面的 const 表示该函数承诺不修改当前对象的可观察状态。在 const 成员函数中，this 被视为 `const T* const this`，因此不能修改普通成员变量，也不能调用非 const 成员函数。

---

## Q5：为什么 const 对象只能调用 const 成员函数？

答：

const 对象不能被修改，而非 const 成员函数不承诺不修改对象，所以 const 对象不能调用非 const 成员函数。const 成员函数承诺不修改对象，因此 const 对象可以调用。

---

## Q6：mutable 有什么用？

答：

mutable 允许某个成员即使在 const 成员函数中也能被修改。常见场景包括缓存、访问计数、调试统计和 mutex。比如 `get() const` 逻辑上只读，但需要加锁，mutex 内部状态会变化，所以 mutex 常声明为 mutable。

---

## Q7：const 成员变量为什么必须在初始化列表中初始化？

答：

构造函数体执行时，成员变量已经完成初始化。const 成员变量初始化后不能再被赋值，因此必须在初始化列表阶段直接初始化，而不能在构造函数体内赋值。

---

## Q8：初始化列表和构造函数体有什么区别？

答：

初始化列表用于初始化成员变量；构造函数体是在成员已经初始化完成之后执行额外逻辑或赋值。对于 const 成员、引用成员、没有默认构造函数的成员，必须使用初始化列表。即使普通成员，也通常推荐用初始化列表，避免默认构造后再赋值的额外开销。

---

## Q9：引用和指针有什么区别？

答：

引用是对象的别名，定义时必须初始化，初始化后不能重新绑定，语义上一般不为空，使用时像普通对象一样访问。指针是保存地址的变量，可以为空，可以改变指向，使用时需要解引用。函数参数中，对象一定存在用引用，可能为空用指针。

---

## Q10：函数参数什么时候用智能指针？

答：

智能指针应该用于表达所有权关系，而不是简单替代裸指针或引用。函数要接管独占所有权时传 `unique_ptr`；函数要共享并保存对象生命周期时传 `shared_ptr`；函数只是观察 shared_ptr 管理的对象时用 `weak_ptr`。如果只是临时使用对象，通常传引用或裸指针即可。

---

## Q11：为什么只是临时使用对象时，不应该传 `unique_ptr<T>&`？

答：

因为 `unique_ptr<T>&` 表达的是函数可能修改调用者手里的智能指针本身，比如 reset 或重新赋值。但如果函数只是临时使用 `T` 对象，并不关心它由谁管理，也不接管所有权，应该传 `T&` 或 `const T&`。智能指针应该只在需要表达所有权关系时出现在接口中。

---

# 14. 今日易错点总结

```text
1. const int* 和 int const* 等价，都是指向 const int 的指针。
2. int* const p 表示指针本身不能改。
3. const 成员函数不是 const 返回值，而是函数承诺不修改对象。
4. const 对象只能调用 const 成员函数。
5. 非 const 对象可以调用 const 和非 const 成员函数。
6. mutable 成员可以在 const 成员函数中被修改。
7. const 成员变量必须在初始化列表中初始化。
8. 构造函数体内是赋值或额外逻辑，不是成员初始化阶段。
9. 引用必须初始化，且不能重新绑定。
10. 参数一定存在用引用，可能为空用指针。
11. 只是临时使用对象，不要传 unique_ptr/shared_ptr。
12. unique_ptr 参数表示接管所有权。
13. shared_ptr 参数通常表示共享并可能保存生命周期。
14. weak_ptr 表示观察但不拥有。
15. 智能指针不是普通引用和指针的替代品。
```

---

# 15. 今日核心代码片段

## 15.1 const 指针

```cpp
int a = 10;
int b = 20;

const int* p1 = &a;       // 内容不能改，指针能改
int* const p2 = &a;       // 指针不能改，内容能改
const int* const p3 = &a; // 内容和指针都不能改
```

---

## 15.2 const 成员函数

```cpp
class User {
public:
    int age() const {
        return age_;
    }

private:
    int age_ = 0;
};
```

---

## 15.3 const 重载

```cpp
class Buffer {
public:
    char& at(size_t i) {
        return data_[i];
    }

    const char& at(size_t i) const {
        return data_[i];
    }

private:
    std::vector<char> data_;
};
```

---

## 15.4 mutable

```cpp
class Counter {
public:
    int get() const {
        ++access_count_;
        return value_;
    }

private:
    int value_ = 0;
    mutable int access_count_ = 0;
};
```

---

## 15.5 mutable mutex

```cpp
#include <mutex>

class SafeData {
public:
    int get() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return value_;
    }

private:
    int value_ = 0;
    mutable std::mutex mutex_;
};
```

---

## 15.6 const 成员变量初始化

```cpp
class User {
public:
    User(int id) : id_(id) {}

private:
    const int id_;
};
```

---

## 15.7 引用成员初始化

```cpp
class Holder {
public:
    Holder(int& x) : ref_(x) {}

private:
    int& ref_;
};
```

---

## 15.8 没有默认构造函数的成员初始化

```cpp
class Engine {
public:
    Engine(int power) {}
};

class Car {
public:
    Car() : engine_(100) {}

private:
    Engine engine_;
};
```

---

## 15.9 函数接口设计

```cpp
void printVector(const std::vector<int>& v);

void sortVector(std::vector<int>& v);

void dumpUser(const User* user);

void resetConnection(Connection& conn);

void takeTask(std::unique_ptr<Task> task);

void useTask(Task& task);

void useTaskReadonly(const Task& task);
```

---

## 15.10 修改调用者的 unique_ptr

```cpp
void resetTask(std::unique_ptr<Task>& task) {
    task = std::make_unique<Task>();
}
```

---

## 15.11 保存 shared_ptr

```cpp
class Manager {
public:
    void saveConnection(std::shared_ptr<Connection> conn) {
        connections_.push_back(conn);
    }

private:
    std::vector<std::shared_ptr<Connection>> connections_;
};
```

---

## 15.12 使用 weak_ptr 观察对象

```cpp
class Session {
public:
    void setOwner(std::weak_ptr<User> owner) {
        owner_ = owner;
    }

    void notifyOwner() {
        if (auto owner = owner_.lock()) {
            owner->notify();
        }
    }

private:
    std::weak_ptr<User> owner_;
};
```

---

# 16. 明日开始前复习问题

```text
1. const int* 和 int* const 有什么区别？
2. const int* const 表示什么？
3. const 成员函数中的 this 是什么类型？
4. const 对象为什么不能调用非 const 成员函数？
5. mutable 有什么用？
6. const 成员变量为什么必须在初始化列表中初始化？
7. 初始化列表和构造函数体的区别是什么？
8. 哪些成员必须在初始化列表中初始化？
9. 引用和指针有什么区别？
10. 参数一定不为空时，用引用还是指针？
11. 参数可能为空时，用引用还是指针？
12. 函数只读 vector，参数怎么写？
13. 函数修改 vector，参数怎么写？
14. 函数接管 unique_ptr 所有权，参数怎么写？
15. 函数只是临时使用 unique_ptr 管理的对象，参数应该写 unique_ptr& 还是 T&？
16. 什么时候函数参数应该使用 shared_ptr？
17. 什么时候应该使用 weak_ptr？
```

---

# 17. Day 4 总结

Day 4 的核心收获：

```text
const：表达不可修改。
引用：表达对象一定存在的借用。
指针：表达对象可能为空的借用。
智能指针：表达所有权关系。
初始化列表：负责成员初始化。
构造函数体：负责初始化后的逻辑。
```

最终形成的工程判断：

```text
只读且一定存在 → const T&
需要修改且一定存在 → T&
可能为空且只读 → const T*
可能为空且修改 → T*
小对象 → T
接管所有权 → unique_ptr<T>
修改调用者的 unique_ptr → unique_ptr<T>&
共享保存生命周期 → shared_ptr<T>
观察但不拥有 → weak_ptr<T>
只是临时使用对象 → 不要传智能指针，传 T& / const T& / T*
```

最重要的一句话：

```text
函数参数类型本身就是接口语义。
```
