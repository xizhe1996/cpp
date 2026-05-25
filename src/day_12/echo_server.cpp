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

  setNonBlock(listenfd);

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
