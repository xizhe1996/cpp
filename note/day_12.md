# Day 12｜非阻塞 epoll echo server：循环 accept / read 到 EAGAIN

# 1. 今日学习目标

Day 12 在 Day 11 最小 epoll echo server 的基础上，进一步推进到非阻塞 epoll 版本。

今日目标：

- 理解为什么 epoll 通常要配合非阻塞 fd
- 复习 `setNonBlock(int fd)`
- 理解非阻塞 `accept/read/write` 的返回行为
- 理解 `EAGAIN / EWOULDBLOCK` 的含义
- 实现循环 accept 到 EAGAIN
- 实现循环 read 到 EAGAIN
- 理解当前版本仍未解决的 write / output buffer 问题
- 为后续 Reactor / Connection / Buffer 做准备

核心主线：

> epoll 负责等待 fd 就绪；非阻塞 fd 保证具体 IO 操作不会卡住事件循环；循环 accept/read 到 EAGAIN 表示把当前能处理的连接和数据都处理完。

---

# 2. Day 11 教学版 epoll server 的问题

Day 11 的最小 epoll echo server 已经可以：

```text
1. 注册 listenfd 到 epoll
2. epoll_wait 等待事件
3. listenfd 就绪时 accept 新连接
4. connfd 就绪时 read/write echo
5. 客户端关闭时 epoll_ctl DEL + close
````

但它仍然存在问题：

```text
1. listenfd / connfd 仍然是阻塞 fd
2. listenfd 就绪时只 accept 一次
3. connfd 可读时只 read 一次
4. write 没有处理部分写和 EAGAIN
5. 没有用户态 input/output buffer
```

Day 12 先解决前 3 个问题：

```text
1. listenfd 非阻塞
2. connfd 非阻塞
3. listenfd 触发后循环 accept 到 EAGAIN
4. connfd 可读后循环 read 到 EAGAIN
```

---

# 3. 为什么 epoll 要配合非阻塞 fd？

epoll 的作用是：

```text
告诉用户程序哪些 fd 已经就绪。
```

但是：

```text
epoll_wait 返回 fd 就绪，不代表后续所有 IO 操作一定不会阻塞。
```

如果 fd 仍然是阻塞模式：

```text
accept 可能阻塞
read 可能阻塞
write 可能阻塞
```

对于单线程事件循环来说：

```text
任意一个 fd 阻塞，都会卡住整个 EventLoop。
```

因此工程版 epoll server 通常会：

```text
listenfd 设置非阻塞
connfd 设置非阻塞
```

目标是：

```text
epoll_wait 返回事件
    ↓
处理 fd
    ↓
如果当前不能继续处理，返回 EAGAIN
    ↓
退出当前 fd 处理，继续处理其他 fd
```

---

# 4. 阻塞 read 和非阻塞 read 的区别

## 4.1 阻塞 read

如果 socket 是阻塞模式：

```cpp
ssize_t n = read(fd, buf, sizeof(buf));
```

当没有数据时，通常会阻塞等待。

返回情况：

```text
有数据到达：
    n > 0

对端正常关闭：
    n == 0

出错：
    n < 0
```

阻塞 read 的语义：

```text
没有数据时，先等。
等到有数据 / 对端关闭 / 出错，再返回。
```

---

## 4.2 非阻塞 read

如果 socket 是非阻塞模式：

```cpp
ssize_t n = read(fd, buf, sizeof(buf));
```

当当前没有数据时，它不会阻塞等待，而是直接返回：

```text
n == -1
errno == EAGAIN 或 EWOULDBLOCK
```

非阻塞 read 的语义：

```text
有数据就读。
没数据也不等，直接告诉你“现在没数据”。
```

---

## 4.3 对比表

| 场景    | 阻塞 read    | 非阻塞 read                       |
| ----- | ---------- | ------------------------------ |
| 有数据   | 返回 `n > 0` | 返回 `n > 0`                     |
| 对端关闭  | 返回 `0`     | 返回 `0`                         |
| 连接错误  | 返回 `-1`    | 返回 `-1`                        |
| 暂时没数据 | 阻塞等待       | 返回 `-1` + `EAGAIN/EWOULDBLOCK` |

核心：

```text
EAGAIN 是非阻塞 IO 中“暂时没数据 / 暂时不能做”的正常控制流。
```

---

# 5. setNonBlock

## 5.1 作用

把 fd 设置成非阻塞模式。

## 5.2 代码

```cpp
#include <fcntl.h>
#include <cstdio>

bool setNonBlock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    perror("fcntl F_GETFL");
    return false;
  }

  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    perror("fcntl F_SETFL");
    return false;
  }

  return true;
}
```

---

## 5.3 为什么先 F_GETFL？

```cpp
int flags = fcntl(fd, F_GETFL, 0);
```

作用：

```text
获取 fd 当前的文件状态标志。
```

不能直接：

```cpp
fcntl(fd, F_SETFL, O_NONBLOCK);
```

因为这样可能覆盖 fd 原有状态。

正确方式：

```cpp
fcntl(fd, F_SETFL, flags | O_NONBLOCK);
```

含义：

```text
保留原有 flags，同时新增 O_NONBLOCK。
```

---

# 6. setNonBlock 使用位置

listenfd 创建后：

```cpp
int listenfd = socket(AF_INET, SOCK_STREAM, 0);
if (listenfd < 0) {
  perror("socket");
  return 1;
}

if (!setNonBlock(listenfd)) {
  close(listenfd);
  return 1;
}
```

每次 accept 成功后：

```cpp
int connfd = accept(listenfd, nullptr, nullptr);
if (connfd >= 0) {
  if (!setNonBlock(connfd)) {
    close(connfd);
    continue;
  }

  // epoll_ctl ADD connfd
}
```

工程版 epoll server 中：

```text
listenfd 和 connfd 都应该设置成非阻塞。
```

---

# 7. 非阻塞 accept

## 7.1 为什么循环 accept？

listenfd 触发一次 `EPOLLIN`，表示：

```text
accept 队列中有连接可以取。
```

但队列中可能不止一个连接。

如果只 accept 一次：

```cpp
int connfd = accept(listenfd, nullptr, nullptr);
```

可能还有其他连接留在队列中。

工程版写法：

```text
循环 accept，直到 EAGAIN。
```

目标：

```text
一次 listenfd 事件到来后，把当前 accept 队列中能取出的连接尽量取完。
```

---

## 7.2 非阻塞 accept 返回 EAGAIN

当 listenfd 是非阻塞模式时：

```cpp
int connfd = accept(listenfd, nullptr, nullptr);
```

如果当前没有更多连接可取，会返回：

```text
-1
errno == EAGAIN 或 EWOULDBLOCK
```

这不是严重错误，而是表示：

```text
当前 accept 队列已经取空。
```

所以应该：

```cpp
if (errno == EAGAIN || errno == EWOULDBLOCK) {
  break;
}
```

而不是关闭 listenfd。

---

# 8. handleAccept

```cpp
void handleAccept(int epfd, int listenfd) {
  while (true) {
    int connfd = accept(listenfd, nullptr, nullptr);

    if (connfd >= 0) {
      if (!setNonBlock(connfd)) {
        close(connfd);
        continue;
      }

      epoll_event ev{};
      ev.events = EPOLLIN;
      ev.data.fd = connfd;

      if (epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev) < 0) {
        perror("epoll_ctl add connfd");
        close(connfd);
        continue;
      }

      std::cout << "client connected, fd = " << connfd << std::endl;

    } else {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      } else {
        perror("accept");
        break;
      }
    }
  }
}
```

---

## 8.1 handleAccept 的逻辑

```text
while true:
    accept

    connfd >= 0:
        设置 connfd 非阻塞
        注册 connfd 到 epoll
        继续 accept

    connfd < 0 且 errno == EAGAIN/EWOULDBLOCK:
        accept 队列已经取空
        break

    connfd < 0 且其他错误:
        perror
        break
```

---

## 8.2 为什么 EAGAIN 时 break？

因为：

```text
EAGAIN 表示当前没有更多连接可以取。
listenfd 本身仍然有效。
```

所以只是退出本轮 accept 循环，回到 epoll_wait，等待下次新连接事件。

不能：

```text
close(listenfd)
```

否则服务端就不能继续接收后续连接。

---

## 8.3 setNonBlock(connfd) 失败为什么要 close(connfd)？

因为：

```text
connfd 已经 accept 成功，是一个真实打开的 fd。
如果不再使用它，就必须 close，否则会造成 fd 泄漏。
```

同时当前服务端要求所有 connfd 都是非阻塞 fd。

如果设置非阻塞失败，继续使用它可能导致：

```text
某个 connfd 阻塞整个事件循环。
```

所以应该直接关闭。

---

# 9. 非阻塞 read

## 9.1 为什么循环 read？

connfd 触发 `EPOLLIN` 时，表示：

```text
socket 接收缓冲区中有数据可读，或者对端关闭。
```

但缓冲区中可能有很多数据。

如果只 read 一次：

```cpp
read(fd, buf, sizeof(buf));
```

可能没有把当前缓冲区中的数据读完。

工程版写法：

```text
循环 read，直到 EAGAIN。
```

---

## 9.2 非阻塞 read 返回 EAGAIN

当 connfd 是非阻塞模式时：

```cpp
ssize_t n = read(fd, buf, sizeof(buf));
```

如果当前没有更多数据可读，会返回：

```text
-1
errno == EAGAIN 或 EWOULDBLOCK
```

这表示：

```text
本轮数据已经读完。
```

所以应该退出 read 循环，回到 epoll_wait。

---

# 10. handleRead

```cpp
void handleRead(int epfd, int fd) {
  char buf[1024];

  while (true) {
    ssize_t n = read(fd, buf, sizeof(buf));

    if (n > 0) {
      ssize_t written = write(fd, buf, n);
      if (written < 0) {
        perror("write");
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
        return;
      }
    } else if (n == 0) {
      std::cout << "client closed, fd = " << fd << std::endl;
      epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
      close(fd);
      return;
    } else {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
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

---

## 10.1 handleRead 的逻辑

```text
while true:
    read

    n > 0:
        读到数据，尝试 write echo

    n == 0:
        对端关闭
        epoll_ctl DEL
        close
        return

    n < 0 且 errno == EAGAIN/EWOULDBLOCK:
        当前数据已经读完
        break

    n < 0 且其他错误:
        perror
        epoll_ctl DEL
        close
        return
```

---

# 11. 当前 handleRead 的已知问题

当前版本中：

```cpp
ssize_t written = write(fd, buf, n);
```

仍然没有完整处理：

```text
1. 部分写
2. write 返回 EAGAIN
3. output buffer
4. EPOLLOUT 动态关注
```

所以它仍然是教学版。

---

# 12. write 的复杂性

## 12.1 非阻塞 write 返回 EAGAIN

如果 fd 是非阻塞的，当 socket 内核发送缓冲区暂时没有空间时：

```cpp
write(fd, buf, len);
```

可能返回：

```text
-1
errno == EAGAIN 或 EWOULDBLOCK
```

含义：

```text
当前暂时不能写。
这不是连接一定出错，而是稍后再试。
```

如果是阻塞 write，此时可能会阻塞等待发送缓冲区可用。

---

## 12.2 部分写

即使 write 成功，也可能只写出部分数据。

例如：

```cpp
ssize_t written = write(fd, buf, 1000);
```

可能返回：

```text
300
```

表示只写出 300 字节，剩下 700 字节不能丢。

---

# 13. output buffer

## 13.1 作用

output buffer 是每个连接在用户态维护的发送缓冲区。

作用：

```text
保存业务层想发送但尚未成功写入 socket 内核发送缓冲区的数据。
```

当 write 没写完时：

```text
已写出部分：
    不用管

未写出部分：
    放入 output buffer
```

后续等 fd 可写时继续发送。

---

## 13.2 为什么需要 EPOLLOUT？

`EPOLLOUT` 表示 fd 当前可写，socket 发送缓冲区有空间。

当 output buffer 中有数据没发完时，需要关注：

```cpp
EPOLLOUT
```

流程：

```text
write 没写完
    ↓
剩余数据放入 output buffer
    ↓
epoll_ctl MOD，增加 EPOLLOUT
    ↓
epoll_wait 返回 EPOLLOUT
    ↓
继续 write output buffer
    ↓
如果写完
        取消 EPOLLOUT
```

---

## 13.3 为什么不能一直关注 EPOLLOUT？

因为 socket 大多数时候都是可写的。

如果一直关注 EPOLLOUT，即使没有待发送数据，epoll_wait 也可能频繁返回写事件。

这会造成：

```text
无意义唤醒
busy loop
CPU 空转
```

所以工程版通常是：

```text
output buffer 为空：
    不关注 EPOLLOUT

output buffer 非空：
    关注 EPOLLOUT
```

一句话：

```text
只有有数据要写时，才关注 EPOLLOUT；写完后取消 EPOLLOUT。
```

---

# 14. Day 12 完整代码

```cpp
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <iostream>

bool setNonBlock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    perror("fcntl F_GETFL");
    return false;
  }

  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    perror("fcntl F_SETFL");
    return false;
  }

  return true;
}

void handleAccept(int epfd, int listenfd) {
  while (true) {
    int connfd = accept(listenfd, nullptr, nullptr);

    if (connfd >= 0) {
      if (!setNonBlock(connfd)) {
        close(connfd);
        continue;
      }

      epoll_event ev{};
      ev.events = EPOLLIN;
      ev.data.fd = connfd;

      if (epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev) < 0) {
        perror("epoll_ctl add connfd");
        close(connfd);
        continue;
      }

      std::cout << "client connected, fd = " << connfd << std::endl;

    } else {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      } else {
        perror("accept");
        break;
      }
    }
  }
}

void handleRead(int epfd, int fd) {
  char buf[1024];

  while (true) {
    ssize_t n = read(fd, buf, sizeof(buf));

    if (n > 0) {
      ssize_t written = write(fd, buf, n);
      if (written < 0) {
        perror("write");
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
        close(fd);
        return;
      }
    } else if (n == 0) {
      std::cout << "client closed, fd = " << fd << std::endl;
      epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
      close(fd);
      return;
    } else {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
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

int main() {
  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    perror("socket");
    return 1;
  }

  if (!setNonBlock(listenfd)) {
    close(listenfd);
    return 1;
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(8080);

  if (bind(listenfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    perror("bind");
    close(listenfd);
    return 1;
  }

  if (listen(listenfd, 128) < 0) {
    perror("listen");
    close(listenfd);
    return 1;
  }

  int epfd = epoll_create1(0);
  if (epfd < 0) {
    perror("epoll_create1");
    close(listenfd);
    return 1;
  }

  epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.fd = listenfd;

  if (epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev) < 0) {
    perror("epoll_ctl add listenfd");
    close(epfd);
    close(listenfd);
    return 1;
  }

  std::cout << "non-blocking epoll echo server listening on port 8080\n";

  const int MAX_EVENTS = 1024;
  epoll_event events[MAX_EVENTS];

  while (true) {
    int n = epoll_wait(epfd, events, MAX_EVENTS, -1);

    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }

      perror("epoll_wait");
      break;
    }

    for (int i = 0; i < n; ++i) {
      int fd = events[i].data.fd;

      if (fd == listenfd) {
        handleAccept(epfd, listenfd);
      } else {
        handleRead(epfd, fd);
      }
    }
  }

  close(epfd);
  close(listenfd);

  return 0;
}
```

---

# 15. 当前版本的能力

当前 Day 12 版本已经支持：

```text
1. listenfd 非阻塞
2. connfd 非阻塞
3. epoll_wait 事件循环
4. listenfd 可读时循环 accept 到 EAGAIN
5. connfd 可读时循环 read 到 EAGAIN
6. 多客户端连接管理
7. 客户端关闭时 epoll_ctl DEL + close
```

---

# 16. 当前版本的限制

当前版本仍然没有解决：

```text
1. write 部分写
2. write EAGAIN
3. output buffer
4. EPOLLOUT 动态关注
5. input buffer
6. 粘包 / 拆包
7. Connection 对象
8. Channel 事件分发
9. EventLoop 封装
10. Acceptor 封装
11. 线程池处理业务
```

所以它仍然是教学版。

---

# 17. 后续 Reactor 演进方向

后续会从当前函数式代码演进成：

```text
EventLoop：
    负责 epoll_wait 和事件循环

Channel：
    封装 fd、关注事件、回调函数

Connection：
    封装单个客户端连接状态

Buffer：
    封装 inputBuffer / outputBuffer

Acceptor：
    封装 listenfd 和 accept 新连接

ThreadPool：
    处理耗时业务逻辑
```

其中：

```text
handleRead：
    从 socket 读数据到 inputBuffer

handleWrite：
    从 outputBuffer 写数据到 socket

send：
    尝试直接写
    写不完就放入 outputBuffer
    注册 EPOLLOUT
```

这就是 MiniNet 的核心主线。

---

# 18. Day 12 面试问答

## Q1：为什么 epoll server 通常要设置 fd 为非阻塞？

答：

因为 epoll_wait 只表示 fd 就绪，但真正的 accept/read/write 仍然可能阻塞。如果 fd 是阻塞模式，某个 fd 的 IO 操作可能卡住整个事件循环。设置非阻塞后，无法立即完成的 IO 操作会返回 EAGAIN，而不是阻塞等待。

---

## Q2：非阻塞 accept 返回 EAGAIN 表示什么？

答：

表示当前没有更多已完成连接可以 accept，accept 队列已经取空。这不是错误，应该退出 accept 循环，回到 epoll_wait 等待下一次事件。

---

## Q3：非阻塞 read 返回 EAGAIN 表示什么？

答：

表示当前没有更多数据可读，也就是本轮已经把内核接收缓冲区中当前可读的数据读完了。这不是连接错误。

---

## Q4：为什么 listenfd 可读后要循环 accept？

答：

一次 listenfd 的 EPOLLIN 事件可能对应 accept 队列中有多个已完成连接。循环 accept 可以一次性把当前队列中的连接尽量取完，直到 EAGAIN，减少重复事件触发和用户态 / 内核态切换。

---

## Q5：为什么 connfd 可读后要循环 read？

答：

一次 connfd 的 EPOLLIN 事件到来时，内核接收缓冲区中可能有多段数据。循环 read 可以尽量把当前已到达的数据读完，直到 EAGAIN。对于 ET 模式，这一点尤其重要，因为如果不读到 EAGAIN，剩余数据可能不会再次触发通知。

---

## Q6：非阻塞 write 返回 EAGAIN 表示什么？

答：

表示当前 socket 内核发送缓冲区暂时没有空间，不能继续写。这不是连接一定出错，而是应该稍后等 EPOLLOUT 事件再继续写。

---

## Q7：write 返回值小于要写入的长度怎么办？

答：

说明发生了部分写。已经写出去的部分不用管，未写出去的剩余数据必须保存到该连接的 output buffer 中，之后关注 EPOLLOUT，等 fd 可写时继续发送。

---

## Q8：为什么不能一直关注 EPOLLOUT？

答：

socket 大多数时候都是可写的。如果一直关注 EPOLLOUT，即使没有待发送数据，也会导致 epoll_wait 频繁返回可写事件，造成无意义唤醒甚至 busy loop。因此只有 output buffer 非空时才关注 EPOLLOUT，写完后取消关注。

---

# 19. Day 12 易错点总结

```text
1. epoll_wait 返回就绪，不代表后续 IO 操作绝不会阻塞。
2. 工程版 epoll 通常必须配合非阻塞 fd。
3. listenfd 和 connfd 都应该设置为非阻塞。
4. 非阻塞 accept/read/write 的 EAGAIN 不是严重错误。
5. accept 返回 EAGAIN 表示 accept 队列取空。
6. read 返回 EAGAIN 表示当前数据读完。
7. write 返回 EAGAIN 表示当前暂时不能写。
8. listenfd 可读后要循环 accept。
9. connfd 可读后要循环 read。
10. setNonBlock 失败后要 close 已创建的 fd。
11. EAGAIN 时不要 close listenfd。
12. write 可能部分写。
13. 未写完数据要进入 output buffer。
14. 有待发送数据时才关注 EPOLLOUT。
15. 当前代码仍然没有 input/output Buffer 和 Connection 封装。
```

---

# 20. Day 12 总结

Day 12 的核心收获：

```text
非阻塞 epoll 的核心模型：

epoll_wait 等待事件
    ↓
listenfd 可读
    ↓
循环 accept 到 EAGAIN
    ↓
connfd 设置非阻塞并注册 EPOLLIN

epoll_wait 等待事件
    ↓
connfd 可读
    ↓
循环 read 到 EAGAIN
    ↓
读到数据则 echo
    ↓
对端关闭或错误则 DEL + close
```

最重要的一句话：

> 非阻塞 epoll 的关键不是“调用一次 accept/read”，而是把当前能处理的连接和数据都处理到 EAGAIN，然后立刻回到 epoll_wait，继续处理其他 fd。
