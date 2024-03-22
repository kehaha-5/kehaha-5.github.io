#include <arpa/inet.h>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "Constant.h"

struct sockaddr_in _serveraddr;
int _socketfd;
uint64_t _hasSendPacketsNum = 0;

void showInfo() {
  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    std::cout << "hasSendPacketsNum:" << _hasSendPacketsNum << std::endl;
  }
}

void sendMsg() {
  std::string msg(MAX_MSG_LENGTH, '0');
  int res = send(_socketfd, msg.c_str(), msg.size(), 0);
  if (res == -1) {
    if (!(errno == EAGAIN || errno == EWOULDBLOCK)) {
      perror("send msg ");
      exit(EXIT_FAILURE);
    }
  }
  _hasSendPacketsNum++;
}

int main(int argc, const char **argv) {
  if (argc < 2) {
    fprintf(stderr, "must be one arg \n");
    return 0;
  }

  _socketfd = socket(AF_INET, SOCK_STREAM, 0);
  if (_socketfd == -1) {
    perror("socket ");
    exit(EXIT_FAILURE);
  }

  auto host = "127.0.0.1";
  auto port = 9999;

  struct sockaddr_in servaddr;
  bzero(&servaddr, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(port);
  if (connect(_socketfd, (const struct sockaddr *)&servaddr, sizeof servaddr) ==
      -1) {
    perror("connect ");
    exit(EXIT_FAILURE);
  }
  _serveraddr = servaddr;

  auto thread = std::thread(showInfo);
  thread.detach();

  auto loop = MAX_SEND_PACKETS_LOOP;
  while (loop) {
    auto peer_seconde = std::atoi(argv[1]);
    auto start = std::chrono::system_clock::now();
    while (peer_seconde) {
      sendMsg();
      peer_seconde--;
    }
    auto end = std::chrono::system_clock::now();
    auto d = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    if (d.count() < 1000) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000 - d.count()));
    }
    loop--;
  }

  std::cout << "total hasSendPacketsNum:" << _hasSendPacketsNum
            << " hasSendSize:"
            << uint64_t(MAX_SEND_PACKETS_LOOP) * uint64_t(MAX_MSG_LENGTH) *
                   uint64_t(std::atoi(argv[1]))
            << std::endl;
}
