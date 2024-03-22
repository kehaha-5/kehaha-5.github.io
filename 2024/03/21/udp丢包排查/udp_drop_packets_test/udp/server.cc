#include <arpa/inet.h>
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

void showInfo() {
  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    std::cout << "hasRevc:" << hasRevc_ << std::endl;
  }
}

int main(int argc, const char** argv) {
  // create socket
  auto socketfd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);

  if (socketfd == -1) {
    perror("socket error");
    exit(EXIT_FAILURE);
  }

  auto host = "0.0.0.0";
  auto port = 8888;

  struct sockaddr_in servaddr;
  bzero(&servaddr, sizeof(servaddr));
  servaddr.sin_family = AF_INET;

  if (inet_aton(host, &servaddr.sin_addr) == -1) {
    perror("inet_aton error");
    exit(EXIT_FAILURE);
  }

  servaddr.sin_port = htons(port);

  if (bind(socketfd, (const struct sockaddr*)&servaddr, sizeof servaddr) ==
      -1) {
    perror("bind error");
    exit(EXIT_FAILURE);
  }

  std::cout << "udp bind is listenning ... port " << port << std::endl;

  auto thread = std::thread(showInfo);
  thread.detach();

  while (true) {
    std::string data(MAX_MSG_LENGTH, '0');
    struct sockaddr_in clientAddr = {};
    int clientLen = sizeof(clientAddr);
    int len = recvfrom(socketfd, &data[0], MAX_MSG_LENGTH, 0,
                       (struct sockaddr*)&clientAddr, (socklen_t*)&clientLen);
    hasRevc_++;
  }
}
