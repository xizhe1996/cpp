#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <iostream>
#include <thread>

void handleClient(int connfd) {
  char buf[1024];

  while (true) {
    ssize_t n = read(connfd, buf, sizeof(buf));

    if (n > 0) {
      write(connfd, buf, n);
    } else if (n == 0) {
      std::cout << "client closed\n";
      break;
    } else {
      perror("read");
      break;
    }
  }
}

int main() {
  // 1. 创建 listenfd
  int listenfd = socket(AF_INET, SOCK_STREAM, 0);

  if (listenfd < 0) {
    perror("socket");
    return 1;
  }

  // 2. 设置 sockaddr_in
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(8080);

  // 3. bind
  int ret = bind(listenfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
  if (ret < 0) {
    perror("bind");
    close(listenfd);
    return 1;
  }

  // 4. listen
  ret = listen(listenfd, 128);
  if (ret < 0) {
    perror("listen");
    close(listenfd);
    return 1;
  }

  std::cout << "server listening on port 8080" << std::endl;

  // 5. accept
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

  // 8. close
  close(listenfd);

  return 0;
}