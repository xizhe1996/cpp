# Day 11｜IO 多路复用与 epoll 基础

# 1. 今日学习目标

Day 11 从 Day 10 的阻塞 IO 问题继续，进入 IO 多路复用。

今日目标：

- 理解为什么需要 IO 多路复用
- 理解 select / poll / epoll 解决的问题
- 理解 fd 可读 / 可写的含义
- 掌握 epoll 的三个核心 API
- 写出最小 epoll echo server
- 理解 listenfd 和 connfd 在 epoll 中的不同处理
- 理解当前教学版 epoll server 的问题
- 理解为什么工程版需要非阻塞 fd
- 掌握 `setNonBlock(int fd)` 的写法
- 理解 EAGAIN / EWOULDBLOCK 的含义
- 为后续 Reactor / MiniNet 做准备

核心主线：

> epoll 不负责替用户程序 accept/read/write，它只负责告诉用户程序哪些 fd 已经就绪。

---

# 2. 为什么需要 IO 多路复用？

Day 10 的单线程阻塞 IO server 存在问题：

```text
如果服务端正在处理 client A，并阻塞在 client A 的 read 上，
那么它无法继续 accept client B，也无法处理其他客户端。
````

一连接一线程模型可以缓解这个问题：

```text
每个客户端连接由独立线程处理。
```

但缺点是：

```text
1. 连接数越多，线程数越多
2. 每个线程都有栈空间开销
3. 大量线程上下文切换成本高
4. 阻塞 IO 下很多线程只是空等
5. detach 后线程生命周期难管理
```

因此需要一种机制：

```text
一个线程同时监听多个 fd。
哪个 fd 就绪了，就处理哪个 fd。
```

这就是 IO 多路复用。

---

# 3. IO 多路复用的核心思想

IO 多路复用的核心是：

```text
一个线程同时等待多个 fd 的 IO 就绪事件。
```

不是：

```text
一个线程同时执行多个 read/write。
```

而是：

```text
同时监控多个 fd。
哪个 fd 可读 / 可写，就处理哪个 fd。
```

可以理解为：

```text
没有 IO 多路复用：
    一个线程阻塞等待一个 fd。

有 IO 多路复用：
    一个线程等待多个 fd。
    epoll_wait 返回哪些 fd 就绪。
```

---

# 4. 什么叫 fd 可读？

对于读事件来说：

```text
fd 可读
```

表示：

```text
此时调用 read / accept 有结果可返回，不会一直阻塞等待普通数据。
```

不同 fd 含义不同。

## 4.1 listenfd 可读

listenfd 可读表示：

```text
有新的已完成连接在 accept 队列中。
此时调用 accept 通常可以得到新的 connfd。
```

注意：

```text
listenfd 可读不是 read 业务数据，而是 accept 新连接。
```

## 4.2 connfd 可读

connfd 可读通常表示：

```text
1. 有数据可以 read
2. 或者对端关闭连接，read 会返回 0
3. 或者连接出现异常，read 返回错误
```

所以 connfd 可读后，需要调用 read，并根据返回值判断。

---

# 5. select / poll / epoll 的关系

它们都是 IO 多路复用机制。

## 5.1 select

特点：

```text
1. 使用 fd_set 表示 fd 集合
2. 有 fd 数量限制
3. 每次调用都要重新设置 fd 集合
4. 内核需要扫描 fd 集合
```

## 5.2 poll

特点：

```text
1. 使用 pollfd 数组表示 fd 集合
2. 没有 select 那种固定 fd_set 限制
3. 但仍然需要线性扫描
```

## 5.3 epoll

特点：

```text
1. Linux 特有
2. 通过 epoll instance 管理 fd 集合
3. fd 注册后保存在内核中
4. epoll_wait 返回就绪事件列表
5. 更适合大量 fd 场景
```

当前阶段先记：

```text
select / poll / epoll 都是为了让一个线程管理多个 fd。
epoll 是 Linux 下更常用的高并发 IO 多路复用机制。
```

---

# 6. epoll 的三个核心 API

epoll 主要使用三个系统调用：

```text
1. epoll_create1()
2. epoll_ctl()
3. epoll_wait()
```

---

# 7. epoll_create1

## 7.1 用法

```cpp
int epfd = epoll_create1(0);
```

## 7.2 作用

```text
创建一个 epoll 实例，返回 epoll fd。
```

可以理解为：

```text
创建一个事件管理器。
```

`epfd` 本身也是 fd，需要在不使用时：

```cpp
close(epfd);
```

否则会造成 fd 和内核资源泄漏。

---

# 8. epoll_ctl

## 8.1 用法

```cpp
epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
```

## 8.2 作用

管理 epoll 实例中的 fd 事件。

常见操作：

```text
EPOLL_CTL_ADD：
    添加 fd

EPOLL_CTL_MOD：
    修改 fd 关注的事件

EPOLL_CTL_DEL：
    删除 fd
```

例如注册 listenfd：

```cpp
epoll_event ev{};
ev.events = EPOLLIN;
ev.data.fd = listenfd;

epoll_ctl(epfd, EPOLL_CTL_ADD, listenfd, &ev);
```

含义：

```text
把 listenfd 添加到 epoll 实例 epfd 中，并关注 listenfd 的 EPOLLIN 事件。
```

---

# 9. epoll_event

常用字段：

```cpp
epoll_event ev{};
ev.events = EPOLLIN;
ev.data.fd = listenfd;
```

## 9.1 events

```cpp
ev.events = EPOLLIN;
```

表示关注：

```text
可读事件
```

对于 listenfd：

```text
EPOLLIN 表示有新连接可以 accept。
```

对于 connfd：

```text
EPOLLIN 表示有数据可读，或者对端关闭。
```

## 9.2 data.fd

```cpp
ev.data.fd = listenfd;
```

作用：

```text
设置事件触发后，epoll_wait 返回给用户态的附带数据。
```

当事件触发后，可以通过：

```cpp
int fd = events[i].data.fd;
```

知道是哪个 fd 就绪。

注意：

```text
epoll_ctl 的第三个参数才是真正告诉 epoll 要监听哪个 fd。
ev.data.fd 是为了事件返回时方便用户程序识别 fd。
```

---

# 10. epoll_wait

## 10.1 用法

```cpp
epoll_event events[1024];

int n = epoll_wait(epfd, events, 1024, -1);
```

## 10.2 参数含义

```text
epfd：
    epoll 实例 fd

events：
    用户态数组，用来接收就绪事件

1024：
    最多接收多少个事件

-1：
    超时时间，-1 表示一直阻塞等待
```

## 10.3 返回值

```text
n > 0：
    本次返回 n 个就绪事件

n == 0：
    超时，没有事件

n < 0：
    出错
```

注意：

```text
n 表示就绪事件数量，不是简单的 fd 数量。
```

一个 fd 可能同时带有多个事件标志，例如：

```cpp
EPOLLIN | EPOLLERR | EPOLLHUP
```

---

# 11. epoll server 的基本逻辑

整体流程：

```text
socket
bind
listen

epoll_create1
epoll_ctl ADD listenfd

while true:
    epoll_wait

    for 每个就绪事件:
        if fd == listenfd:
            accept 新连接
            epoll_ctl ADD connfd
        else:
            read connfd
            write echo
            如果关闭或出错：
                epoll_ctl DEL connfd
                close connfd
```

---

# 12. listenfd 和 connfd 的处理区别

## 12.1 listenfd 就绪

表示：

```text
有新连接可以 accept。
```

处理：

```cpp
int connfd = accept(listenfd, nullptr, nullptr);
```

然后：

```cpp
epoll_event connEv{};
connEv.events = EPOLLIN;
connEv.data.fd = connfd;

epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &connEv);
```

原因：

```text
listenfd 只负责新连接事件。
connfd 才负责客户端通信。
如果不把 connfd 注册到 epoll，客户端之后发数据时，epoll_wait 不会通知。
```

## 12.2 connfd 就绪

表示：

```text
客户端发来了数据，或者客户端关闭连接。
```

处理：

```cpp
ssize_t nread = read(fd, buf, sizeof(buf));
```

根据返回值：

```text
nread > 0：
    读到数据，echo 回去

nread == 0：
    客户端关闭连接，删除 epoll 监听并 close fd

nread < 0：
    读取出错，删除 epoll 监听并 close fd
```

---

# 13. 关闭 connfd 前为什么建议 epoll_ctl DEL？

教学版中，直接：

```cpp
close(fd);
```

通常也可以，因为 Linux 关闭 fd 时通常会清理 epoll 关系。

但工程上更推荐：

```cpp
epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
close(fd);
```

原因：

```text
1. 语义更清晰：这个 fd 不再由 epoll 管理。
2. 生命周期更明确：先从事件管理器删除，再关闭 fd。
3. 减少 fd 复用带来的理解复杂度。
```

---

# 14. 最小 epoll echo server

```cpp
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>

int main() {
  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    perror("socket");
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

  std::cout << "epoll echo server listening on port 8080\n";

  const int MAX_EVENTS = 1024;
  epoll_event events[MAX_EVENTS];

  while (true) {
    int n = epoll_wait(epfd, events, MAX_EVENTS, -1);

    if (n < 0) {
      perror("epoll_wait");
      break;
    }

    for (int i = 0; i < n; ++i) {
      int fd = events[i].data.fd;

      if (fd == listenfd) {
        int connfd = accept(listenfd, nullptr, nullptr);
        if (connfd < 0) {
          perror("accept");
          continue;
        }

        epoll_event connEv{};
        connEv.events = EPOLLIN;
        connEv.data.fd = connfd;

        if (epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &connEv) < 0) {
          perror("epoll_ctl add connfd");
          close(connfd);
          continue;
        }

        std::cout << "client connected, fd = " << connfd << std::endl;

      } else {
        char buf[1024];
        ssize_t nread = read(fd, buf, sizeof(buf));

        if (nread > 0) {
          ssize_t written = write(fd, buf, nread);
          if (written < 0) {
            perror("write");
            epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
            close(fd);
          }
        } else if (nread == 0) {
          std::cout << "client closed, fd = " << fd << std::endl;
          epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
          close(fd);
        } else {
          perror("read");
          epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
          close(fd);
        }
      }
    }
  }

  close(epfd);
  close(listenfd);

  return 0;
}
```

---

# 15. 当前教学版 epoll server 的问题

当前版本已经能通过一个线程管理多个客户端，但仍然不是工程版。

主要问题：

```text
1. fd 仍然是阻塞模式
2. listenfd 就绪时只 accept 一次
3. connfd 可读时只 read 一次
4. write 没有处理部分写
5. 没有用户态输入 / 输出 Buffer
6. 没有动态关注 EPOLLOUT
7. 没有处理 EPOLLERR / EPOLLHUP
8. 没有 Connection / Channel / EventLoop 抽象
```

---

# 16. 为什么阻塞 fd 仍然有问题？

即使用了 epoll，如果 fd 本身是阻塞的，仍然可能发生：

```text
某个 fd 的 accept/read/write 阻塞，导致整个事件循环被卡住。
```

所以工程版 epoll server 通常会：

```text
listenfd 设置非阻塞
connfd 设置非阻塞
```

---

# 17. 为什么要循环 accept？

一次 listenfd 的 EPOLLIN 事件可能对应多个已完成连接。

工程版通常会循环：

```text
accept
accept
accept
...
直到 EAGAIN
```

也就是：

```cpp
while (true) {
  int connfd = accept(listenfd, nullptr, nullptr);

  if (connfd >= 0) {
    // 注册 connfd
  } else {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      break;
    } else {
      perror("accept");
      break;
    }
  }
}
```

非阻塞 accept 返回 EAGAIN 表示：

```text
当前没有更多已完成连接可以 accept。
```

---

# 18. 为什么要循环 read？

一次 connfd 的 EPOLLIN 事件到来时，内核接收缓冲区中可能还有多段数据。

工程版通常会循环：

```text
read
read
read
...
直到 EAGAIN
```

非阻塞 read 返回 EAGAIN 表示：

```text
当前内核接收缓冲区已经没有更多数据可读。
```

这不是严重错误。

---

# 19. write 的问题与 EPOLLOUT

write 可能出现两个问题：

```text
1. 只写出部分数据
2. 发送缓冲区满，返回 EAGAIN
```

所以工程版需要：

```text
输出缓冲区 output buffer
```

用于保存没写完的数据。

当 output buffer 中有待发送数据时，才关注：

```cpp
EPOLLOUT
```

当数据全部写完后，取消：

```cpp
EPOLLOUT
```

原因：

```text
socket 大多数时候都是可写的。
如果一直关注 EPOLLOUT，epoll_wait 会频繁返回写事件，造成无意义唤醒甚至 busy loop。
```

---

# 20. setNonBlock

## 20.1 作用

把 fd 设置成非阻塞模式。

## 20.2 代码

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

## 20.3 为什么要先 F_GETFL？

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

因为这可能覆盖 fd 原有状态。

正确做法是：

```cpp
flags | O_NONBLOCK
```

含义：

```text
保留原有 flags，同时新增 O_NONBLOCK。
```

---

# 21. EAGAIN / EWOULDBLOCK

对于非阻塞 fd：

```text
如果当前操作不能立即完成，系统调用不会阻塞，而是返回 -1，并设置 errno。
```

常见情况：

## 21.1 非阻塞 accept

```text
errno == EAGAIN / EWOULDBLOCK
```

表示：

```text
当前没有更多已完成连接可以 accept。
```

## 21.2 非阻塞 read

表示：

```text
当前没有更多数据可读。
```

## 21.3 非阻塞 write

表示：

```text
当前发送缓冲区暂时不可写。
```

这些不一定是严重错误，而是非阻塞 IO 的正常控制流。

---

# 22. 工程版 Reactor 会引入什么？

当前教学版代码把所有逻辑写在一个大循环里。

后续 Reactor 会拆成：

```text
EventLoop：
    负责 epoll_wait 和事件循环

Channel：
    封装 fd 和关注的事件

Connection：
    封装一个客户端连接，包括 fd、状态、输入缓冲区、输出缓冲区

Acceptor：
    专门负责 listenfd 和 accept 新连接

Buffer：
    处理用户态输入 / 输出缓冲区，支持粘包 / 拆包处理
```

这就是 MiniNet 项目后续主线。

---

# 23. Day 11 面试问答

## Q1：什么是 IO 多路复用？

答：

IO 多路复用是让一个线程同时等待多个 fd 的 IO 就绪事件。它不是让一个线程同时执行多个 read/write，而是通过 select/poll/epoll 等机制监听多个 fd，哪个 fd 就绪就处理哪个 fd。

---

## Q2：epoll 解决什么问题？

答：

epoll 让一个线程可以高效管理多个 fd 的事件。相比一连接一线程模型，epoll 不需要为每个连接创建线程，可以减少线程数量、栈空间消耗和上下文切换开销。

---

## Q3：listenfd 可读和 connfd 可读有什么区别？

答：

listenfd 可读表示有新的连接可以 accept；connfd 可读表示有数据可以 read，或者对端关闭连接，read 返回 0。

---

## Q4：epoll 的三个核心 API 是什么？

答：

`epoll_create1` 创建 epoll 实例，返回 epfd；`epoll_ctl` 添加、修改或删除 fd 的监听事件；`epoll_wait` 等待已注册 fd 的就绪事件，并把事件返回给用户态。

---

## Q5：epoll_wait 返回的 n 是什么？

答：

n 表示本次返回的就绪事件数量。通常每个事件对应一个 fd，但严格来说是事件数量，因为一个 fd 可能同时带有多个事件标志。

---

## Q6：epoll_event.data.fd 有什么作用？

答：

`data.fd` 是用户设置的附带数据，事件触发后会由 epoll_wait 原样返回。通常把 fd 放进去，方便事件返回时知道哪个 fd 就绪。

---

## Q7：为什么 accept 得到 connfd 后还要 epoll_ctl ADD？

答：

listenfd 只负责新连接事件，connfd 才负责和客户端通信。如果不把 connfd 注册进 epoll，客户端发数据时 epoll_wait 不会通知这个 connfd 的读事件。

---

## Q8：关闭 connfd 前为什么建议 epoll_ctl DEL？

答：

close(fd) 通常会让内核清理 epoll 关系，但工程上显式 `EPOLL_CTL_DEL` 语义更清晰，表示这个 fd 不再由 epoll 管理，也有助于避免 fd 复用造成的调试混淆。

---

## Q9：为什么 epoll server 还要使用非阻塞 fd？

答：

epoll_wait 只表示某个 fd 就绪，但 fd 本身如果是阻塞模式，accept/read/write 仍然可能在边界情况下阻塞。单线程事件循环中，一个 fd 阻塞会卡住整个服务，所以工程版通常把 listenfd 和 connfd 都设置为非阻塞。

---

## Q10：非阻塞 read 返回 EAGAIN 表示什么？

答：

表示当前没有更多数据可读，不是连接错误。通常说明本轮已经把内核接收缓冲区中的数据读完，可以退出 read 循环。

---

## Q11：为什么有待发送数据时才关注 EPOLLOUT？

答：

socket 大多数时候都是可写的。如果长期关注 EPOLLOUT，epoll_wait 会频繁返回写事件，造成无意义唤醒甚至 busy loop。因此通常只有 output buffer 中有未发送完的数据时才关注 EPOLLOUT，写完后取消关注。

---

# 24. Day 11 易错点总结

```text
1. epoll 不替你 read/write，只告诉你 fd 就绪。
2. listenfd 可读表示可以 accept，不是 read 数据。
3. connfd 可读可能是有数据，也可能是对端关闭。
4. epfd 也是 fd，需要 close。
5. ev.data.fd 是事件返回时的附带数据。
6. epoll_wait 返回的是就绪事件数量。
7. accept 得到 connfd 后必须注册到 epoll，后续才能监听数据。
8. close connfd 前建议 epoll_ctl DEL。
9. 使用 epoll 不代表 IO 操作自动非阻塞。
10. 工程版 epoll 通常配合非阻塞 fd。
11. 非阻塞 accept/read/write 的 EAGAIN 不是严重错误。
12. listenfd 就绪时工程版通常循环 accept。
13. connfd 可读时工程版通常循环 read。
14. write 可能部分写。
15. 有未发送数据才关注 EPOLLOUT。
16. 当前教学版 epoll server 还没有 Buffer / Connection / Channel / EventLoop。
```

---

# 25. Day 11 总结

Day 11 的核心收获：

```text
IO 多路复用：
    一个线程同时等待多个 fd 的 IO 就绪事件。

epoll：
    Linux 下常用的 IO 多路复用机制。

epoll_create1：
    创建 epoll 实例。

epoll_ctl：
    注册、修改、删除 fd 事件。

epoll_wait：
    等待就绪事件。

listenfd EPOLLIN：
    有新连接可以 accept。

connfd EPOLLIN：
    有数据可读，或对端关闭。

非阻塞 fd：
    防止 accept/read/write 阻塞整个事件循环。

EAGAIN：
    非阻塞 IO 下表示当前没有更多连接/数据/写空间。
```

最重要的一句话：

> epoll 是 Reactor 的底层事件等待机制；它只告诉程序哪些 fd 就绪，真正的 accept/read/write、Buffer 管理和连接状态维护，都需要用户态网络框架自己完成。

