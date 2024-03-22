cmake_minimum_required(VERSION 3.0.0)
project(EPOLL_READ_EVEN_AND_SENDTO VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
enable_testing()

include_directories(${PROJECT_SOURCE_DIR})
add_executable(UDP_SER udp/server.cc Constant.h)
add_executable(UDP_CLI udp/client.cc Constant.h)
add_executable(UDP_SER_INC udp/server_increase_recv_buff.cc Constant.h)
add_executable(TCP_SER tcp/server.cc Constant.h)
add_executable(TCP_CLI tcp/client.cc Constant.h)


set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)


