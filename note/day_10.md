# Day 10｜Linux 网络编程入门：Socket、TCP 服务端与阻塞 IO

# 1. 今日学习目标

Day 10 开始进入 Linux 网络编程主线。

今日目标：

- 理解 socket 是什么
- 理解 socket fd 和普通 fd 的关系
- 理解 listenfd 和 connfd 的区别
- 掌握 TCP 服务端基本系统调用流程
- 手写最小 echo server
- 理解 read / write 的阻塞行为
- 理解用户缓冲区和内核 socket 缓冲区
- 写出循环 read/write 的 echo server
- 写出串行多客户端 server
- 理解一连接一线程模型
- 明确为什么后续需要 epoll / Reactor

核心主线：

> socket 是用户态程序访问网络通信能力的文件描述符；服务端通过 socket / bind / listen / accept 建立监听和连接，通过 read / write 在 connfd 上收发数据。

---

# 2. socket 是什么？

在 Linux 网络编程中，`socket` 可以理解为：

```text
用户态程序访问网络通信能力的一个文件描述符。
````

创建 socket：

```cpp
int fd = socket(AF_INET, SOCK_STREAM, 0);
```

成功时返回：

```text
一个 socket fd
```

失败时返回：

```text
-1，并设置 errno
```

socket fd 可以被很多 Linux IO 接口操作：

```cpp
read()
write()
close()
fcntl()
select()
poll()
epoll()
```

所以可以把 socket fd 理解为：

```text
Linux 把网络连接也抽象成 fd 来操作。
```

---

# 3. socket fd 也是资源

socket fd 是一种系统资源。

成功创建后：

```cpp
int sockfd = socket(AF_INET, SOCK_STREAM, 0);
```

不再使用时必须：

```cpp
close(sockfd);
```

否则会造成：

```text
fd 泄漏
内核资源泄漏
进程 fd 数耗尽
```

所以 socket fd 适合用 RAII 管理：

```text
构造时获得 fd
析构时 close fd
禁用拷贝
支持移动
```

这和之前学习的 `FdGuard` 是同一类问题。

---

# 4. socket(AF_INET, SOCK_STREAM, 0) 的含义

```cpp
int fd = socket(AF_INET, SOCK_STREAM, 0);
```

三个参数分别是：

```text
AF_INET：
    IPv4 地址族

SOCK_STREAM：
    字节流 socket，通常对应 TCP

0：
    使用默认协议
```

所以：

```cpp
socket(AF_INET, SOCK_STREAM, 0)
```

表示：

```text
创建一个 IPv4 TCP socket fd
```

注意：

```text
socket() 只是创建 socket fd，不等于建立 TCP 连接。
```

对于服务端，后续还需要：

```text
bind → listen → accept
```

对于客户端，后续还需要：

```text
connect
```

---

# 5. listenfd 和 connfd

服务端通常有两类 socket fd：

```text
1. listenfd
2. connfd
```

---

## 5.1 listenfd

服务端启动时创建：

```cpp
int listenfd = socket(AF_INET, SOCK_STREAM, 0);
```

然后经过：

```text
bind()
listen()
```

之后，这个 fd 用于：

```text
监听端口
接收客户端连接请求
```

它不直接负责和某个具体客户端通信。

---

## 5.2 connfd

当客户端连接进来后，服务端调用：

```cpp
int connfd = accept(listenfd, nullptr, nullptr);
```

`accept()` 返回一个新的 fd：

```text
connfd
```

这个 `connfd` 才用于和某个具体客户端进行数据通信：

```cpp
read(connfd, buf, size);
write(connfd, buf, size);
```

---

## 5.3 listenfd 和 connfd 的区别

| fd 类型      | 来源                           | 作用                     |
| ---------- | ---------------------------- | ---------------------- |
| `listenfd` | `socket()` 创建后 `bind/listen` | 监听端口，接收新连接             |
| `connfd`   | `accept(listenfd)` 返回        | 和某个客户端进行 read/write 通信 |

可以这样记：

```text
listenfd 负责接客。
connfd 负责聊天。
```

一个服务端通常是：

```text
1 个 listenfd
N 个 connfd
```

这也是后续 epoll / Reactor 要解决的问题：

```text
如何高效管理大量 connfd 的读写事件。
```

---

# 6. TCP 服务端基本流程

一个最小 TCP 服务端流程：

```text
socket()
   ↓
bind()
   ↓
listen()
   ↓
accept()
   ↓
read() / write()
   ↓
close()
```

对应含义：

```text
socket：
    创建 socket fd

bind：
    绑定本地 IP 和端口

listen：
    让 socket 进入监听状态

accept：
    接收客户端连接，返回 connfd

read/write：
    通过 connfd 和客户端通信

close：
    释放 fd
```

---

# 7. bind()

## 7.1 bind 的作用

```cpp
bind(listenfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
```

作用：

```text
把 socket fd 绑定到本地 IP + 端口。
```

例如绑定：

```text
0.0.0.0:8080
```

表示：

```text
监听本机所有网卡上的 8080 端口。
```

---

## 7.2 sockaddr_in

IPv4 常用地址结构：

```cpp
sockaddr_in addr{};
addr.sin_family = AF_INET;
addr.sin_addr.s_addr = INADDR_ANY;
addr.sin_port = htons(8080);
```

字段含义：

```text
sin_family = AF_INET：
    IPv4 地址族

sin_addr.s_addr = INADDR_ANY：
    监听本机所有网卡地址

sin_port = htons(8080)：
    监听 8080 端口，端口需要转换成网络字节序
```

---

## 7.3 为什么 bind 需要 reinterpret_cast？

`bind` 的函数原型接收的是通用地址结构：

```cpp
const struct sockaddr* addr
```

而 IPv4 使用的是具体地址结构：

```cpp
sockaddr_in
```

所以需要：

```cpp
reinterpret_cast<sockaddr*>(&addr)
```

可以这样理解：

```text
sockaddr 是通用地址类型。
sockaddr_in 是 IPv4 地址类型。
bind 接口需要通用指针，所以 sockaddr_in* 要转成 sockaddr*。
```

---

# 8. 网络字节序与 htons()

## 8.1 为什么端口要用 htons？

TCP/IP 协议规定多字节整数使用：

```text
网络字节序，也就是大端序。
```

而常见 x86 / ARM 主机通常是：

```text
小端序。
```

所以端口号需要：

```cpp
addr.sin_port = htons(8080);
```

`htons` 含义：

```text
host to network short
```

即：

```text
16 位整数从主机字节序转换成网络字节序。
```

---

## 8.2 8080 变成 36895 的问题

如果错误写成：

```cpp
addr.sin_port = 8080;
```

可能会发现服务端实际监听：

```text
0.0.0.0:36895
```

原因是字节序错误。

8080 的十六进制是：

```text
0x1F90
```

网络字节序希望内存中是：

```text
1F 90
```

但小端机器直接写入 8080 时，内存可能是：

```text
90 1F
```

内核按网络字节序解释后就是：

```text
0x901F = 36895
```

所以：

```text
8080 ↔ 36895 是典型的字节序反转现象。
```

正确写法必须是：

```cpp
addr.sin_port = htons(8080);
```

---

# 9. listen()

## 9.1 listen 的作用

```cpp
listen(listenfd, 128);
```

作用：

```text
让 socket 进入监听状态，使它可以接收客户端连接请求。
```

注意区分：

```text
bind：
    绑定 IP/端口

listen：
    进入监听状态
```

---

## 9.2 backlog

```cpp
listen(listenfd, 128);
```

第二个参数 `128` 是 backlog。

当前阶段可以先理解为：

```text
内核中等待 accept 的连接队列长度提示值。
```

更深入的半连接队列、全连接队列，后续学习 TCP 连接队列时再展开。

---

# 10. accept()

## 10.1 accept 的作用

```cpp
int connfd = accept(listenfd, nullptr, nullptr);
```

作用：

```text
从 listenfd 上接收一个客户端连接。
成功时返回新的 connfd。
```

如果当前没有客户端连接，在阻塞模式下：

```text
accept 会阻塞等待。
```

---

## 10.2 accept 的参数

```cpp
accept(listenfd, nullptr, nullptr);
```

三个参数：

```text
第 1 个参数：
    listenfd

第 2 个参数：
    用于接收客户端地址的结构体指针

第 3 个参数：
    地址长度指针
```

当前如果不关心客户端 IP/端口，可以写：

```cpp
nullptr, nullptr
```

---

# 11. read() / write()

## 11.1 read

```cpp
char buf[1024];
ssize_t n = read(connfd, buf, sizeof(buf));
```

含义：

```text
从 connfd 对应的内核接收缓冲区中，最多读取 sizeof(buf) 字节到用户态 buf。
```

返回值：

```text
n > 0：
    实际读到 n 字节

n == 0：
    对端正常关闭连接，读到 EOF

n < 0：
    读取出错
```

---

## 11.2 write

```cpp
ssize_t n = write(connfd, buf, len);
```

含义：

```text
把用户态 buf 中的数据写入 connfd 对应的内核发送缓冲区。
```

返回值：

```text
n > 0：
    实际写入 n 字节

n < 0：
    写入出错
```

严格来说：

```text
write 不保证一次写完全部数据。
```

即：

```text
write 返回值可能小于你要求写入的长度。
```

所以严谨场景需要循环 write。

---

# 12. 阻塞 IO

socket fd 默认是阻塞模式。

在阻塞 socket 上：

```text
read：
    如果接收缓冲区没有数据，通常会阻塞等待。
    如果有数据，返回实际读到的字节数。
    如果对端关闭，返回 0。

write：
    如果发送缓冲区有空间，写入部分或全部数据后返回。
    如果发送缓冲区没有足够空间，可能阻塞等待。
```

注意：

```text
阻塞 write 不等于一定一次写完所有数据。
```

仍然要看返回值。

---

# 13. 用户缓冲区与内核 socket 缓冲区

一次 socket 读写涉及两类缓冲区。

## 13.1 内核侧

每个 TCP socket 在内核中通常有：

```text
1. socket 接收缓冲区 recv buffer
2. socket 发送缓冲区 send buffer
```

---

## 13.2 用户侧

用户态程序在 read/write 时提供自己的缓冲区：

```cpp
char buf[1024];
```

read 时：

```text
内核接收缓冲区 → 用户缓冲区
```

write 时：

```text
用户缓冲区 → 内核发送缓冲区
```

---

## 13.3 注意点

用户缓冲区不是 socket 固定拥有的资源。

更准确说：

```text
内核接收 / 发送缓冲区是 socket 内核对象的重要组成部分。

用户缓冲区是用户程序每次 read/write 时自己传入的一段内存。
```

---

# 14. TCP 是字节流协议

TCP 没有消息边界。

这意味着：

```text
一次 write 不一定对应一次 read。
一次 read 不一定读到完整业务消息。
```

例如客户端一次发送 3000 字节，服务端 buf 只有 1024：

```text
第一次 read：1024 字节
第二次 read：1024 字节
第三次 read：952 字节
```

剩余数据通常还在内核 socket 接收缓冲区，不会因为用户 buf 小就直接丢失。

所以：

```text
buf 小不等于数据丢失，只是需要多次 read。
```

后续需要 Buffer 和协议解析来解决粘包/拆包问题。

---

# 15. 最小 echo server：只处理一次 read/write

```cpp
#include <arpa/inet.h>
#include <netinet/in.h>
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

  int ret = bind(listenfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (ret < 0) {
    perror("bind");
    close(listenfd);
    return 1;
  }

  ret = listen(listenfd, 128);
  if (ret < 0) {
    perror("listen");
    close(listenfd);
    return 1;
  }

  std::cout << "server listening on port 8080" << std::endl;

  int connfd = accept(listenfd, nullptr, nullptr);
  if (connfd < 0) {
    perror("accept");
    close(listenfd);
    return 1;
  }

  std::cout << "client connected" << std::endl;

  char buf[1024];
  ssize_t n = read(connfd, buf, sizeof(buf));

  if (n > 0) {
    write(connfd, buf, n);
  } else if (n == 0) {
    std::cout << "client closed" << std::endl;
  } else {
    perror("read");
  }

  close(connfd);
  close(listenfd);

  return 0;
}
```

这个版本：

```text
accept 一个客户端
read 一次
write 一次
close
退出
```

---

# 16. 循环 echo server：支持一个客户端持续 echo

```cpp
char buf[1024];

while (true) {
  ssize_t n = read(connfd, buf, sizeof(buf));

  if (n > 0) {
    ssize_t written = write(connfd, buf, n);
    if (written < 0) {
      perror("write");
      break;
    }
  } else if (n == 0) {
    std::cout << "client closed" << std::endl;
    break;
  } else {
    perror("read");
    break;
  }
}
```

这个版本：

```text
accept 一个客户端
循环 read/write
直到客户端关闭
```

---

# 17. 严谨的 writeAll

当前阶段可以先知道 `write` 可能只写部分数据。

更严谨写法：

```cpp
bool writeAll(int fd, const char* buf, ssize_t len) {
  ssize_t total = 0;

  while (total < len) {
    ssize_t n = write(fd, buf + total, len - total);

    if (n > 0) {
      total += n;
    } else if (n == 0) {
      return false;
    } else {
      perror("write");
      return false;
    }
  }

  return true;
}
```

然后 echo 时：

```cpp
if (!writeAll(connfd, buf, n)) {
  break;
}
```

后续学习非阻塞 IO 和 Buffer 时，这个点会进一步展开。

---

# 18. 串行多客户端 server

## 18.1 handleClient

```cpp
void handleClient(int connfd) {
  char buf[1024];

  while (true) {
    ssize_t n = read(connfd, buf, sizeof(buf));

    if (n > 0) {
      ssize_t written = write(connfd, buf, n);
      if (written < 0) {
        perror("write");
        break;
      }
    } else if (n == 0) {
      std::cout << "client closed\n";
      break;
    } else {
      perror("read");
      break;
    }
  }
}
```

---

## 18.2 accept 循环

```cpp
while (true) {
  int connfd = accept(listenfd, nullptr, nullptr);
  if (connfd < 0) {
    perror("accept");
    continue;
  }

  std::cout << "client connected\n";

  handleClient(connfd);

  close(connfd);
}
```

---

## 18.3 特点

这个版本可以处理多个客户端，但只能串行处理：

```text
client A
    ↓
处理完
    ↓
client B
    ↓
处理完
    ↓
client C
```

它不能同时处理多个活跃客户端。

原因：

```text
主线程进入 handleClient(connfd) 后，会阻塞在当前客户端的 read 循环里。
只要当前客户端不关闭，程序就不会回到 accept。
```

---

# 19. 第二个客户端会发生什么？

如果 client A 一直不关闭，client B 连接进来：

```text
内核层：
    TCP 握手可能已经完成，连接可能排在 accept 队列中。

应用层：
    服务端代码还卡在 client A 的 handleClient 中，没有再次 accept。
```

所以现象可能是：

```text
client B 的 nc 看起来已经连接上；
但是发送数据没有 echo 响应；
直到 client A 关闭，服务端才回到 accept 并开始处理 client B。
```

注意区分：

```text
连接建立
```

和：

```text
应用层 accept / read / write 处理
```

不是同一件事。

---

# 20. 一连接一线程模型

为了让多个客户端可以同时被处理，一个直观方案是：

```text
每 accept 一个连接，就创建一个线程处理它。
```

代码：

```cpp
#include <thread>

while (true) {
  int connfd = accept(listenfd, nullptr, nullptr);
  if (connfd < 0) {
    perror("accept");
    continue;
  }

  std::thread t([connfd]() {
    handleClient(connfd);
    close(connfd);
  });

  t.detach();
}
```

---

## 20.1 为什么这个版本可以并发？

主线程只负责：

```text
accept 新连接
```

每个子线程负责：

```text
handleClient(connfd)
```

所以：

```text
client A 阻塞在 read
```

不会影响主线程继续：

```text
accept client B
```

---

## 20.2 为什么 connfd 要值捕获？

正确：

```cpp
[connfd]() {
  handleClient(connfd);
  close(connfd);
}
```

不要写：

```cpp
[&connfd]() {
  handleClient(connfd);
  close(connfd);
}
```

原因：

```text
connfd 是循环中的局部变量，每轮循环都会变化。
引用捕获可能导致多个线程引用同一个变量，拿到错误 fd。
值捕获会把当前 connfd 的值保存进 lambda 对象中，每个线程处理自己的 fd。
```

---

## 20.3 detach 的作用

```cpp
t.detach();
```

作用：

```text
让 std::thread 对象和底层线程执行流分离。
主线程不等待子线程结束，可以继续 accept 新连接。
子线程结束后由系统回收底层线程资源。
```

如果使用：

```cpp
t.join();
```

主线程就会等待当前客户端处理线程结束，才能继续 accept 下一个客户端，又退化成串行处理。

---

## 20.4 一连接一线程的问题

一连接一线程模型简单，但问题明显：

```text
1. 线程数量随连接数线性增长
2. 每个线程都有独立栈空间
3. 大量线程会消耗内存和内核资源
4. 大量线程会导致上下文切换成本高
5. 阻塞 IO 下很多线程可能只是空等
6. detach 后线程生命周期难管理
7. 高并发场景下容易耗尽系统资源
```

所以它不适合作为高并发网络服务模型。

---

# 21. 一个进程最多能创建多少线程？

没有固定答案。

它受多个因素影响：

```text
1. 每个线程的栈大小
2. 进程虚拟地址空间限制
3. 系统内存
4. ulimit 限制
5. kernel threads-max
6. pid_max
7. cgroup / container 限制
8. pthread / glibc 默认配置
```

其中线程栈大小是重要因素之一。

Linux 下 pthread 默认线程栈常见是：

```text
8MB
```

如果 1000 个线程：

```text
1000 × 8MB = 约 8GB 虚拟地址空间栈预留
```

但这不是唯一限制。

查看栈大小限制：

```bash
ulimit -s
```

查看用户进程/线程数量限制：

```bash
ulimit -u
```

查看系统线程数量上限：

```bash
cat /proc/sys/kernel/threads-max
```

查看 PID 上限：

```bash
cat /proc/sys/kernel/pid_max
```

---

# 22. 为什么需要 epoll / Reactor？

经过 Day 10 的代码演进，可以看到：

## 最小 server

```text
accept 一个客户端
read 一次
write 一次
退出
```

问题：

```text
只能处理一次数据。
```

---

## 循环 echo server

```text
accept 一个客户端
循环 read/write
直到客户端关闭
```

问题：

```text
只能处理一个客户端。
```

---

## 串行多客户端 server

```text
while true:
    accept 一个客户端
    handleClient
    close
```

问题：

```text
可以处理多个客户端，但不能同时处理多个活跃客户端。
```

---

## 一连接一线程 server

```text
while true:
    accept 一个客户端
    创建一个线程处理
```

问题：

```text
线程数量不可控，资源消耗大，不适合高并发。
```

---

因此自然引出：

```text
IO 多路复用
```

也就是：

```text
一个线程同时监控多个 fd 的读写事件。
```

代表机制：

```text
select
poll
epoll
```

后续更进一步会形成：

```text
Reactor 模型
```

也就是：

```text
epoll + 事件分发 + 连接对象 + Buffer + 线程池
```

这就是 MiniNet 的核心主线。

---

# 23. 今日重要面试问答

## Q1：socket() 返回什么？

答：

`socket()` 成功时返回一个 socket fd，本质上是文件描述符。失败时返回 -1，并设置 errno。socket fd 可以通过 read/write/close 等系统调用进行操作。

---

## Q2：socket(AF_INET, SOCK_STREAM, 0) 表示什么？

答：

表示创建一个 IPv4 TCP socket。`AF_INET` 表示 IPv4，`SOCK_STREAM` 表示字节流 socket，通常对应 TCP，第三个参数 0 表示使用默认协议。

---

## Q3：listenfd 和 connfd 有什么区别？

答：

listenfd 是服务端用于监听端口和接收新连接的 fd；connfd 是 accept 返回的新 fd，用于和某个具体客户端进行 read/write 数据通信。通常一个服务端有一个 listenfd，但可以有多个 connfd。

---

## Q4：TCP 服务端基本流程是什么？

答：

基本流程是 socket 创建监听 fd，bind 绑定本地 IP/端口，listen 进入监听状态，accept 接收客户端连接并返回 connfd，然后通过 read/write 和客户端通信，最后 close fd。

---

## Q5：为什么端口要用 htons？

答：

TCP/IP 协议规定多字节整数使用网络字节序，也就是大端序。而主机字节序可能是小端序，所以端口号需要通过 htons 从主机字节序转换成网络字节序。

---

## Q6：read 返回 0 表示什么？

答：

read 返回 0 表示对端正常关闭连接，也就是读到了 EOF。服务端通常应该关闭对应 connfd。

---

## Q7：为什么 read 要放在循环里？

答：

TCP 是字节流协议，一次 read 只表示当前读到了一部分数据，不代表连接结束。只要连接还没关闭，客户端后续仍然可能继续发送数据，所以服务端通常需要循环 read。

---

## Q8：write 一定会一次写完吗？

答：

不一定。write 返回值表示实际写入内核发送缓冲区的字节数，可能小于请求写入的长度。严谨场景下需要循环 write，确保全部数据写入。

---

## Q9：buf 小会导致数据丢失吗？

答：

通常不会。buf 小只表示一次 read 最多读这么多，剩余数据通常仍在内核 socket 接收缓冲区中，下一次 read 可以继续读取。TCP 是字节流协议，需要上层 Buffer 和协议解析处理完整消息。

---

## Q10：为什么单线程阻塞 IO 不能同时处理多个客户端？

答：

因为 accept、read 都可能阻塞。单线程在处理一个客户端的 read/write 循环时，如果该客户端不关闭，程序不会回到 accept，也就不能处理其他客户端。因此单线程阻塞 IO 只能串行处理连接。

---

## Q11：一连接一线程模型有什么问题？

答：

一连接一线程实现简单，但线程数量随连接数线性增长。每个线程都有独立栈空间和调度开销，大量线程会消耗内存和内核资源，并导致严重上下文切换。detach 后线程生命周期也难管理，因此高并发服务一般不用一连接一线程模型。

---

## Q12：为什么需要 epoll？

答：

为了让少量线程同时管理大量连接。epoll 可以让一个线程同时监控多个 fd 的读写事件，只有 fd 就绪时才处理，避免为每个连接创建一个线程，也避免单线程阻塞在某一个 fd 上。

---

# 24. 今日易错点总结

```text
1. socket() 只是创建 socket fd，不是建立连接。
2. socket(AF_INET, SOCK_STREAM, 0) 表示创建 IPv4 TCP socket。
3. bind 负责绑定 IP/端口。
4. listen 负责进入监听状态。
5. accept 返回 connfd。
6. listenfd 不直接处理业务数据。
7. connfd 才负责 read/write。
8. sin_port 必须使用 htons。
9. 8080 变成 36895 是典型字节序错误。
10. read 返回 0 表示对端关闭。
11. read 不保证读到完整业务消息。
12. write 不保证一次写完全部数据。
13. buf 小不等于数据丢失。
14. 用户缓冲区不是 socket 固定资源。
15. socket 内核对象有接收缓冲区和发送缓冲区。
16. 单线程阻塞 IO 不能同时处理多个活跃连接。
17. 一连接一线程可以并发，但线程数量不可控。
18. connfd 在线程 lambda 中要值捕获，不要引用捕获。
19. detach 后线程生命周期不易管理。
20. 高并发服务通常需要 epoll / Reactor。
```

---

# 25. Day 10 总结

Day 10 的核心收获：

```text
socket：
    用户态访问网络通信能力的 fd

listenfd：
    负责监听和 accept 新连接

connfd：
    负责和具体客户端 read/write

服务端流程：
    socket → bind → listen → accept → read/write → close

阻塞 IO：
    read / write / accept 默认可能阻塞

缓冲区：
    read：内核接收缓冲区 → 用户缓冲区
    write：用户缓冲区 → 内核发送缓冲区

单线程阻塞 IO：
    简单，但不能同时处理多个活跃连接

一连接一线程：
    可以并发，但线程数量不可控

后续方向：
    epoll / Reactor / MiniNet
```

最重要的一句话：

> Linux TCP 服务端的基本单位是 fd：listenfd 负责接收连接，connfd 负责数据通信；阻塞 IO 写法简单，但无法高效处理大量连接，因此后续需要 epoll 和 Reactor。
