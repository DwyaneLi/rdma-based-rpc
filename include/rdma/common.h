#ifndef __RDMA_EXAMPLE_COMMON__
#define __RDMA_EXAMPLE_COMMON__

#include "util.h"
#include <array>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <event2/event.h>
#include <event2/thread.h>
#include <functional>
#include <infiniband/verbs.h>
#include <netdb.h>
#include <rdma/rdma_cma.h>
#include <thread>

namespace rdma {

class ConnCtx;

class Conn {
  friend class Client;
  friend class Server;
  friend class ConnCtx;
  friend class ClientSideCtx;
  friend class ServerSideCtx;

public:
  // constexpr static uint64_t default_buffer_size = 4 * 1024 * 1024; // 4MB
  constexpr static uint32_t cq_capacity = 64;
  constexpr static uint32_t buffer_page_size = 1024; // 1KB
  using BufferPage = char[buffer_page_size];

private:
  constexpr static auto defaultQpInitAttr() -> ibv_qp_init_attr {
    return {
        .cap =
            {
                .max_send_wr = cq_capacity * 2,
                .max_recv_wr = cq_capacity * 2,
                .max_send_sge = 1,
                .max_recv_sge = 1,
            },
        .qp_type = IBV_QPT_RC,
        .sq_sig_all = 0,
    };
  }

public:
  Conn(rdma_cm_id *id, uint32_t n_buffer_page);
  ~Conn();

#ifdef USE_NOTIFY
public:
  auto registerCompEvent(::event_base *base) -> int;

private:
  static auto onWorkComp(int fd, short what, void *arg) -> void;
#endif

public:
  auto poll() -> void;

public:
  auto postRecv(void *ctx, void *local_addr, uint32_t length, uint32_t lkey)
      -> void;
  auto postSend(void *ctx, void *local_addr, uint32_t length, uint32_t lkey)
      -> void;
  auto postRead(void *ctx, void *local_addr, uint32_t length, uint32_t lkey,
                void *remote_addr, uint32_t rkey) -> void;
  auto postWriteImm(void *ctx, void *local_addr, uint32_t length, uint32_t lkey,
                    void *remote_addr, uint32_t rkey) -> void;

public:
  auto qpState() -> void;
  auto bufferPage(uint32_t id) -> void *;

private:
  static auto onRecv(int fd, short what, void *arg) -> void;

private:
  rdma_cm_id *id_{nullptr};
  ibv_qp *qp_{nullptr};
  ibv_pd *pd_{nullptr};
  ibv_cq *cq_{nullptr};
  union {
    void *buffer_{nullptr};
    BufferPage *buffer_pages_;
  };
  uint32_t n_buffer_page_{0};
  ibv_mr *buffer_mr_{nullptr};
  uint32_t remote_buffer_key_{0};

#ifdef USE_NOTIFY
  ::event *comp_event_{nullptr};
  ibv_comp_channel *cc_{nullptr};
#endif

  rdma_conn_param param_{};

#ifdef USE_POLL
  std::atomic_bool running_{false};
  std::thread *bg_poller_{nullptr};
#endif
};

class [[gnu::packed]] Meta {
public:
  void *addr_;
  uint64_t length_;
};

class ConnCtx {
public:
  ConnCtx(Conn *conn, void *buffer = nullptr, uint64_t length = 0);

public:
  virtual ~ConnCtx() = default;
  virtual auto advance(int32_t finished_op) -> void = 0;

public:
  auto buffer() -> void *;

protected:
  Conn *conn_{nullptr}; // created in which Conn
  union {
    struct {
      void *buffer_{nullptr}; // do not own
      uint64_t length_{0};
    };
    Meta local_;
  };
  TIMER;
};

} // namespace rdma

#endif