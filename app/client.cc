#include "client.hh"
#include "hello.pb.h"

#include <thread>
#include <unistd.h>
#include <iostream>
#include <stdint.h>
/*
auto main([[gnu::unused]] int argc, char *argv[]) -> int {
  rdma::Client c;

  auto conn_id_1 = c.connect(argv[1], argv[2]);
  auto conn_id_2 = c.connect(argv[1], argv[2]);

  auto fn = [&c](uint32_t conn_id) {
    echo::Hello request;
    echo::Hello response;
    rdma::Status s;
    for (int i = 0; i < 1000; i++) {
      request.set_greeting("hello from " + std::to_string(i));
      printf("send request: \"%s\"\n", request.greeting().c_str());
      s = c.call(conn_id, 0, request, response);
      if (not s.ok()) {
        printf("%s\n", s.whatHappened());
        break;
      }
      printf("receive response: \"%s\"\n", response.greeting().c_str());
    }
  };

  std::thread t2(fn, conn_id_2);
  std::thread t3(fn, conn_id_1);
  std::thread t4(fn, conn_id_2);
  fn(conn_id_1);
  t2.join();
  t3.join();
  t4.join();
  return 0;
}

*/
auto main([[gnu::unused]] int argc, char *argv[]) -> int {
  rdma::Client c;

  auto conn_id_1 = c.connect(argv[1], argv[2]);

  char* write_buff = (char*)malloc(6);
  strcpy(write_buff, "hello");

  void* read_buff = malloc(10);

  std::cout << "test wirte" << std::endl;
  auto m_ifo_1 = c.expose_memory(conn_id_1, write_buff, 6);
  if(m_ifo_1.err_) {
    std::cout << "[ERROR] expose memory error : " << m_ifo_1.err_ << std::endl;
    return 0;
  }
  echo::Hello response;
  echo::write_info w_info;
  w_info.set_data((uint64_t)(uintptr_t)write_buff);
  w_info.set_size(6);
  w_info.set_origin_key(m_ifo_1.remote_buffer_key_);
  rdma::Status s;
  if(s.none()) {
    std::cout << "None!" << std::endl;
  }
  s = c.call(conn_id_1, 0, w_info, response);
  if(not s.ok()) {
    std::cout << s.whatHappened() << std::endl;
  }
  printf("receive response: \"%s\"\n", response.greeting().c_str());
  auto res = c.delete_tmp_mr(conn_id_1);
  if(res != 0) {
    std::cout << "delete mr error" << std::endl;
  }

  auto m_ifo_2 = c.expose_memory(conn_id_1, read_buff, 10);
  if(m_ifo_2.err_) {
    std::cout << "expose memory error" << std::endl;
    return 0;
  }
  echo::read_info r_info;
  r_info.set_data((uint64_t)(uintptr_t)read_buff);
  r_info.set_size(10);
  r_info.set_origin_key(m_ifo_2.remote_buffer_key_);
  s = c.call(conn_id_1, 1, r_info, response);
    if(not s.ok()) {
    std::cout << s.whatHappened() << std::endl;
  }
  std::cout << (char*)read_buff << std::endl;
  printf("receive response: \"%s\"\n", response.greeting().c_str());
  res = c.delete_tmp_mr(conn_id_1);
  if(res != 0) {
    std::cout << "delete mr error" << std::endl;
  }

  return 0;
}
