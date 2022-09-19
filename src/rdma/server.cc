#include "server.hh"
#include <signal.h>

namespace rdma {

Server::Server(const char *host, const char *port) {
  int ret = 0;
  ret = getaddrinfo(host, port, nullptr, &addr_);
  check(ret, "fail to parse host and port");

  info("listening address: %s:%s", host, port);

  ec_ = rdma_create_event_channel();
  checkp(ec_, "fail to create event channel");
  ret = rdma_create_id(ec_, &cm_id_, nullptr, RDMA_PS_TCP);
  check(ret, "fail to create cm id");

  info("initialize event channel and identifier");

  ret = rdma_bind_addr(cm_id_, addr_->ai_addr);
  check(ret, "fail to bind address");
  ret = rdma_listen(cm_id_, default_back_log);
  check(ret, "fail to listen for connections");

  info("bind address and begin listening for connection requests");

  ret = evthread_use_pthreads();
  check(ret, "fail to open multi-thread support for libevent");
  base_ = event_base_new();
  checkp(base_, "fail to create event loop base");
  conn_event_ = event_new(base_, ec_->fd, EV_READ | EV_PERSIST,
                          &Server::onConnEvent, this);
  checkp(conn_event_, "fail to create connection event");
  ret = event_add(conn_event_, nullptr);
  check(ret, "fail to register connection event");
  exit_event_ = event_new(base_, SIGINT, EV_SIGNAL, &Server::onExit, this);
  checkp(exit_event_, "fail to create exit event");
  ret = event_add(exit_event_, nullptr);
  check(ret, "fail to register exit event");
  info("register all events into event loop");
}

auto Server::run() -> int {
  info("event loop running");
  return event_base_dispatch(base_);
}

auto Server::handleConnEvent() -> void {
  rdma_cm_event *e;
  if (rdma_get_cm_event(ec_, &e) != 0) {
    info("fail to get cm event");
    return;
  }

  if (e->status != 0) {
    info("got a bad event");
    auto ret = rdma_ack_cm_event(e);
    warn(ret, "fail to ack event");
    return;
  }

  auto client_id = e->id;

  switch (e->event) {
  case RDMA_CM_EVENT_CONNECT_REQUEST: { // throw when bad connection
    info("handle a connection request");

    auto ctx = new ConnWithCtx(this, client_id);

    auto ret = rdma_accept(client_id, &ctx->param_);
    check(ret, "fail to accept connection");

    ctx->remote_buffer_key_ = *(uint32_t *)e->param.conn.private_data;

    ret = rdma_ack_cm_event(e);
    warn(ret, "fail to ack event");

    client_id->context = ctx;
    info("accept the connection");

#ifdef USE_NOTIFY
    ret = ctx->registerCompEvent(base_);
    check(ret, "fail to register completion event");
#endif

    info("register the connection completion event");
    break;
  }
  case RDMA_CM_EVENT_ESTABLISHED: {
    auto ret = rdma_ack_cm_event(e);
    warn(ret, "fail to ack event");
    info("establish a connection");
    break;
  }
  case RDMA_CM_EVENT_DISCONNECTED: {
    auto ret = rdma_ack_cm_event(e);
    warn(ret, "fail to ack event");
    delete reinterpret_cast<ConnWithCtx *>(client_id->context);
    info("close a connection");
    break;
  }
  default: {
    info("unexpected event: %s", rdma_event_str(e->event));
    break;
  }
  }
}

auto Server::registerHandler(uint32_t id, Handler fn) -> void {
  assert(handlers_.find(id) == handlers_.end());
  handlers_[id] = fn;
}

auto Server::getHandler(uint32_t id) -> Handler { return handlers_.at(id); }

auto Server::onConnEvent([[gnu::unused]] int fd, [[gnu::unused]] short what,
                         void *arg) -> void {
  reinterpret_cast<Server *>(arg)->handleConnEvent();
}

auto Server::onExit(int fd, short what, void *arg) -> void {
  Server *s = reinterpret_cast<Server *>(arg);
  auto ret = event_base_loopbreak(s->base_);
  check(ret, "fail to stop event loop");
  info("stop event loop");
}

Server::~Server() {
  event_base_free(base_);
  event_free(conn_event_);
  event_free(exit_event_);

  auto ret = rdma_destroy_id(cm_id_);
  warn(ret, "fail to destroy cm id");
  rdma_destroy_event_channel(ec_);

  freeaddrinfo(addr_);

  info("cleanup the local resources");
}

Server::Context::Context(Conn *conn, void *buffer, uint32_t length)
    : ConnCtx(0, conn, buffer, length) {}

Server::Context::~Context() {}

auto Server::Context::prepare() -> void {
  state_ = WaitingForBufferMeta;
  conn_->postRecv(this, rawBuf(), headerLength(), conn_->loaclKey());
}

auto Server::Context::advance(const ibv_wc &wc) -> void {
  switch (wc.opcode) {
  case IBV_WC_RECV: {
    assert(state_ == WaitingForBufferMeta);
    state_ = ReadingRequest;
    conn_->postRead(this, rawBuf(), readableLength(), conn_->loaclKey(),
                    header().addr_, conn_->remoteKey());
    break;
  }
  case IBV_WC_RDMA_READ: {
    assert(state_ == ReadingRequest);
    state_ = FilledWithRequest;
    reinterpret_cast<ConnWithCtx *>(conn_)->pending_ctx_.push(this);
    break;
  }
  case IBV_WC_RDMA_WRITE: {
    assert(state_ == WritingResponse);
    state_ = Vacant;
    prepare();
    break;
  }
  default: {
    info("unexpected wc opcode: %d", wc.opcode);
    break;
  }
  }
}

auto Server::Context::handler() -> void {
  static_cast<ConnWithCtx *>(conn_)->s_->getHandler(header().rpc_id_)(*this);
  state_ = WritingResponse;
  conn_->postWriteImm(this, rawBuf(), readableLength(), conn_->loaclKey(),
                      header().addr_, conn_->remoteKey(), header().ctx_id_);
}

Server::ConnWithCtx::ConnWithCtx(Server *s, rdma_cm_id *id)
    : Conn(id, max_context_num), s_(s) {
  for (uint32_t i = 0; i < max_context_num; i++) {
    handle_ctx_[i] = new Context(this, bufferPage(i), buffer_page_size);
    handle_ctx_[i]->prepare(); // only receiver need prepare for request
  }
  serving_ = true;
  bg_handler_ = new std::thread(&ConnWithCtx::serve, this);
}

auto Server::ConnWithCtx::serve() -> void {
  info("bg handler running");
  Context *ctx;
  while (serving_.load(std::memory_order_relaxed)) {
    if (pending_ctx_.tryPop(ctx)) {
      ctx->handler();
    } else {
      pause();
    }
  }
  info("bg handler exits");
}

Server::ConnWithCtx::~ConnWithCtx() {
  for (auto p : handle_ctx_) {
    delete p;
  }
  serving_ = false;
  bg_handler_->join();
  delete bg_handler_;
}

} // namespace rdma