# Day 14｜Reactor 模型入门：EventLoop / Channel / Acceptor / Connection / ThreadPool 职责拆分

# 1. 今日学习目标

Day 14 是 Day 1～Day 14 两周面试准备内容的收束日之一，重点不是继续无限深入源码，而是把前面学习的 socket、epoll、非阻塞 IO、LT/ET、线程池等内容，组织成一个面试中能讲清楚的 Reactor 架构。

今日目标：

- 理解 Reactor 模型解决什么问题
- 理解 Reactor 和 epoll 的关系
- 理解 EventLoop / Channel / Acceptor / Connection / Buffer 的职责
- 理解 `ev.data.ptr = channel` 的意义
- 理解 Channel 中 `events_` 和 `revents_` 的区别
- 理解 Connection 为什么需要 inputBuffer / outputBuffer
- 理解线程池和 Reactor 的关系
- 理解请求链路和回复链路如何流转
- 整理一套 MiniNet / 网络框架项目的面试表达

核心主线：

> Reactor 是基于 IO 多路复用的事件驱动模型。EventLoop 负责事件等待和分发，Channel 封装 fd 和事件回调，Acceptor 负责新连接，Connection 负责已连接客户端的读写和状态管理，线程池负责耗时业务处理。

---

# 2. 为什么需要 Reactor？

前面 Day 10～Day 13 已经写过非阻塞 epoll echo server，大致结构是：

```cpp
while (true) {
  int n = epoll_wait(epfd, events, MAX_EVENTS, -1);

  for (int i = 0; i < n; ++i) {
    int fd = events[i].data.fd;

    if (fd == listenfd) {
      handleAccept(epfd, listenfd);
    } else {
      handleRead(epfd, fd);
    }
  }
}
````

这段代码可以工作，但问题是：

```text
1. epoll_wait 逻辑混在 main 中
2. fd 和事件没有对象封装
3. listenfd 和 connfd 通过 if 判断区分
4. 每个连接没有独立状态
5. 没有 inputBuffer / outputBuffer
6. 没有统一的回调机制
7. 后续加入线程池、协议解析、连接关闭会越来越乱
```

Reactor 要解决的问题是：

```text
把事件等待、事件分发、连接管理、读写处理、缓冲区管理拆成不同组件。
```

一句话：

```text
Reactor 不是替代 epoll，而是基于 epoll 做事件驱动封装。
```

---

# 3. Reactor 的一句话定义

Reactor 是一种事件驱动的网络 IO 模型。

它通过 IO 多路复用机制同时监听多个 fd，当某个 fd 的事件就绪后，由事件循环将事件分发给对应的处理对象或回调函数。

在 Linux 下，Reactor 通常基于 epoll 实现：

```text
epoll_wait：
    等待 IO 事件

EventLoop：
    负责事件循环和事件分发

Channel：
    封装 fd 和事件回调

Acceptor：
    处理新连接

Connection：
    处理已建立连接上的读写
```

压缩理解：

```text
Reactor = epoll_wait + 事件分发 + 回调处理
```

---

# 4. Reactor 和 epoll 的关系

## 4.1 epoll 是什么？

epoll 是 Linux 提供的 IO 多路复用机制，是系统调用层面的能力。

它负责：

```text
监听多个 fd
等待 fd 就绪
返回就绪事件
```

常用 API：

```text
epoll_create1
epoll_ctl
epoll_wait
```

---

## 4.2 Reactor 是什么？

Reactor 是一种架构模型。

它基于 epoll 这样的 IO 多路复用机制，将就绪事件封装成对象和回调，并进行分发。

可以这样区分：

```text
epoll：
    底层系统调用 / IO 多路复用机制

Reactor：
    基于 epoll 构建的事件驱动架构模型
```

一句话：

```text
epoll 负责告诉程序哪些 fd 就绪；
Reactor 负责把这些就绪事件封装、分发，并调用对应处理逻辑。
```

---

# 5. 从 epoll echo server 到 Reactor 的映射

| 原始 epoll 代码         | Reactor 组件                                         |
| ------------------- | -------------------------------------------------- |
| `epfd`              | `EventLoop` 内部的 epoll fd                           |
| `epoll_wait` 循环     | `EventLoop::loop()`                                |
| `epoll_event`       | `Channel` 关注的事件                                    |
| `fd == listenfd` 判断 | `Acceptor` / listen Channel                        |
| `handleAccept()`    | `Acceptor::handleRead()`                           |
| `handleRead()`      | `Connection::handleRead()`                         |
| `connfd`            | `Connection` 管理的 fd                                |
| `char buf[1024]`    | `Buffer` 雏形                                        |
| `write(fd, buf, n)` | `Connection::send()` / `Connection::handleWrite()` |

核心：

```text
不是重新学一个陌生模型，而是把之前写过的 epoll server 拆成对象和职责。
```

---

# 6. Reactor 最小核心组件

Day 14 中重点掌握：

```text
1. EventLoop
2. Channel
3. Acceptor
4. Connection
5. Buffer
6. ThreadPool
```

组件职责表：

| 组件           | 核心职责                                   |
| ------------ | -------------------------------------- |
| `EventLoop`  | epoll_wait、epoll_ctl、事件循环、分发活跃 Channel |
| `Channel`    | fd + events + revents + callbacks      |
| `Acceptor`   | 管理 listenfd，accept 新连接                 |
| `Connection` | 管理 connfd、读写、关闭、连接状态                   |
| `Buffer`     | 处理 TCP 字节流、部分写、粘包拆包                    |
| `ThreadPool` | 处理耗时业务，生成 response                     |

---

# 7. EventLoop

## 7.1 EventLoop 是什么？

EventLoop 是 Reactor 的核心事件循环。

它负责：

```text
1. 持有 epoll fd
2. 调用 epoll_wait 等待事件
3. 通过 epoll_ctl 添加 / 修改 / 删除 Channel
4. 获取活跃 Channel
5. 分发事件给 Channel
```

一句话：

```text
EventLoop = epoll 的封装 + 事件循环 + 事件分发器
```

---

## 7.2 EventLoop 的核心接口

```cpp
class EventLoop {
 public:
  EventLoop();
  ~EventLoop();

  void loop();
  void updateChannel(Channel* channel);
  void removeChannel(Channel* channel);

 private:
  int epfd_;
};
```

---

## 7.3 EventLoop::loop 的核心逻辑

原始写法：

```cpp
int fd = events[i].data.fd;

if (fd == listenfd) {
  handleAccept(...);
} else {
  handleRead(...);
}
```

Reactor 中写法：

```cpp
Channel* channel = static_cast<Channel*>(events[i].data.ptr);
channel->setRevents(events[i].events);
channel->handleEvent();
```

核心变化：

```text
EventLoop 不再关心 fd 是 listenfd 还是 connfd；
EventLoop 只关心活跃 Channel，并调用 Channel::handleEvent。
```

---

# 8. Channel

## 8.1 Channel 是什么？

Channel 是对一个 fd 及其事件的封装。

它保存：

```text
1. fd
2. events_：关注的事件
3. revents_：实际发生的事件
4. readCallback_
5. writeCallback_
6. closeCallback_
7. errorCallback_，可选
```

一句话：

```text
Channel = fd + events + revents + callbacks
```

---

## 8.2 Channel 最小骨架

```cpp
#include <sys/epoll.h>

#include <functional>
#include <utility>

class Channel {
 public:
  explicit Channel(int fd) : fd_(fd) {}

  int fd() const { return fd_; }

  uint32_t events() const { return events_; }
  void setRevents(uint32_t revents) { revents_ = revents; }

  void enableReading() { events_ |= EPOLLIN; }
  void enableWriting() { events_ |= EPOLLOUT; }
  void disableWriting() { events_ &= ~EPOLLOUT; }
  void disableAll() { events_ = 0; }

  void setReadCallback(std::function<void()> cb) {
    readCallback_ = std::move(cb);
  }

  void setWriteCallback(std::function<void()> cb) {
    writeCallback_ = std::move(cb);
  }

  void setCloseCallback(std::function<void()> cb) {
    closeCallback_ = std::move(cb);
  }

  void handleEvent() {
    if ((revents_ & EPOLLIN) && readCallback_) {
      readCallback_();
    }

    if ((revents_ & EPOLLOUT) && writeCallback_) {
      writeCallback_();
    }

    if ((revents_ & (EPOLLHUP | EPOLLERR)) && closeCallback_) {
      closeCallback_();
    }
  }

 private:
  int fd_;
  uint32_t events_ = 0;
  uint32_t revents_ = 0;

  std::function<void()> readCallback_;
  std::function<void()> writeCallback_;
  std::function<void()> closeCallback_;
};
```

---

## 8.3 events_ 和 revents_ 的区别

```text
events_：
    当前 Channel 希望 epoll 关注的事件。
    例如 EPOLLIN、EPOLLIN | EPOLLOUT。

revents_：
    本次 epoll_wait 实际返回的事件。
    例如某次实际发生了 EPOLLIN 或 EPOLLOUT。
```

压缩记忆：

```text
events_ = 我关心什么
revents_ = 实际发生了什么
```

---

## 8.4 为什么 Channel 要保存 callback？

Channel 保存 callback 是为了把 fd 事件和具体处理逻辑绑定起来。

例如：

```text
listen Channel 的 readCallback：
    Acceptor::handleRead

conn Channel 的 readCallback：
    Connection::handleRead

conn Channel 的 writeCallback：
    Connection::handleWrite

conn Channel 的 closeCallback：
    Connection::handleClose
```

这样 EventLoop 不需要知道 fd 是 listenfd 还是 connfd。

EventLoop 只需要：

```cpp
channel->handleEvent();
```

Channel 根据 `revents_` 调用对应回调。

---

# 9. 为什么用 ev.data.ptr = channel？

Day 11 的写法：

```cpp
ev.data.fd = connfd;
```

epoll_wait 返回后：

```cpp
int fd = events[i].data.fd;
```

这种方式只能拿到 fd。

Reactor 中更常用：

```cpp
ev.data.ptr = channel;
```

epoll_wait 返回后：

```cpp
Channel* channel = static_cast<Channel*>(events[i].data.ptr);
```

好处：

```text
1. EventLoop 可以直接拿到 Channel 对象
2. EventLoop 不需要判断 fd 类型
3. 消除 if (fd == listenfd) 这种分支
4. 事件处理逻辑通过 Channel callback 分发
```

一句话：

```text
从 fd 判断，升级成对象回调分发。
```

---

# 10. Channel 和 EventLoop 的关系

Channel 知道：

```text
自己管理哪个 fd
自己关注哪些事件
自己事件触发后调用哪些 callback
```

EventLoop 知道：

```text
epfd
如何调用 epoll_ctl
如何调用 epoll_wait
如何分发活跃 Channel
```

因此：

```text
Channel 修改关注事件；
EventLoop 修改内核 epoll 监听关系。
```

典型链路：

```text
Channel::enableReading()
    ↓
events_ 加上 EPOLLIN
    ↓
Channel::update()
    ↓
EventLoop::updateChannel(this)
    ↓
epoll_ctl ADD / MOD
```

当事件发生：

```text
epoll_wait 返回事件
    ↓
EventLoop 拿到 Channel*
    ↓
channel->setRevents(...)
    ↓
channel->handleEvent()
    ↓
调用 readCallback_ / writeCallback_ / closeCallback_
```

---

# 11. addedToLoop_ 的作用

在 EventLoop::updateChannel 中，需要判断：

```text
这个 Channel 是第一次注册到 epoll？
还是已经注册过，只是修改关注事件？
```

因此可以在 Channel 中维护：

```cpp
bool addedToLoop_ = false;
```

作用：

```text
addedToLoop_ 用来区分 epoll_ctl ADD 还是 MOD。
```

逻辑：

```cpp
if (!channel->addedToLoop()) {
    epoll_ctl(epfd_, EPOLL_CTL_ADD, channel->fd(), &ev);
    channel->setAddedToLoop(true);
} else {
    epoll_ctl(epfd_, EPOLL_CTL_MOD, channel->fd(), &ev);
}
```

---

# 12. Acceptor

## 12.1 Acceptor 是什么？

Acceptor 专门负责监听 socket，也就是 listenfd 的管理。

它负责：

```text
1. 创建 listenfd
2. 设置 listenfd 非阻塞
3. bind
4. listen
5. 创建 listen Channel
6. listenfd 可读时 accept 新连接
7. accept 出 connfd 后通知上层 Server
```

一句话：

```text
Acceptor 负责接收新连接，不负责处理客户端数据。
```

对应之前的说法：

```text
listenfd 负责接客，connfd 负责聊天。
```

---

## 12.2 Acceptor 的事件链路

```text
listenfd EPOLLIN
    ↓
epoll_wait 返回 listen Channel
    ↓
EventLoop 调用 channel->handleEvent()
    ↓
listen Channel 调用 readCallback_
    ↓
Acceptor::handleRead()
    ↓
accept 新 connfd
    ↓
通知 TcpServer 创建 Connection
```

---

## 12.3 Acceptor 和 Connection 的边界

Acceptor accept 出 connfd 后，通常不直接长期管理这个 connfd。

更准确的链路：

```text
Acceptor accept 出 connfd
    ↓
通过 newConnectionCallback 通知 TcpServer
    ↓
TcpServer 创建 Connection
    ↓
Connection 管理 connfd 的后续生命周期
```

所以：

```text
Acceptor 负责新连接接收；
Connection 负责已建立连接的读写和关闭。
```

---

# 13. Connection

## 13.1 Connection 是什么？

Connection 表示一个客户端连接。

它负责管理：

```text
1. connfd
2. conn Channel
3. inputBuffer
4. outputBuffer
5. 连接状态
6. handleRead
7. handleWrite
8. handleClose
9. send / sendInLoop
```

一句话：

```text
Connection 负责一个客户端连接的生命周期和 IO 处理。
```

---

## 13.2 Connection 为什么需要 Channel？

Connection 持有 Channel，是因为这个连接的所有 IO 事件都需要通过 Channel 注册到 EventLoop，并在事件触发时回调到 Connection 的处理函数。

关系：

```text
conn Channel readCallback  → Connection::handleRead
conn Channel writeCallback → Connection::handleWrite
conn Channel closeCallback → Connection::handleClose
```

Connection 管理连接语义，Channel 管理这个连接在 EventLoop 中的事件代理。

---

## 13.3 Connection 的读事件链路

```text
connfd EPOLLIN
    ↓
epoll_wait 返回 conn Channel
    ↓
EventLoop 调用 Channel::handleEvent()
    ↓
Channel 调用 readCallback_
    ↓
Connection::handleRead()
    ↓
read 数据到 inputBuffer
    ↓
协议解析
    ↓
得到完整 request
    ↓
提交线程池或直接处理
```

---

## 13.4 Connection 的写事件链路

```text
outputBuffer 非空
    ↓
channel_->enableWriting()
    ↓
epoll_ctl MOD，加 EPOLLOUT
    ↓
EPOLLOUT 触发
    ↓
Channel 调用 writeCallback_
    ↓
Connection::handleWrite()
    ↓
继续写 outputBuffer
    ↓
写完后 channel_->disableWriting()
```

---

## 13.5 Connection 的关闭链路

关闭触发来源：

```text
1. read 返回 0，对端正常关闭
2. read/write 返回严重错误
3. EPOLLHUP / EPOLLERR
4. 应用层主动关闭
```

关闭处理：

```text
Connection::handleClose()
    ↓
更新连接状态
    ↓
EventLoop::removeChannel()
    ↓
close(connfd)
    ↓
TcpServer 从连接管理表中移除 Connection
```

目标：

```text
保证 fd、Channel、Connection 生命周期一致。
```

---

# 14. Buffer

## 14.1 为什么需要 Buffer？

TCP 是字节流协议：

```text
一次 read 不等于一个完整请求
一次 write 不一定写完全部响应
```

因此 Connection 需要用户态 Buffer。

---

## 14.2 inputBuffer

作用：

```text
保存从 socket 读到但业务还没处理完的数据。
```

用于处理：

```text
1. TCP 字节流
2. 粘包
3. 拆包
4. 半包
5. 协议解析
```

读事件中：

```text
Connection::handleRead()
    ↓
read 数据到 inputBuffer
    ↓
从 inputBuffer 解析完整 request
```

---

## 14.3 outputBuffer

作用：

```text
保存业务想发送但暂时没有成功写入 socket 内核发送缓冲区的数据。
```

用于处理：

```text
1. 非阻塞 write 部分写
2. write 返回 EAGAIN
3. EPOLLOUT 续写
4. 保证同一连接上的发送顺序
```

当数据没写完：

```text
剩余数据放入 outputBuffer
    ↓
关注 EPOLLOUT
```

当 EPOLLOUT 触发：

```text
Connection::handleWrite()
    ↓
继续发送 outputBuffer
    ↓
写完后取消 EPOLLOUT
```

---

# 15. 为什么不能一直关注 EPOLLOUT？

socket 大多数时候都是可写的。

如果一直关注 EPOLLOUT，即使 outputBuffer 中没有待发送数据，epoll_wait 也可能频繁返回可写事件。

这会导致：

```text
无意义唤醒
busy loop
CPU 空转
```

因此：

```text
EPOLLIN 通常长期关注
EPOLLOUT 通常动态关注
```

EPOLLOUT 的正确策略：

```text
outputBuffer 为空：
    不关注 EPOLLOUT

outputBuffer 非空：
    关注 EPOLLOUT

outputBuffer 写空：
    取消 EPOLLOUT
```

---

# 16. Reactor 和线程池的关系

## 16.1 职责分工

```text
Reactor / EventLoop：
    负责 IO 事件
    负责 epoll_wait
    负责 accept
    负责 read/write
    负责连接状态
    负责 Buffer 管理

ThreadPool：
    负责耗时业务处理
    例如计算、查询、文件处理、复杂业务逻辑
```

一句话：

```text
Reactor 管 IO，线程池管耗时业务。
```

---

## 16.2 为什么不能在 EventLoop 中执行耗时业务？

如果在 EventLoop 中执行耗时业务：

```cpp
void Connection::handleRead() {
  read(fd, ...);

  doHeavyBusiness();  // 假设耗时 500ms
}
```

这 500ms 内：

```text
不能继续 epoll_wait
不能 accept 新连接
不能处理其他连接读写
不能及时处理关闭和错误
```

这会破坏 Reactor 的事件驱动模型。

因此：

```text
EventLoop 线程只做短平快的 IO 处理；
耗时业务交给线程池。
```

---

# 17. 线程池中的任务是什么？

线程池里放的不是 fd，也不是 epoll 事件，而是：

```text
已经解析出来的业务请求处理任务。
```

典型任务：

```text
process(request) → response
```

流程：

```text
Connection::handleRead()
    ↓
read 数据到 inputBuffer
    ↓
解析出完整 request
    ↓
threadPool.submit(process(request))
```

线程池任务一般包括：

```text
1. 处理 request
2. 执行业务逻辑
3. 生成 response
4. 将发送 response 的任务投递回 EventLoop
```

注意：

```text
worker 线程通常不直接 write fd。
```

---

# 18. 为什么 worker 线程不直接 write(fd, response)？

原因：

```text
1. fd 生命周期由 Connection 管理，worker 线程直接使用 fd 可能遇到 fd 已关闭或复用问题
2. 多个 worker 线程可能同时 write 同一个 fd，导致响应顺序混乱
3. 非阻塞 write 可能部分写或 EAGAIN，需要 outputBuffer 和 EPOLLOUT 机制配合
4. Channel 的 enableWriting / disableWriting 涉及 epoll_ctl MOD，应该由所属 EventLoop 线程统一执行
5. Connection 状态、Buffer、Channel 事件修改通常归属 EventLoop 线程管理
```

正确做法：

```text
worker 线程只生成 response；
真正的发送动作回到 EventLoop 线程，由 Connection::sendInLoop 处理。
```

---

# 19. EventLoop pendingTasks 和 eventfd

## 19.1 为什么需要 pendingTasks？

worker 线程处理完业务后，需要让 EventLoop 线程执行：

```text
conn->sendInLoop(response)
```

但 EventLoop 可能正在：

```cpp
epoll_wait(epfd_, events, MAX_EVENTS, -1);
```

所以 worker 需要把一个任务放入 EventLoop 的任务队列。

这个队列可以叫：

```text
pendingTasks
pendingFunctors
taskQueue
```

任务本质：

```text
在 EventLoop 线程中执行 conn->sendInLoop(response)
```

---

## 19.2 为什么需要 eventfd 唤醒？

如果 worker 只是把任务放入 pendingTasks，EventLoop 可能还在 epoll_wait 中睡眠。

所以需要唤醒 EventLoop。

通常使用：

```text
eventfd
```

流程：

```text
worker 线程写 eventfd
    ↓
eventfd 变为可读
    ↓
epoll_wait 被唤醒
    ↓
EventLoop 处理 wakeup 事件
    ↓
EventLoop 执行 pendingTasks
```

eventfd 的作用：

```text
跨线程唤醒 EventLoop。
```

---

# 20. 请求链路

完整请求链路：

```text
客户端发送请求
    ↓
数据进入服务端内核协议栈
    ↓
connfd 变为可读
    ↓
epoll_wait 返回 conn Channel
    ↓
EventLoop 设置 revents 并调用 Channel::handleEvent
    ↓
Channel 根据 EPOLLIN 调用 readCallback
    ↓
Connection::handleRead
    ↓
read 数据到 inputBuffer
    ↓
协议解析，处理粘包 / 拆包
    ↓
得到完整 request
    ↓
提交到 ThreadPool
```

压缩版：

```text
EPOLLIN → Connection::handleRead → inputBuffer → request → ThreadPool
```

---

# 21. 回复链路

线程池处理完业务后：

```text
ThreadPool 处理 request
    ↓
生成 response
    ↓
worker 不直接 write
    ↓
worker 把发送任务投递回 EventLoop
    ↓
eventfd 唤醒 EventLoop
    ↓
EventLoop 执行 pending task
    ↓
Connection::sendInLoop(response)
    ↓
尝试 write
    ↓
如果写完：
        结束
    ↓
如果没写完 / EAGAIN：
        剩余数据进入 outputBuffer
        Channel enableWriting，关注 EPOLLOUT
    ↓
后续 EPOLLOUT 触发
    ↓
Connection::handleWrite
    ↓
继续写 outputBuffer
    ↓
写完后 disableWriting，取消 EPOLLOUT
```

压缩版：

```text
ThreadPool → response → queueInLoop(conn->sendInLoop(response)) → EventLoop → Connection → write/outputBuffer/EPOLLOUT
```

---

# 22. 回复链路的关键理解

线程池处理完业务后，不是简单地把 response 交给 EventLoop 再让 EventLoop 找连接。

更准确是：

```text
worker 已经知道对应 Connection
    ↓
worker 构造一个 task：conn->sendInLoop(response)
    ↓
把这个 task 投递给 conn 所属的 EventLoop
    ↓
EventLoop 只负责在自己的线程里执行这个 task
```

也就是说：

```text
EventLoop 不主动理解 response 属于哪个连接；
任务里已经绑定了 Connection。
```

---

# 23. send / sendInLoop 的区别

## 23.1 send

`send()` 是线程安全入口。

它会判断当前线程是不是 Connection 所属 EventLoop 线程。

```text
如果当前就在 EventLoop 线程：
    直接调用 sendInLoop

如果当前在 worker 线程：
    把 sendInLoop 封装成任务投递到 EventLoop
```

---

## 23.2 sendInLoop

`sendInLoop()` 是真正执行发送逻辑的函数，必须在 EventLoop 线程中执行。

它负责：

```text
1. 尝试直接 write
2. 处理部分写
3. 处理 EAGAIN
4. 把剩余数据放入 outputBuffer
5. 必要时 enableWriting，关注 EPOLLOUT
```

---

# 24. sendInLoop 的核心逻辑

```text
如果 outputBuffer 为空：
    尝试直接 write response

    如果一次写完：
        结束

    如果只写一部分：
        剩余数据放入 outputBuffer
        enableWriting

    如果 write 返回 EAGAIN：
        response 放入 outputBuffer
        enableWriting

如果 outputBuffer 非空：
    说明前面还有数据没写完
    新 response 追加到 outputBuffer 后面
    保持 EPOLLOUT 关注
```

注意：

```text
如果 outputBuffer 已经有数据，新的 response 不能绕过 outputBuffer 直接 write。
```

原因：

```text
需要保证同一连接上的发送顺序。
```

---

# 25. handleWrite 的核心逻辑

当 EPOLLOUT 触发：

```text
Connection::handleWrite()
    ↓
从 outputBuffer 中继续 write
    ↓
如果写了一部分：
        从 outputBuffer 中移除已写部分
    ↓
如果 outputBuffer 为空：
        disableWriting，取消 EPOLLOUT
    ↓
如果 write 返回 EAGAIN：
        保留 outputBuffer，继续关注 EPOLLOUT
    ↓
如果 write 发生严重错误：
        handleClose
```

---

# 26. 主从 Reactor 与线程池

主从 Reactor 和线程池不是一回事。

## 26.1 Main Reactor

负责：

```text
listenfd
accept 新连接
把 connfd 分发给某个 SubReactor
```

## 26.2 Sub Reactor

负责：

```text
connfd
read/write
Connection 状态
inputBuffer/outputBuffer
```

## 26.3 ThreadPool

负责：

```text
业务处理
process(request) → response
```

常见结构：

```text
1 个 MainReactor
N 个 SubReactor
M 个 Worker 业务线程
```

压缩理解：

```text
Reactor 线程是 IO 线程；
ThreadPool 是业务线程。
```

---

# 27. 面试表达：Reactor 模型怎么设计？

可以这样回答：

```text
我把 epoll 事件循环拆成几个核心组件。

EventLoop 负责持有 epoll fd，并在 loop 中调用 epoll_wait 获取就绪事件，然后把事件分发给对应的 Channel。Channel 是对 fd 事件的封装，里面保存 fd、关注的事件、实际发生的事件，以及读写关闭等回调。epoll_wait 返回后，EventLoop 通过 Channel 调用对应回调。

Acceptor 封装 listenfd，负责 socket、bind、listen 以及 accept 新连接。当 listenfd 可读时，Acceptor accept 出 connfd，并交给 TcpServer 创建 Connection。

Connection 表示一个客户端连接，内部持有 connfd 对应的 Channel，以及 inputBuffer、outputBuffer 和连接状态。读事件触发时读取数据到 inputBuffer，写事件触发时从 outputBuffer 继续发送数据。
```

---

# 28. 面试表达：线程池和 Reactor 如何配合？

可以这样回答：

```text
在 Reactor 模型中，我会把 IO 和业务处理拆开。EventLoop 线程负责 epoll_wait、accept、read、write 和连接状态管理，要求这些操作尽量短平快，不能被耗时业务阻塞。

当 Connection::handleRead 读到数据后，会先写入 inputBuffer，并在 inputBuffer 中做协议解析，解决 TCP 字节流下的粘包、拆包问题。当解析出一个完整 request 后，如果业务处理比较耗时，就把 request 封装成任务提交到线程池。

线程池只负责业务处理，比如根据 request 计算或查询，生成 response。worker 线程通常不直接 write connfd，因为 fd 生命周期、outputBuffer、EPOLLOUT 关注和连接状态都由 Connection 所属的 EventLoop 管理。worker 处理完后，会把一个发送任务投递回这个 Connection 所属的 EventLoop，比如让 EventLoop 执行 conn->sendInLoop(response)。

EventLoop 可能正阻塞在 epoll_wait，所以跨线程投递任务时通常会通过 eventfd 唤醒 EventLoop。EventLoop 被唤醒后执行 pendingTasks，在自己的线程中调用 Connection::sendInLoop。sendInLoop 会尝试直接写 socket；如果写完就结束；如果发生部分写或 EAGAIN，就把剩余数据放入 outputBuffer，并让 Channel 关注 EPOLLOUT。后续 fd 可写时，Connection::handleWrite 会继续发送 outputBuffer，写完后取消 EPOLLOUT。
```

---

# 29. Day 14 面试问答

## Q1：什么是 Reactor 模型？

答：

Reactor 是一种事件驱动的网络 IO 模型。它通过 IO 多路复用机制监听多个 fd，当 fd 事件就绪后，由事件循环分发给对应的处理对象或回调函数。在 Linux 下通常基于 epoll 实现。

---

## Q2：Reactor 和 epoll 是什么关系？

答：

epoll 是 Linux 提供的 IO 多路复用系统调用，负责等待和返回就绪 fd。Reactor 是基于 epoll 封装出来的事件驱动架构模型，负责事件分发、回调处理和连接管理。

---

## Q3：EventLoop 的职责是什么？

答：

EventLoop 是事件循环和事件分发器。它持有 epoll fd，负责 epoll_wait、epoll_ctl，管理 Channel 的注册、修改和删除，并在事件就绪后分发给对应 Channel。

---

## Q4：Channel 的职责是什么？

答：

Channel 是对 fd 及其事件的封装，保存 fd、关注事件 events_、实际发生事件 revents_，以及读写关闭等回调。EventLoop 分发事件后，Channel 根据 revents_ 调用对应 callback。

---

## Q5：为什么使用 ev.data.ptr = channel？

答：

这样 epoll_wait 返回时可以直接拿到 Channel 对象，而不是只拿到 fd。EventLoop 不需要判断 fd 是 listenfd 还是 connfd，只需要调用 channel->handleEvent，具体逻辑由 Channel 上绑定的 callback 决定。

---

## Q6：Acceptor 的职责是什么？

答：

Acceptor 负责 listenfd，包括 socket、bind、listen、设置非阻塞，以及在 listenfd 可读时 accept 新连接。accept 出 connfd 后，通常通知 TcpServer 创建 Connection。

---

## Q7：Connection 的职责是什么？

答：

Connection 表示一个客户端连接，负责 connfd 的生命周期和 IO 处理。它通常持有 connfd 对应的 Channel、inputBuffer、outputBuffer 和连接状态，负责 handleRead、handleWrite、handleClose 以及 send/sendInLoop。

---

## Q8：为什么需要 inputBuffer？

答：

因为 TCP 是字节流协议，一次 read 不一定对应一个完整请求。inputBuffer 用来保存已经读取但业务还没处理完的数据，用于协议解析和处理粘包、拆包、半包问题。

---

## Q9：为什么需要 outputBuffer？

答：

因为非阻塞 write 可能只写出部分数据，也可能返回 EAGAIN。未写完的数据不能丢，需要保存到 outputBuffer。后续 fd 可写时，通过 EPOLLOUT 触发 handleWrite 继续发送。

---

## Q10：为什么不能一直关注 EPOLLOUT？

答：

socket 大多数时候都是可写的。如果一直关注 EPOLLOUT，即使 outputBuffer 中没有待发送数据，也会造成无意义唤醒甚至 busy loop。因此 EPOLLOUT 应该按需关注：outputBuffer 非空时关注，写空后取消。

---

## Q11：线程池和 Reactor 如何配合？

答：

Reactor 负责 IO 事件，线程池负责耗时业务。Connection 从 inputBuffer 解析出完整 request 后，如果业务处理耗时，就提交到线程池。worker 生成 response 后，不直接 write fd，而是把发送任务投递回对应 EventLoop，由 EventLoop 在线程内调用 Connection::sendInLoop，通过 outputBuffer 和 EPOLLOUT 完成发送。

---

## Q12：为什么 worker 线程不直接 write fd？

答：

因为 fd 生命周期、Connection 状态、outputBuffer、Channel 事件修改都归属 EventLoop 管理。worker 直接 write 可能造成 fd 已关闭、fd 复用、多个线程并发写同一个 fd、响应顺序混乱，以及无法统一处理部分写和 EAGAIN。因此 worker 应该把发送任务投递回 EventLoop。

---

# 30. Day 14 易错点总结

```text
1. Reactor 不是 epoll 本身，而是基于 epoll 的事件驱动模型。
2. EventLoop 负责事件等待和分发，不直接处理业务。
3. Channel 封装 fd、events、revents、callback。
4. events_ 是关注的事件，revents_ 是实际发生的事件。
5. ev.data.ptr = channel 可以避免 EventLoop 判断 fd 类型。
6. Acceptor 负责 listenfd 和 accept，不负责客户端读写。
7. Connection 负责 connfd、Buffer、读写关闭和连接状态。
8. inputBuffer 处理 TCP 字节流、粘包拆包。
9. outputBuffer 处理部分写和 write EAGAIN。
10. EPOLLIN 通常长期关注。
11. EPOLLOUT 通常按需关注。
12. 线程池处理业务，不替代 Reactor。
13. worker 线程通常不直接 write fd。
14. worker 生成 response 后，应把发送任务投递回 EventLoop。
15. EventLoop 可能需要 eventfd 跨线程唤醒。
16. send 是线程安全入口，sendInLoop 是 EventLoop 线程内真正发送。
17. MainReactor / SubReactor 是 IO 线程模型，ThreadPool 是业务线程模型，不要混淆。
```

---

# 31. Day 14 总结

Day 14 的核心收获：

```text
Reactor：
    基于 epoll 的事件驱动模型。

EventLoop：
    事件循环和事件分发器。

Channel：
    fd 事件代理对象。

Acceptor：
    listenfd 和 accept 新连接。

Connection：
    connfd、连接状态、读写和 Buffer。

Buffer：
    inputBuffer 处理请求解析；
    outputBuffer 处理未发送完的数据。

ThreadPool：
    处理耗时业务，生成 response。

请求链路：
    EPOLLIN → Connection::handleRead → inputBuffer → request → ThreadPool

回复链路：
    ThreadPool → response → queueInLoop(conn->sendInLoop(response)) → EventLoop → Connection → write/outputBuffer/EPOLLOUT
```

最重要的一句话：

> Reactor 管 IO，线程池管业务；worker 线程不直接 write fd，而是把发送任务投递回对应 EventLoop，由 Connection 在 IO 线程内通过 outputBuffer 和 EPOLLOUT 完成发送。

````

---

# Day 1～Day 14 面试准备阶段收束

这 14 天已经覆盖了：

```text
Day 1～Day 9：
    C++ 线程基础、mutex、condition_variable、线程池、future / packaged_task

Day 10：
    socket / bind / listen / accept / read / write / 阻塞 IO

Day 11：
    IO 多路复用与 epoll 基础

Day 12：
    非阻塞 epoll echo server，accept/read 到 EAGAIN

Day 13：
    LT / ET 触发模式，EPOLLIN / EPOLLOUT，动态关注写事件

Day 14：
    Reactor 模型，EventLoop / Channel / Acceptor / Connection / Buffer / ThreadPool
````

当前阶段你已经具备一套比较完整的 **Linux C++ 网络开发面试主线**：

```text
线程基础
    ↓
线程池
    ↓
socket 编程
    ↓
阻塞 IO 的问题
    ↓
epoll
    ↓
非阻塞 IO
    ↓
LT / ET
    ↓
Reactor
    ↓
线程池与 Reactor 的配合
```
