# Day 13｜epoll LT / ET 触发模式：为什么 ET 必须读到 EAGAIN

# 1. 今日学习目标

Day 13 重点学习 epoll 的两种触发模式：

```text
LT：Level Trigger，水平触发
ET：Edge Trigger，边缘触发
````

今日目标：

* 理解 LT / ET 的核心区别
* 理解 LT 为什么“没处理完还会继续通知”
* 理解 ET 为什么“没处理完可能不会再次通知”
* 理解为什么 ET 必须配合非阻塞 fd
* 理解为什么 ET 下 accept / read / write 都要处理到 EAGAIN 或完成
* 理解 EPOLLIN / EPOLLOUT 和 LT / ET 是两个不同维度
* 明确 MiniNet 初期为什么建议先用 LT
* 明确 MiniNet 后续如何为 ET 做准备

核心主线：

> LT 看状态，ET 看变化。
> LT 下 fd 只要仍然 ready，就会继续通知；ET 下 fd 只有状态变化到 ready 时才通知。

---

# 2. LT 和 ET 的一句话区别

## 2.1 LT：水平触发

LT 的核心是：

```text
只要 fd 仍然处于就绪状态，epoll_wait 就会持续返回这个 fd。
```

例如 connfd 的接收缓冲区还有数据：

```text
这次没读完
    ↓
下次 epoll_wait 还会继续返回这个 connfd
```

所以 LT 更宽容。

一句话：

```text
LT 看“现在是不是 ready”。
```

---

## 2.2 ET：边缘触发

ET 的核心是：

```text
只有 fd 的就绪状态发生变化时，epoll_wait 才通知。
```

例如 connfd 从：

```text
不可读
    ↓
可读
```

这个状态变化会触发一次 `EPOLLIN`。

但如果这次没有读完，缓冲区仍然有数据：

```text
可读
    ↓
仍然可读
```

状态没有发生新的边缘变化，就不能依赖 epoll_wait 再次返回这个 fd。

一句话：

```text
ET 看“刚刚是不是变成 ready”。
```

---

# 3. LT / ET 的直观类比

## 3.1 LT 像“闹钟一直响”

只要缓冲区里还有数据：

```text
epoll_wait：你有数据没读
epoll_wait：你有数据没读
epoll_wait：你有数据没读
```

直到数据被读完。

所以 LT 是：

```text
状态仍然满足，就持续提醒。
```

---

## 3.2 ET 像“门铃只按一次”

数据从没有变成有：

```text
叮咚！有新数据了。
```

如果这次只读了一部分，剩余数据还在内核缓冲区里，但状态仍然是“有数据”，门铃不会一直响。

所以 ET 是：

```text
状态变化时提醒一次。
```

---

# 4. LT 模式行为

默认 epoll 是 LT 模式。

注册方式：

```cpp
epoll_event ev{};
ev.events = EPOLLIN;
ev.data.fd = connfd;

epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev);
```

只要没有加：

```cpp
EPOLLET
```

就是 LT。

---

## 4.1 LT 下 connfd 可读

假设：

```text
connfd 接收缓冲区有 100 字节
用户 buf 只有 10 字节
一次 read 只读走 10 字节
剩余 90 字节
```

LT 下：

```text
第一次 epoll_wait 返回 connfd
read 10 字节，剩 90 字节

第二次 epoll_wait 仍然返回 connfd
read 10 字节，剩 80 字节

第三次 epoll_wait 仍然返回 connfd
...
```

原因：

```text
只要接收缓冲区还有数据，fd 就仍然处于可读状态。
```

---

## 4.2 LT 下 listenfd 可读

假设：

```text
listenfd 的 accept 队列里有 5 个连接
一次 accept 只取出 1 个
剩余 4 个连接
```

LT 下：

```text
下次 epoll_wait 仍然会返回 listenfd
```

原因：

```text
只要 accept 队列仍然非空，listenfd 就仍然处于可读状态。
```

---

## 4.3 LT 的优点

```text
1. 编程简单
2. 容错性高
3. 没有一次读完 / accept 完，也不容易导致连接卡住
4. 更适合初学和项目初期
5. 适合先把 EventLoop / Channel / Connection / Buffer 主线写稳
```

---

## 4.4 LT 的缺点

如果每次只处理一点点，epoll 会反复提醒：

```text
fd 还可读
fd 还可读
fd 还可读
```

这会带来：

```text
1. 更多 epoll_wait 返回
2. 更多用户态 / 内核态切换
3. 更多事件处理次数
```

所以即使使用 LT，工程上也通常尽量：

```text
循环 accept 到 EAGAIN
循环 read 到 EAGAIN
```

以减少重复通知。

---

# 5. ET 模式行为

ET 注册方式：

```cpp
epoll_event ev{};
ev.events = EPOLLIN | EPOLLET;
ev.data.fd = connfd;

epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev);
```

其中：

```cpp
EPOLLET
```

表示使用边缘触发。

---

## 5.1 ET 下 connfd 可读

假设：

```text
connfd 接收缓冲区有 100 字节
用户 buf 只有 10 字节
一次 read 只读走 10 字节
剩余 90 字节
```

第一次数据到达时：

```text
不可读 → 可读
```

触发一次 `EPOLLIN`。

如果这次只读了 10 字节：

```text
可读 → 仍然可读
```

没有新的边缘变化。

结果：

```text
epoll_wait 不一定会再次返回这个 connfd。
```

所以剩余 90 字节可能一直留在内核接收缓冲区里，直到有新的状态变化，比如新数据到达、连接关闭或错误事件。

---

## 5.2 ET 下 listenfd 可读

假设：

```text
accept 队列里有 5 个连接
一次 accept 只取出 1 个
剩余 4 个连接
```

listenfd 的状态是：

```text
有连接 → 仍然有连接
```

没有新的边缘变化。

结果：

```text
epoll_wait 不一定会再次返回 listenfd。
```

所以 ET 下 listenfd 必须循环 accept 到 EAGAIN。

---

# 6. 为什么 ET 必须配合非阻塞 fd？

ET 下的典型处理方式是：

```text
一次事件来了，就尽量处理干净。
```

对于 read：

```text
循环 read，直到 EAGAIN。
```

如果 fd 是阻塞模式，那么：

```text
读完当前已有数据后，下一次 read 会阻塞等待新数据。
```

这会直接卡住整个 EventLoop。

所以 ET 必须满足：

```text
1. fd 非阻塞
2. 循环处理
3. 遇到 EAGAIN 才停
```

可以压缩记忆为：

```text
ET = 非阻塞 + 循环处理到 EAGAIN
```

---

# 7. EAGAIN 在 ET 中的意义

对于非阻塞 fd：

```text
EAGAIN / EWOULDBLOCK 不是严重错误。
```

它表示：

```text
当前这件事暂时做不了了。
```

具体到不同操作：

## 7.1 accept 返回 EAGAIN

表示：

```text
当前 accept 队列已经取空，没有更多已完成连接可以 accept。
```

## 7.2 read 返回 EAGAIN

表示：

```text
当前接收缓冲区已经读空，没有更多数据可读。
```

## 7.3 write 返回 EAGAIN

表示：

```text
当前发送缓冲区暂时没有空间，不能继续写。
```

所以 ET 下的停止条件通常是：

```text
accept 到 EAGAIN
read 到 EAGAIN
write 到 EAGAIN 或 outputBuffer 写空
```

---

# 8. ET 下处理读事件

ET 下 read 的正确模型：

```cpp
void handleReadET(int epfd, int fd) {
  char buf[1024];

  while (true) {
    ssize_t n = read(fd, buf, sizeof(buf));

    if (n > 0) {
      // 处理读到的数据，例如 append 到 inputBuffer
    } else if (n == 0) {
      // 对端关闭
      epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
      close(fd);
      return;
    } else {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // 当前接收缓冲区已经读空
        break;
      } else {
        perror("read");
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
        return;
      }
    }
  }
}
```

核心：

```text
ET 下必须 read 到 EAGAIN。
```

---

# 9. ET 下处理 accept 事件

ET 下 listenfd 的正确模型：

```cpp
void handleAcceptET(int epfd, int listenfd) {
  while (true) {
    int connfd = accept(listenfd, nullptr, nullptr);

    if (connfd >= 0) {
      setNonBlock(connfd);

      epoll_event ev{};
      ev.events = EPOLLIN | EPOLLET;
      ev.data.fd = connfd;

      epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev);
    } else {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // 当前 accept 队列已经取空
        break;
      } else {
        perror("accept");
        break;
      }
    }
  }
}
```

核心：

```text
ET 下必须 accept 到 EAGAIN。
```

---

# 10. ET 下处理写事件

ET 下 write 的正确模型：

```text
有 outputBuffer：
    关注 EPOLLOUT

EPOLLOUT 触发：
    循环 write outputBuffer
    直到 outputBuffer 写空
    或 write 返回 EAGAIN

outputBuffer 写空：
    取消 EPOLLOUT

write 返回 EAGAIN：
    保留 outputBuffer
    继续关注 EPOLLOUT
```

伪代码：

```cpp
void handleWrite(int fd) {
  while (!outputBuffer.empty()) {
    ssize_t n = write(fd, outputBuffer.data(), outputBuffer.size());

    if (n > 0) {
      outputBuffer.retrieve(n);
    } else if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      } else {
        close(fd);
        return;
      }
    }
  }

  if (outputBuffer.empty()) {
    // 取消 EPOLLOUT
  } else {
    // 继续关注 EPOLLOUT
  }
}
```

核心：

```text
读：读到 EAGAIN，说明当前读缓冲区读空。
写：写到 EAGAIN，说明当前发送缓冲区写满。
```

---

# 11. EPOLLIN / EPOLLOUT 和 LT / ET 是两个维度

这个点非常重要。

```text
EPOLLIN / EPOLLOUT 决定关注什么事件。
LT / ET 决定事件如何触发。
```

它们不是一回事。

---

## 11.1 EPOLLIN

表示关注读事件。

对于 listenfd：

```text
有新连接可以 accept。
```

对于 connfd：

```text
有数据可读，或对端关闭。
```

---

## 11.2 EPOLLOUT

表示关注写事件。

对于 connfd：

```text
socket 发送缓冲区有空间，可以写入数据。
```

---

## 11.3 LT

表示：

```text
只要 fd 仍然处于就绪状态，就持续通知。
```

---

## 11.4 ET

表示：

```text
只有 fd 就绪状态发生变化时才通知。
```

---

# 12. EPOLLOUT 可以用 LT，也可以用 ET

EPOLLOUT 不要求一定使用 ET。

## 12.1 LT 方式关注 EPOLLOUT

注册或修改事件：

```cpp
ev.events = EPOLLIN | EPOLLOUT;
epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
```

LT 下 EPOLLOUT 的含义：

```text
只要 socket 发送缓冲区有空间，epoll_wait 就会持续返回 EPOLLOUT。
```

所以 LT 下的问题是：

```text
如果一直关注 EPOLLOUT，而 socket 大多数时候又都是可写的，就会频繁返回写事件。
```

这会造成：

```text
无意义唤醒
busy loop
CPU 空转
```

因此正确做法不是“不能用 LT 关注 EPOLLOUT”，而是：

```text
不能长期无条件关注 EPOLLOUT。
```

---

## 12.2 ET 方式关注 EPOLLOUT

注册或修改事件：

```cpp
ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
```

ET 下 EPOLLOUT 通常在：

```text
不可写 → 可写
```

这种状态变化时触发。

例如：

```text
发送缓冲区满了
    ↓
内核发送出去一部分数据
    ↓
发送缓冲区有空间
    ↓
触发 EPOLLOUT
```

ET 下处理 EPOLLOUT 时，也要尽量写到：

```text
outputBuffer 为空
或者 write 返回 EAGAIN
```

---

# 13. EPOLLOUT 的核心不是 LT/ET，而是动态关注

EPOLLOUT 最重要的原则：

```text
按需关注。
```

也就是：

```text
outputBuffer 为空：
    不关注 EPOLLOUT

outputBuffer 非空：
    关注 EPOLLOUT

outputBuffer 写空：
    取消 EPOLLOUT
```

原因：

```text
socket 大多数时候都是可写的。
如果没有数据要写，却一直关注 EPOLLOUT，就会导致大量无效事件。
```

一句话：

```text
EPOLLIN 通常长期关注。
EPOLLOUT 通常动态关注。
```

---

# 14. LT / ET 下 EPOLLOUT 的处理对比

## 14.1 LT 下 EPOLLOUT

如果 outputBuffer 里有 700 字节待发送，关注 EPOLLOUT。

只要发送缓冲区有空间：

```text
epoll_wait 会返回 EPOLLOUT
```

处理：

```text
handleWrite 尝试写 outputBuffer
```

如果写完：

```text
取消 EPOLLOUT
```

如果没写完：

```text
继续关注 EPOLLOUT
```

LT 下只要 fd 仍然可写，后续还会继续通知。

---

## 14.2 ET 下 EPOLLOUT

如果 outputBuffer 里有 700 字节待发送，关注 EPOLLOUT。

当 fd 从不可写变成可写时：

```text
epoll_wait 返回 EPOLLOUT
```

处理：

```text
handleWrite 尽量写
直到 outputBuffer 为空
或者 write 返回 EAGAIN
```

如果写到 EAGAIN：

```text
说明发送缓冲区暂时满了
继续保留 outputBuffer
继续关注 EPOLLOUT
等待下一次不可写 → 可写变化
```

如果写完：

```text
取消 EPOLLOUT
```

---

# 15. LT / ET 对比表

| 对比点            | LT 水平触发     | ET 边缘触发      |
| -------------- | ----------- | ------------ |
| 触发依据           | fd 当前状态是否就绪 | fd 状态是否发生变化  |
| 默认模式           | 是           | 否，需要 EPOLLET |
| 没读完数据          | 下次还会通知      | 可能不再通知       |
| 没 accept 完     | 下次还会通知      | 可能不再通知       |
| fd 是否必须非阻塞     | 强烈建议        | 必须           |
| 是否必须处理到 EAGAIN | 建议          | 必须           |
| 编程难度           | 较低          | 较高           |
| 容错性            | 较高          | 较低           |
| 通知次数           | 可能更多        | 通常更少         |
| 适合阶段           | 初学 / 项目初期   | 框架成熟后优化      |

---

# 16. MiniNet 初期为什么建议先用 LT？

MiniNet 初期建议先使用 LT。

原因：

```text
1. LT 容错性更高
2. 即使某次没有读完，也会继续通知
3. 更适合先完成 EventLoop / Channel / Connection / Buffer 主线
4. 不容易因为漏处理导致连接卡住
5. 实现复杂度低于 ET
```

初期注册方式：

```cpp
ev.events = EPOLLIN;
```

即默认 LT。

---

# 17. 但处理逻辑可以按 ET 思路写

即使 MiniNet 初期使用 LT，处理逻辑仍然建议按 ET 思路写：

```text
listenfd 就绪：
    循环 accept 到 EAGAIN

connfd 可读：
    循环 read 到 EAGAIN
```

这样做的好处：

```text
1. 减少 LT 下重复通知
2. 减少 epoll_wait 返回次数
3. 减少用户态 / 内核态切换
4. 提高处理效率
5. 为后续切换 ET 做准备
```

这是一个稳妥的过渡方案：

```text
事件模式先用 LT。
处理逻辑接近 ET。
等 Buffer / EPOLLOUT / Connection 稳定后，再考虑 ET。
```

---

# 18. MiniNet 中 EPOLLOUT 的建议策略

MiniNet 初期建议：

```text
事件模式：
    LT

读事件：
    长期关注 EPOLLIN

写事件：
    outputBuffer 非空时关注 EPOLLOUT
    outputBuffer 清空后取消 EPOLLOUT
```

也就是：

```text
EPOLLIN 长期关注。
EPOLLOUT 按需关注。
```

这样既稳定，又不会因为 EPOLLOUT 长期触发造成 busy loop。

---

# 19. 面试表达：LT 和 ET 的区别

可以这样回答：

```text
LT 是水平触发，关注 fd 当前是否仍然处于就绪状态。只要状态满足，epoll_wait 就会持续返回该事件。例如接收缓冲区里还有数据，即使上次没有读完，下次 epoll_wait 仍然会继续通知。

ET 是边缘触发，关注 fd 就绪状态的变化。比如从不可读变成可读时通知一次。如果这次没有把数据读完，缓冲区中剩余的数据不一定会再次触发通知。

因此 ET 模式必须配合非阻塞 fd，并且在读事件到来时循环 read，直到返回 EAGAIN，表示当前数据已经读完。
```

---

# 20. 面试表达：为什么项目初期选择 LT？

可以这样回答：

```text
项目初期选择 LT 模式，是为了降低实现复杂度，提高稳定性。LT 模式下即使某次没有读完数据，epoll 仍然会继续通知，不容易因为漏读导致连接卡住。

但在具体处理逻辑上，仍然按更接近 ET 的方式去写，比如 listenfd 就绪后循环 accept 到 EAGAIN，connfd 可读后循环 read 到 EAGAIN。这样一方面可以减少重复事件通知，另一方面也为后续切换到 ET 模式打基础。
```

---

# 21. 面试表达：为什么不能一直关注 EPOLLOUT？

可以这样回答：

```text
socket 大多数时候都是可写的。如果一直关注 EPOLLOUT，即使 outputBuffer 中没有待发送数据，epoll_wait 也可能频繁返回可写事件，造成无意义唤醒甚至 busy loop。

因此 EPOLLOUT 通常是按需关注：当 outputBuffer 中有未发送完的数据时关注 EPOLLOUT；当数据全部写完后取消 EPOLLOUT。
```

---

# 22. Day 13 易错点总结

```text
1. LT 是默认模式，不需要额外设置。
2. ET 需要显式添加 EPOLLET。
3. LT 看状态，ET 看变化。
4. LT 下没读完，下次还会通知。
5. ET 下没读完，不保证再次通知。
6. ET 下必须使用非阻塞 fd。
7. ET 下 accept 要循环到 EAGAIN。
8. ET 下 read 要循环到 EAGAIN。
9. ET 下 write 要写到 outputBuffer 空或 EAGAIN。
10. EAGAIN 是非阻塞 IO 中的正常控制流。
11. listenfd 在 ET 下也要循环 accept。
12. EPOLLIN / EPOLLOUT 决定关注什么事件。
13. LT / ET 决定事件如何触发。
14. EPOLLOUT 可以用 LT，也可以用 ET。
15. EPOLLOUT 的关键是按需关注，而不是必须 ET。
16. EPOLLIN 通常长期关注。
17. EPOLLOUT 通常动态关注。
18. MiniNet 初期更适合先用 LT。
19. 处理逻辑可以按 ET 思路写，为后续优化做准备。
```

---

# 23. Day 13 总结

Day 13 的核心收获：

```text
LT：
    状态满足就通知。
    没处理完还会继续通知。
    容错性高，适合项目初期。

ET：
    状态变化才通知。
    没处理完可能不会再次通知。
    必须非阻塞，并处理到 EAGAIN。

EPOLLIN：
    通常长期关注。

EPOLLOUT：
    通常按需关注。
    outputBuffer 非空时关注。
    outputBuffer 清空后取消。

MiniNet 初期策略：
    事件模式使用 LT。
    处理逻辑按 ET 思路写。
    先把 EventLoop / Channel / Connection / Buffer 做稳。
    后续再考虑 ET 优化。
```

最重要的一句话：

> LT 看状态，ET 看变化；EPOLLIN / EPOLLOUT 决定关注什么事件，LT / ET 决定事件如何触发。MiniNet 初期先用 LT 更稳，但 accept/read 的处理逻辑可以按 ET 思路处理到 EAGAIN。
