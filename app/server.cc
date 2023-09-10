#include "server.hh"
#include "hello.pb.h"
#include <unistd.h>
#include <stdint.h>

/*
auto main([[gnu::unused]] int argc, char *argv[]) -> int {
  rdma::Server s(argv[1], argv[2]);

  s.registerHandler(0, [](rdma::RPCHandle &handle) -> void {
    echo::Hello request;
    handle.getRequest(request);
    printf("receive request: \"%s\"\n", request.greeting().c_str());
    echo::Hello response;
    auto thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
    response.set_greeting("hi from server thread " + std::to_string(thread_id));
    printf("set response: \"%s\"\n", response.greeting().c_str());
    handle.setResponse(response);
  });

  return s.run();
}
*/
auto main([[gnu::unused]] int argc, char *argv[]) -> int {
  rdma::Server s(argv[1], argv[2]);

  s.registerHandler(0, [](rdma::RPCHandle &handle) -> void {
    echo::write_info w_info;
    handle.getRequest(w_info);
    void* buffer = nullptr;
    buffer = malloc(6);
    auto connection = static_cast<rdma::ConnCtx*>(&handle)->get_conn();
    connection->expose_memory(buffer, 6);
    connection->postRead(&handle, buffer, 6, connection->tmp_buff_lk(), (void*)(uintptr_t)w_info.data(), w_info.origin_key());
    
    echo::Hello response;
    std::string ss = (char*)buffer;
    response.set_greeting("now, op write ok!" + ss);
    handle.setResponse(response);
    connection->delete_tmp_mr();
  });

  s.registerHandler(1, [](rdma::RPCHandle &handle) -> void {
    echo::read_info r_info;
    handle.getRequest(r_info);
    char* buffer = (char*)malloc(10);
    strcpy(buffer, "hello,too");
    auto connection = static_cast<rdma::ConnCtx*>(&handle)->get_conn();
    connection->expose_memory(buffer, 10);
    connection->postWrite(&handle, buffer, 10, connection->tmp_buff_lk(), (void*)(uintptr_t)r_info.data(), r_info.origin_key());
    
    echo::Hello response;
    response.set_greeting("now, op read ok!");
    handle.setResponse(response);
    connection->delete_tmp_mr();
  });
  return s.run();
}