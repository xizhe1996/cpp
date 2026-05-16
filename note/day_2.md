# Day 2｜智能指针与 vector 高频基础

# 1. 今日学习目标

Day 2 的核心目标：

- 理解裸指针手动管理资源的问题
- 掌握 `unique_ptr` 的独占所有权语义
- 掌握 `shared_ptr` 的共享所有权和引用计数
- 掌握 `weak_ptr` 的弱引用语义与循环引用问题
- 理解智能指针的传参方式和所有权表达
- 理解自定义 deleter 与自定义 new 的区别
- 掌握 `shared_ptr` 常见错误用法
- 掌握 `vector` 的 `size/capacity/reserve/resize`
- 理解 `vector` 扩容、移动/拷贝、迭代器失效
- 掌握 `vector erase` 和 `erase-remove idiom`

核心主线：

> 智能指针的本质是用 RAII 管理资源所有权；STL 容器的本质是理解其内存模型和元素生命周期。


# 2. 裸指针的问题

裸指针需要手动 `delete`：

```cpp
void rawPointerLeak(bool error) {
    User* p = new User();

    if (error) {
        return; // 忘记 delete，内存泄漏
    }

    p->hello();

    delete p;
}
````

如果中途 `return` 或抛异常，`delete` 没有执行，就会发生资源泄漏。

---

# 3. unique_ptr 基础

## 3.1 unique_ptr 的核心语义

`std::unique_ptr<T>` 表示：

```text
独占所有权。
同一时刻只有一个 unique_ptr 负责释放对象。
```

特点：

```text
不能拷贝
可以移动
离开作用域自动释放资源
```

示例：

```cpp
void uniquePtrNoLeak(bool error) {
    auto ptr = std::make_unique<User>();

    if (error) {
        return;
    }

    ptr->hello();
}
```

即使中途 `return`，`ptr` 离开作用域时也会自动释放 `User`。

---

## 3.2 make_unique

推荐写法：

```cpp
auto p = std::make_unique<User>();
```

不推荐：

```cpp
std::unique_ptr<User> p(new User());
```

`make_unique` 的主要好处：

```text
写法简洁
避免显式 new
异常安全习惯更好
符合 RAII 风格
```

注意：

```text
make_unique 的主要优势不是减少内存分配次数。
unique_ptr 没有控制块，默认通常只需要一次对象分配。
```

---

# 4. make_unique 与 make_shared 的区别

## 4.1 make_shared

`shared_ptr` 需要：

```text
对象本身
控制块 control block
```

控制块中通常包含：

```text
强引用计数
弱引用计数
deleter
allocator 信息
```

普通写法：

```cpp
std::shared_ptr<User> p(new User());
```

可能需要两次分配：

```text
一次分配 User 对象
一次分配控制块
```

而：

```cpp
auto p = std::make_shared<User>();
```

通常可以把：

```text
User 对象 + 控制块
```

放在同一块内存中，一次分配完成。

---

## 4.2 make_unique

`unique_ptr` 没有控制块，默认只保存对象指针。

所以：

```cpp
std::unique_ptr<User> p(new User());
auto p = std::make_unique<User>();
```

通常都是一次对象分配。

`make_unique` 的主要价值是：

```text
避免显式 new
更安全
更简洁
```

---

# 5. unique_ptr 不能拷贝，但可以移动

## 5.1 不能拷贝

错误：

```cpp
auto p1 = std::make_unique<User>();
auto p2 = p1; // 编译错误
```

原因：

如果允许拷贝，两个 `unique_ptr` 会认为自己都拥有同一个对象，最终 double free。

---

## 5.2 可以移动

正确：

```cpp
auto p1 = std::make_unique<User>();
auto p2 = std::move(p1);
```

移动后：

```text
p2 接管资源
p1 变为空 unique_ptr
```

注意：

```cpp
if (!p1) {
    std::cout << "p1 is null\n";
}
```

移动后的 `p1` 仍然是一个有效的 `unique_ptr` 对象，只是不再管理原资源。

可以判断、重新赋值，但不能解引用访问原对象。

---

# 6. std::move 的本质

`std::move` 本身不移动资源。

它只是：

```text
把对象转换成右值，使其满足移动构造 / 移动赋值的参数要求。
```

真正发生资源转移的是：

```text
目标类型的移动构造函数或移动赋值函数。
```

示例：

```cpp
auto p2 = std::move(p1);
```

实际含义：

```text
std::move(p1) 把 p1 转成右值
unique_ptr 的移动构造接管 p1 的指针
p1 被置空
```

---

# 7. std::move 后一定调用移动构造吗？

不一定。

## 情况 1：有可用移动构造

```cpp
A(A&&);
```

调用：

```cpp
A b(std::move(a));
```

会调用移动构造。

---

## 情况 2：没有移动构造，但有可用拷贝构造

```cpp
A(const A&);
```

右值可以绑定到 `const A&`，因此可能调用拷贝构造。

---

## 情况 3：移动构造显式 delete

```cpp
A(const A&);
A(A&&) = delete;
```

调用：

```cpp
A b(std::move(a));
```

通常会编译报错。

因为重载决议会优先选择 `A(A&&)`，但它被 delete，不会自动退回拷贝构造。

---

## 面试表达

```text
std::move 只是类型转换，不保证真的移动。具体调用移动构造还是拷贝构造，取决于目标类型是否有可用的移动构造。如果没有移动构造但有可用拷贝构造，右值可能绑定到 const 引用从而调用拷贝。如果移动构造被显式 delete，重载决议可能选中 deleted 函数并导致编译失败。
```

---

# 8. unique_ptr 常用 API

## 8.1 operator-> / operator*

```cpp
auto p = std::make_unique<User>();

p->hello();
(*p).hello();
```

---

## 8.2 判断是否为空

```cpp
if (p) {
    p->hello();
}
```

---

## 8.3 get()

```cpp
User* raw = p.get();
```

作用：

```text
获取底层裸指针
不转移所有权
不能 delete raw
```

错误：

```cpp
User* raw = p.get();
delete raw; // 错误，unique_ptr 后续还会 delete
```

---

## 8.4 release()

```cpp
User* raw = p.release();
```

作用：

```text
释放 unique_ptr 对资源的所有权
返回裸指针
p 变为空
调用者必须自己负责 delete
```

`release()` 比较危险，应谨慎使用。

---

## 8.5 reset()

```cpp
p.reset();
```

作用：

```text
释放当前管理的对象
p 变为空
```

也可以接管新对象：

```cpp
p.reset(new User());
```

但更推荐：

```cpp
p = std::make_unique<User>();
```

---

# 9. unique_ptr 函数传参

## 9.1 只是借用对象

如果函数只是临时使用对象，不接管所有权：

```cpp
void use(User* user); // 可为空
void use(User& user); // 不为空
```

调用：

```cpp
auto p = std::make_unique<User>();

use(p.get());
use(*p);
```

---

## 9.2 接管所有权

如果函数要接管对象所有权：

```cpp
void takeOwnership(std::unique_ptr<User> user);
```

调用：

```cpp
auto p = std::make_unique<User>();
takeOwnership(std::move(p));
```

调用后：

```text
p 变为空
函数内部 user 拥有对象
函数结束时对象释放
```

---

## 9.3 修改调用者的 unique_ptr

```cpp
void resetUser(std::unique_ptr<User>& user) {
    user = std::make_unique<User>();
}
```

---

## 9.4 传参语义总结

```cpp
void use(User* p);                      // 借用，可为空
void use(User& p);                      // 借用，不为空
void take(std::unique_ptr<User> p);     // 接管所有权
void reset(std::unique_ptr<User>& p);   // 修改调用者的 unique_ptr
```

---

# 10. shared_ptr 基础

## 10.1 shared_ptr 核心语义

`std::shared_ptr<T>` 表示：

```text
共享所有权。
多个 shared_ptr 可以共同拥有同一个对象。
最后一个 shared_ptr 销毁时，对象才会被释放。
```

示例：

```cpp
auto p1 = std::make_shared<User>();
auto p2 = p1;
```

此时：

```text
p1 和 p2 指向同一个 User
引用计数为 2
```

---

## 10.2 shared_ptr 拷贝是否拷贝对象？

不会。

```cpp
auto p1 = std::make_shared<User>();
auto p2 = p1;
```

只是：

```text
拷贝 shared_ptr 本身
共享同一个对象
强引用计数 +1
```

通常：

```cpp
p1.get() == p2.get()
```

---

## 10.3 shared_ptr 什么时候释放对象？

当最后一个拥有对象的 `shared_ptr` 被销毁、reset 或重新赋值时。

```cpp
auto p1 = std::make_shared<User>();

{
    auto p2 = p1;
} // p2 销毁，计数减 1

p1.reset(); // 如果计数变 0，对象析构
```

---

# 11. use_count()

```cpp
p.use_count();
```

适合：

```text
学习
调试
观察引用计数
```

不适合：

```text
依赖它写核心业务逻辑
```

尤其多线程环境中，引用计数可能随时被其他线程改变。

---

# 12. shared_ptr 传参

## 12.1 临时使用对象

不建议：

```cpp
void print(std::shared_ptr<User> user);
```

因为这会增加引用计数，并暗示函数参与所有权管理。

推荐：

```cpp
void print(const User& user);
void print(const User* user);
```

---

## 12.2 函数需要共享 / 保存所有权

可以传：

```cpp
void saveUser(std::shared_ptr<User> user);
```

适用于函数需要保存对象、延长生命周期的场景。

---

# 13. weak_ptr 基础

## 13.1 weak_ptr 核心语义

`weak_ptr` 是弱引用：

```text
观察 shared_ptr 管理的对象
不增加强引用计数
不拥有对象
不延长对象生命周期
```

---

## 13.2 shared_ptr 循环引用问题

错误示例：

```cpp
class B;

class A {
public:
    std::shared_ptr<B> b;
};

class B {
public:
    std::shared_ptr<A> a;
};
```

使用：

```cpp
auto pa = std::make_shared<A>();
auto pb = std::make_shared<B>();

pa->b = pb;
pb->a = pa;
```

结果：

```text
A strong count = 2
B strong count = 2
```

外部 `pa/pb` 销毁后，内部仍然互相持有，强引用计数无法归零，导致对象不析构。

---

## 13.3 weak_ptr 解决循环引用

修改其中一边：

```cpp
class B {
public:
    std::weak_ptr<A> a;
};
```

因为 `weak_ptr` 不增加强引用计数，可以打破强引用环。

核心：

```text
weak_ptr 不是帮忙析构，而是不参与所有权，打破强引用环。
```

---

## 13.4 weak_ptr 不能直接访问对象

错误：

```cpp
weak->hello(); // 不允许
```

原因：

```text
weak_ptr 不拥有对象，也不保证对象还活着。
```

必须使用：

```cpp
auto sp = weak.lock();

if (sp) {
    sp->hello();
}
```

---

## 13.5 lock()

`lock()` 的作用：

```text
尝试从 weak_ptr 获取 shared_ptr。
如果对象仍然存在，返回有效 shared_ptr，并在使用期间临时增加强引用计数。
如果对象已经释放，返回空 shared_ptr。
```

---

## 13.6 expired()

```cpp
if (weak.expired()) {
    std::cout << "object expired\n";
}
```

表示对象是否已经失效。

实际访问对象时，更推荐：

```cpp
auto sp = weak.lock();
if (sp) {
    // 使用 sp
}
```

---

# 14. shared_ptr / weak_ptr 控制块

## 14.1 控制块内容

控制块通常保存：

```text
强引用计数
弱引用计数
deleter
allocator 信息
```

---

## 14.2 强引用计数归零

```text
strong count == 0
```

被管理对象析构。

---

## 14.3 弱引用计数归零

当：

```text
strong count == 0
weak count == 0
```

控制块本身释放。

---

## 14.4 为什么对象销毁后控制块还要存在？

因为 `weak_ptr` 还需要判断对象是否已经失效。

```cpp
std::weak_ptr<User> wp;

{
    auto sp = std::make_shared<User>();
    wp = sp;
} // User 析构

if (wp.expired()) {
    std::cout << "expired\n";
}
```

这里对象已经没了，但 `weak_ptr` 仍然可以通过控制块判断状态。

---

# 15. 智能指针大小

在常见 64 位平台上：

```text
unique_ptr<T>：通常 8 字节
shared_ptr<T>：通常 16 字节
weak_ptr<T>：通常 16 字节
```

大致原因：

```text
unique_ptr：对象指针
shared_ptr：对象指针 + 控制块指针
weak_ptr：对象指针 + 控制块指针
```

注意：

```text
这是常见实现，不是 C++ 标准强制规定。
```

---

## 15.1 unique_ptr 大小的例外

如果 `unique_ptr` 带有状态 deleter，大小可能增加。

```cpp
struct MyDeleter {
    int tag;
    void operator()(User* p) const {
        delete p;
    }
};

std::unique_ptr<User, MyDeleter> p;
```

它可能需要保存：

```text
User* 指针
MyDeleter 对象
```

所以不一定是 8 字节。

---

# 16. 自定义 deleter

## 16.1 deleter 解决什么问题？

自定义 deleter 解决：

```text
智能指针析构时怎么释放资源。
```

默认情况下：

```cpp
std::unique_ptr<User> p(new User());
```

析构时调用：

```cpp
delete p;
```

但是如果资源不是用 `new` 创建的，就不能用默认 deleter。

---

## 16.2 FILE* 示例

`fopen` / `fclose` 必须配套。

```cpp
std::unique_ptr<FILE, decltype(&fclose)> fp(
    fopen("test.txt", "w"),
    &fclose
);
```

原因：

```text
fopen 返回 FILE*
FILE* 不是通过 new 创建的对象
不能用 delete 释放
必须用 fclose 释放
```

---

## 16.3 malloc 示例

`malloc` / `free` 必须配套。

```cpp
std::unique_ptr<int, decltype(&free)> p(
    static_cast<int*>(std::malloc(sizeof(int))),
    &free
);
```

---

## 16.4 get() 与自定义 deleter

```cpp
fputs("hello\n", fp.get());
```

`fp.get()`：

```text
返回 FILE* 裸指针
只是借用给 C API
不转移所有权
```

---

# 17. 自定义 new / operator new

`new User()` 大致分两步：

```text
1. operator new 分配原始内存
2. 在这块内存上调用 User 构造函数
```

`delete p` 大致分两步：

```text
1. 调用 User 析构函数
2. operator delete 释放原始内存
```

类可以自定义：

```cpp
static void* operator new(std::size_t size);
static void operator delete(void* ptr) noexcept;
```

示例：

```cpp
class User {
public:
    static void* operator new(std::size_t size) {
        std::cout << "custom operator new\n";
        return std::malloc(size);
    }

    static void operator delete(void* ptr) noexcept {
        std::cout << "custom operator delete\n";
        std::free(ptr);
    }

    User() {
        std::cout << "User constructor\n";
    }

    ~User() {
        std::cout << "User destructor\n";
    }
};
```

---

# 18. 自定义 deleter / operator new / allocator 区别

```text
概念                    解决的问题

自定义 deleter           智能指针析构时怎么释放资源
operator new/delete      new/delete 时怎么分配/释放原始内存
allocator                STL 容器内部怎么分配元素内存
RAII wrapper             把资源生命周期绑定到对象生命周期
```

---

# 19. shared_ptr 常见错误用法

## 19.1 用同一个裸指针构造多个 shared_ptr

错误：

```cpp
User* raw = new User();

std::shared_ptr<User> p1(raw);
std::shared_ptr<User> p2(raw);
```

问题：

```text
p1 和 p2 各自创建独立控制块。
两个控制块都认为自己拥有 raw。
```

结果：

```text
p1 引用计数归零 → delete raw
p2 引用计数归零 → 再次 delete raw
```

导致 double free。

---

## 19.2 正确共享方式

正确：

```cpp
auto p1 = std::make_shared<User>();
auto p2 = p1;
```

此时：

```text
p1 和 p2 共享同一个控制块
引用计数为 2
最终只 delete 一次
```

---

## 19.3 不能直接 return shared_ptr<T>(this)

错误：

```cpp
class User {
public:
    std::shared_ptr<User> getShared() {
        return std::shared_ptr<User>(this);
    }
};
```

如果对象本来已经由 `shared_ptr` 管理：

```cpp
auto p1 = std::make_shared<User>();
auto p2 = p1->getShared();
```

则：

```text
p1 有一个控制块
p2 通过 this 又创建一个新控制块
两个控制块管理同一个 User
```

导致 double free。

---

# 20. enable_shared_from_this

如果类需要在成员函数里返回指向自己的 `shared_ptr`，应该继承：

```cpp
std::enable_shared_from_this<T>
```

示例：

```cpp
class User : public std::enable_shared_from_this<User> {
public:
    std::shared_ptr<User> getShared() {
        return shared_from_this();
    }
};
```

使用：

```cpp
auto p1 = std::make_shared<User>();
auto p2 = p1->getShared();
```

`shared_from_this()` 不会创建新的控制块，而是基于已有控制块生成新的 `shared_ptr`。

---

## 20.1 shared_from_this 的前提

前提：

```text
当前对象必须已经被 shared_ptr 正确管理。
```

错误：

```cpp
User u;
auto p = u.shared_from_this(); // 错
```

错误：

```cpp
User* raw = new User();
auto p = raw->shared_from_this(); // 错
```

因为此时没有已有控制块。

通常会抛出：

```cpp
std::bad_weak_ptr
```

---

## 20.2 enable_shared_from_this 的大致原理

可以简单理解为：

```text
enable_shared_from_this<T> 内部维护一个 weak_ptr<T>。
当对象第一次被 shared_ptr 管理时，这个 weak_ptr 绑定到 shared_ptr 的控制块。
调用 shared_from_this() 时，本质上是对这个 weak_ptr 调用 lock()。
```

因此它不会重复创建控制块。

---

# 21. shared_ptr 的核心不是指针值，而是控制块

重要结论：

```text
shared_ptr 的共享所有权不是靠裸指针地址相同实现的，而是靠共享同一个控制块实现的。

同一个对象必须只有一个 shared_ptr 控制块。
多个 shared_ptr 可以共享同一个控制块。
不能让多个控制块管理同一个裸指针。
```

---

# 22. vector 基础模型

## 22.1 vector 的本质

`std::vector<T>` 可以理解为：

```text
一段连续内存 + 当前元素数量 + 当前容量
```

内部大概有：

```text
begin：数据起始位置
end：当前元素结束位置
cap：当前容量结束位置
```

---

## 22.2 size

```cpp
v.size();
```

表示：

```text
当前有效元素数量。
```

---

## 22.3 capacity

```cpp
v.capacity();
```

表示：

```text
当前已分配内存最多能容纳多少个元素。
```

`capacity` 可以大于 `size`。

---

# 23. reserve 和 resize

## 23.1 reserve

```cpp
v.reserve(5);
```

作用：

```text
预留容量，保证 capacity 至少为 5。
不创建元素，不改变 size。
```

示例：

```cpp
std::vector<int> v;
v.reserve(5);

v.size();     // 0
v.capacity(); // >= 5
```

此时不能直接：

```cpp
v[0] = 10; // 错误
```

因为没有有效元素。

---

## 23.2 resize

```cpp
v.resize(3);
```

作用：

```text
调整元素数量，改变 size。
如果变大，会默认构造新元素。
如果变小，会销毁多余元素。
```

示例：

```cpp
std::vector<int> v;
v.push_back(1);
v.resize(3);
```

结果：

```text
1 0 0
```

---

## 23.3 reserve vs resize 面试回答

```text
reserve 只改变 vector 的容量 capacity，用于提前分配内存，避免频繁扩容，但不会创建元素，也不会改变 size。resize 会改变 vector 的元素数量 size，如果变大，会默认构造新元素；如果变小，会销毁多余元素。因此 reserve 后不能直接通过下标访问新容量范围内的位置，而 resize 后可以访问新创建的元素。
```

---

# 24. vector 扩容

当：

```text
size == capacity
```

再插入新元素时，`vector` 需要扩容。

扩容通常包括：

```text
1. 申请一块更大的连续内存
2. 把旧元素移动 / 拷贝到新内存
3. 释放旧内存
4. 插入新元素
```

---

## 24.1 扩容策略

常见实现中可能出现：

```text
1 → 2 → 4 → 8 → 16
```

但 C++ 标准没有规定扩容必须是 2 倍。

更准确说法：

```text
vector 在容量不足时会重新分配更大的连续内存，增长策略由具体标准库实现决定。
```

---

# 25. vector 扩容导致失效

如果扩容发生：

```text
底层 data 地址变化
旧内存释放
```

因此：

```text
iterator 失效
指针失效
引用失效
```

示例：

```cpp
std::vector<int> v;
v.push_back(1);

int* p = &v[0];

v.push_back(2); // 可能扩容

std::cout << *p << std::endl; // 可能未定义行为
```

---

## 25.1 push_back 一定导致迭代器失效吗？

不一定。

```text
如果 push_back 没有触发扩容：
    原有元素的 iterator / 指针 / 引用通常仍然有效。
    但 end() 迭代器会变化。

如果 push_back 触发扩容：
    所有 iterator / 指针 / 引用都会失效。
```

---

# 26. vector::data()

```cpp
v.data();
```

返回底层数组起始地址。

常用于和 C API 交互。

注意：

```text
vector 扩容后，原来的 data() 地址可能失效。
```

---

# 27. reserve 的作用

`reserve` 可以提前分配容量。

好处：

```text
减少重新分配
减少元素移动 / 拷贝成本
减少 iterator / 指针 / 引用失效概率
```

但如果插入数量超过预留容量，仍然可能扩容。

---

# 28. vector 中对象的拷贝 / 移动

## 28.1 push_back(T(...))

```cpp
v.push_back(Item(1));
```

通常过程：

```text
1. 创建临时对象 Item(1)
2. 将临时对象移动进 vector
3. 析构临时对象
```

---

## 28.2 emplace_back

```cpp
v.emplace_back(1);
```

通常直接在 `vector` 内部存储位置构造对象。

好处：

```text
少一次临时对象构造
少一次移动构造
```

---

## 28.3 扩容时为什么移动 / 拷贝旧元素？

因为 `vector` 要求元素存储在连续内存中。

当原连续内存容量不足时，需要申请更大的新内存，并把旧元素移动或拷贝到新内存中。

---

# 29. noexcept 对 vector 扩容的影响

如果元素类型的移动构造是：

```cpp
T(T&&) noexcept;
```

`vector` 扩容时通常更愿意移动旧元素。

如果移动构造可能抛异常，而拷贝构造可用，`vector` 为了异常安全可能选择拷贝。

所以资源类移动构造一般建议写：

```cpp
T(T&& other) noexcept;
T& operator=(T&& other) noexcept;
```

---

# 30. vector erase

## 30.1 删除单个元素

```cpp
auto it = v.begin() + 2;
it = v.erase(it);
```

`erase` 返回：

```text
指向被删除元素后一个位置的迭代器。
```

如果删除的是最后一个元素，则返回：

```cpp
v.end()
```

---

## 30.2 erase 后迭代器失效

`vector` 删除中间元素后，后面的元素会整体前移。

因此从删除位置开始，到末尾的迭代器、指针、引用通常都会失效。

错误写法：

```cpp
for (auto it = v.begin(); it != v.end(); ++it) {
    if (*it % 2 == 0) {
        v.erase(it); // it 失效
    }
}
```

正确写法：

```cpp
for (auto it = v.begin(); it != v.end(); ) {
    if (*it % 2 == 0) {
        it = v.erase(it);
    } else {
        ++it;
    }
}
```

---

# 31. erase-remove idiom

删除所有满足条件的元素：

```cpp
v.erase(
    std::remove_if(v.begin(), v.end(), [](int x) {
        return x % 2 == 0;
    }),
    v.end()
);
```

---

## 31.1 remove_if 的作用

`remove_if` 不真正删除元素，不改变容器 size。

它会：

```text
把“不需要删除”的元素移动到前面
返回新的逻辑结尾 new_end
```

---

## 31.2 erase 的作用

```cpp
v.erase(new_end, v.end());
```

真正删除尾部无效元素，改变容器 size。

---

# 32. 今日重要面试问答

## Q1：unique_ptr 和 shared_ptr 的区别？

答：

`unique_ptr` 表示独占所有权，不能拷贝，只能移动，离开作用域时自动释放资源，通常大小接近一个裸指针。`shared_ptr` 表示共享所有权，可以拷贝，多个 shared_ptr 共享同一个控制块，通过强引用计数管理对象生命周期，最后一个 shared_ptr 销毁时对象才会释放。

---

## Q2：std::move 一定会移动资源吗？

答：

不一定。`std::move` 本身只是把对象转换成右值，并不真正移动资源。资源是否移动取决于目标类型是否有可用的移动构造或移动赋值。如果没有移动构造但有可用拷贝构造，可能调用拷贝构造。如果移动构造被显式 delete，可能直接编译失败。

---

## Q3：shared_ptr 的控制块是什么？

答：

`shared_ptr` 的控制块通常保存强引用计数、弱引用计数、deleter、allocator 等信息。强引用计数决定对象生命周期，强引用计数归零时对象析构；弱引用计数用于管理控制块生命周期，强弱引用都归零后控制块释放。

---

## Q4：weak_ptr 为什么能解决循环引用？

答：

`weak_ptr` 是弱引用，不增加强引用计数，也不拥有对象。两个对象互相用 `shared_ptr` 持有会形成强引用环，引用计数无法归零。将其中一边改成 `weak_ptr` 可以打破强引用环，使对象在外部 `shared_ptr` 销毁后正常析构。

---

## Q5：为什么不能用同一个裸指针构造两个 shared_ptr？

答：

因为每次用裸指针构造 `shared_ptr` 都会创建新的控制块。两个独立控制块管理同一个对象时，它们的引用计数彼此独立，都会在自己的强引用计数归零时释放对象，最终导致 double free 或 use-after-free。

---

## Q6：为什么不能在成员函数里 return std::shared_ptr<T>(this)？

答：

因为 `this` 是裸指针，用它构造 `shared_ptr` 会创建新的控制块。如果当前对象已经由其他 `shared_ptr` 管理，就会出现多个控制块管理同一个对象，导致 double free。正确做法是继承 `std::enable_shared_from_this<T>`，通过 `shared_from_this()` 从已有控制块生成新的 `shared_ptr`。

---

## Q7：reserve 和 resize 有什么区别？

答：

`reserve` 只改变 `vector` 的容量 `capacity`，不创建元素，不改变 `size`，用于提前分配内存、减少扩容。`resize` 会改变元素数量 `size`，如果变大，会默认构造新元素；如果变小，会销毁多余元素。因此 `reserve` 后不能直接通过下标访问新容量范围，而 `resize` 后可以访问新创建的元素。

---

## Q8：vector 扩容为什么会导致迭代器失效？

答：

`vector` 底层要求连续内存。扩容时会申请一块更大的连续内存，把旧元素移动或拷贝过去，然后释放旧内存。因此原来指向旧内存的 iterator、指针和引用都会失效。

---

## Q9：push_back 和 emplace_back 有什么区别？

答：

`push_back(T(args...))` 通常会先创建一个临时对象，再将它移动或拷贝进容器。`emplace_back(args...)` 会直接在容器内部存储位置构造对象，通常可以减少一次临时对象和移动/拷贝。但如果已经有现成对象，应该根据语义选择 push_back 拷贝或移动。

---

## Q10：erase-remove idiom 是什么？

答：

`erase-remove idiom` 是从 `vector` 等顺序容器中删除满足条件元素的常用写法。`remove_if` 不真正删除元素，而是把保留元素移动到前面，并返回新的逻辑结尾；`erase` 再删除从新逻辑结尾到原末尾的元素，真正改变容器 size。

---

# 33. 今日易错点总结

```text
1. unique_ptr 不能拷贝，只能移动。
2. std::move 不移动资源，只做右值转换。
3. p.get() 返回裸指针，不转移所有权，不能 delete。
4. shared_ptr 拷贝不拷贝对象，只增加引用计数。
5. 临时使用对象时，不建议传 shared_ptr。
6. weak_ptr 不增加强引用计数，访问前必须 lock。
7. 同一个裸指针不能构造多个 shared_ptr。
8. 不能直接 return shared_ptr<T>(this)。
9. reserve 只改 capacity，不改 size。
10. resize 改 size，可能创建或销毁元素。
11. vector 扩容会导致 iterator / 指针 / 引用失效。
12. remove_if 不改变 size，erase 才真正删除元素。
```

---

# 34. 今日核心代码片段

## 34.1 unique_ptr 借用与接管

```cpp
void borrowByPointer(User* user) {
    if (user) {
        user->hello();
    }
}

void borrowByRef(User& user) {
    user.hello();
}

void takeOwnership(std::unique_ptr<User> user) {
    user->hello();
}

int main() {
    auto p = std::make_unique<User>();

    borrowByPointer(p.get());
    borrowByRef(*p);

    takeOwnership(std::move(p));

    if (!p) {
        std::cout << "p is null\n";
    }
}
```

---

## 34.2 shared_ptr 引用计数

```cpp
auto p1 = std::make_shared<User>();

std::cout << p1.use_count() << std::endl; // 1

{
    auto p2 = p1;
    std::cout << p1.use_count() << std::endl; // 2
    std::cout << p2.use_count() << std::endl; // 2
}

std::cout << p1.use_count() << std::endl; // 1

p1.reset(); // 如果是最后一个 shared_ptr，对象析构
```

---

## 34.3 weak_ptr 解决循环引用

```cpp
class B;

class A {
public:
    std::shared_ptr<B> b;
};

class B {
public:
    std::weak_ptr<A> a;
};

int main() {
    auto pa = std::make_shared<A>();
    auto pb = std::make_shared<B>();

    pa->b = pb;
    pb->a = pa;
}
```

---

## 34.4 FILE* 自定义 deleter

```cpp
#include <cstdio>
#include <memory>

int main() {
    std::unique_ptr<FILE, decltype(&fclose)> fp(
        fopen("test.txt", "w"),
        &fclose
    );

    if (!fp) {
        return 1;
    }

    fputs("hello custom deleter\n", fp.get());

    return 0;
}
```

---

## 34.5 shared_ptr 错误：两个控制块

```cpp
User* raw = new User();

std::shared_ptr<User> p1(raw);
std::shared_ptr<User> p2(raw); // 错误：独立控制块，double free
```

正确：

```cpp
auto p1 = std::make_shared<User>();
auto p2 = p1;
```

---

## 34.6 vector reserve / resize

```cpp
std::vector<int> v;

v.reserve(5);
// size = 0, capacity >= 5

v.push_back(1);

v.resize(3);
// 元素：1 0 0
```

---

## 34.7 vector erase 删除偶数

```cpp
std::vector<int> v = {1, 2, 3, 4, 5, 6};

for (auto it = v.begin(); it != v.end(); ) {
    if (*it % 2 == 0) {
        it = v.erase(it);
    } else {
        ++it;
    }
}
```

---

## 34.8 erase-remove idiom

```cpp
std::vector<int> v = {1, 2, 3, 4, 5, 6};

v.erase(
    std::remove_if(v.begin(), v.end(), [](int x) {
        return x % 2 == 0;
    }),
    v.end()
);
```

---

# 35. 明日开始前复习问题

```text
1. unique_ptr 为什么不能拷贝？
2. std::move 是否一定导致移动构造？
3. get / release / reset 的区别是什么？
4. shared_ptr 的控制块保存什么？
5. weak_ptr 为什么不增加强引用计数？
6. weak_ptr 如何安全访问对象？
7. 为什么同一个裸指针不能构造多个 shared_ptr？
8. shared_from_this 的前提是什么？
9. reserve 和 resize 的区别是什么？
10. vector 扩容为什么导致迭代器失效？
11. push_back 和 emplace_back 有什么区别？
12. erase-remove idiom 的两步分别做什么？
```

---

# 36. Day 2 总结

Day 2 的核心收获：

```text
智能指针解决的是资源所有权问题。
vector 训练的是连续内存模型和对象生命周期问题。
```

智能指针主线：

```text
unique_ptr：独占资源
shared_ptr：共享资源
weak_ptr：观察资源
custom deleter：自定义释放方式
```

vector 主线：

```text
size：当前元素数
capacity：当前容量
reserve：预留空间
resize：改变元素数量
扩容：重新分配连续内存
失效：旧地址相关的 iterator / pointer / reference 失效
erase：返回下一个有效迭代器
remove_if：不真正删除，只返回逻辑尾部
```

最终要形成的工程判断：

```text
函数只是使用对象 → 传引用 / 指针
函数要独占接管 → 传 unique_ptr
函数要共享保存 → 传 shared_ptr
函数只观察生命周期 → 用 weak_ptr

vector 已知大小 → reserve
需要创建元素 → resize
删除元素 → 注意迭代器失效
批量删除 → erase-remove idiom
```

