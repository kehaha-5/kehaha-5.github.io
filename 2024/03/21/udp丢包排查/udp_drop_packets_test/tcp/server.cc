#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <netinet/in.h>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <ostream>
#include <thread>

#include "Constant.h"

uint64_t hasRevc_ = 0;
uint64_t hasRevcSize_ = 0;

void showInfo() {
  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    std::cout << "hasRevc:" << hasRevc_;
    std::cout << " hasRevcSize:" << hasRevcSize_ << std::endl;
  }
}

int main(int argc, const char **argv) {
  // create socket
  auto socketfd = socket(AF_INET, SOCK_STREAM, 0);

  if (socketfd == -1) {
    perror("socket error");
    exit(EXIT_FAILURE);
  }

  auto host = "0.0.0.0";
  auto port = 9999;

  struct sockaddr_in servaddr;
  bzero(&servaddr, sizeof(servaddr));
  servaddr.sin_family = AF_INET;

  if (inet_aton(host, &servaddr.sin_addr) == -1) {
    perror("inet_aton error");
    exit(EXIT_FAILURE);
  }

  servaddr.sin_port = htons(port);

  if (bind(socketfd, (const struct sockaddr *)&servaddr, sizeof servaddr) ==
      -1) {
    perror("bind error");
    exit(EXIT_FAILURE);
  }

  int reuse = 1;

  if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse,
                 sizeof(int)) == -1) {
    perror("set reuse");
    exit(EXIT_FAILURE);
  }

  if (listen(socketfd, 4096) == -1) {
    perror("listen");
    exit(EXIT_FAILURE);
  }

  std::cout << "tcp bind is listenning ... port " << port << std::endl;

  auto thread = std::thread(showInfo);
  thread.detach();

  struct sockaddr clientAddr = {};
  socklen_t clientLen = sizeof(clientAddr);
  auto acceptfd = accept(socketfd, &clientAddr, &clientLen);
  std::string data(MAX_MSG_LENGTH, '0');
  ssize_t len = 0;
  do {
    len = recv(acceptfd, &data[0], MAX_MSG_LENGTH, 0);
    hasRevc_++;
    hasRevcSize_ += len;
    data.clear();
  } while (len > 0);
}
