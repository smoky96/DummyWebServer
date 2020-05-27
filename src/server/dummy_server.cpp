#include "dummy_server.h"

static int __sig_sktpipefd_[2];  // 统一事件源，传输信号

DummyServer::DummyServer(Config config)
    : __users_(MAX_FD), __pool_(new threadpool<HttpConn>(config.thread_num_)) {
  __port_ = config.port_;

  __root_ = (char*)malloc(strlen(config.root_) + 1);
  if (!__root_) throw std::bad_alloc();
  strcpy(__root_, config.root_);

  __triger_mode_ = ET;
}

DummyServer::~DummyServer() {
  free(__root_);
  Close(__epollfd_);
  Close(__listenfd_);
}

/* 信号处理函数，sig 为待处理信号 */
void DummyServer::__SigHandler(int sig) {
  // 保存 errno，防止在其他函数读取 errno 前触发了信号处理函数
  int errno_copy = errno;
  int msg = sig;
  Send(__sig_sktpipefd_[1], (char*)&msg, 1, 0);
  errno = errno_copy;
}

/* 创建监听事件与 epoll 内核事件表 */
void DummyServer::__Listen() {
  /* 创建监听描述符 */
  __listenfd_ = Socket(AF_INET, SOCK_STREAM, 0);
  struct linger tmp = {1, 0};
  setsockopt(__listenfd_, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

  struct sockaddr_in addr;
  bzero(&addr, sizeof(addr));
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(__port_);
  addr.sin_family = AF_INET;
  Bind(__listenfd_, &addr, sizeof(addr));
  Listen(__listenfd_, 5);

  /* 创建 epoll 内核事件表 */
  __epollfd_ = Epoll_create(5);
  AddFd(__epollfd_, __listenfd_, false, __triger_mode_);
  HttpConn::epollfd_ = __epollfd_;

  /* 统一事件源 */
  Socketpair(AF_UNIX, SOCK_STREAM, 0, __sig_sktpipefd_);
  SetNonBlocking(__sig_sktpipefd_[1]);  // 非阻塞写
  AddFd(__epollfd_, __sig_sktpipefd_[0], false, __triger_mode_);
  AddSig(SIGTERM, __SigHandler, false);
  AddSig(SIGINT, __SigHandler, false);

  /* 忽略 SIGPIPE 信号 */
  AddSig(SIGPIPE, SIG_IGN);
}

/* 启动服务器 */
void DummyServer::Start() {
  __Listen();

  __stop_server_ = false;

  while (!__stop_server_) {
    int num = Epoll_wait(__epollfd_, __events_, MAX_EVENT_NUM, -1);
    for (int i = 0; i < num; ++i) {
      int sockfd = __events_[i].data.fd;

      if (sockfd == __listenfd_) {
        /* 新连接 */
        __AddClient();
      } else if (__events_[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
        /* 异常，关闭连接 */
        __users_[sockfd].CloseConn();
      } else if ((sockfd == __sig_sktpipefd_[0]) &&
                 (__events_[i].events & EPOLLIN)) {
        __SignalProcess();
      } else if (__events_[i].events & EPOLLIN) {
        __ReadFromClient(sockfd);
      } else if (__events_[i].events & EPOLLOUT) {
        __WriteToClient(sockfd);
      }
    }
  }
}

void DummyServer::__AddClient() {
  struct sockaddr_in client_addr;
  socklen_t client_addrlen = sizeof(client_addr);
  int connfd = Accept(__listenfd_, &client_addr, &client_addrlen);
  if (HttpConn::user_cnt_ >= MAX_FD) {
    SendError(connfd, "Internal server busy");
    return;
  }
  /* 将新用户加入用户数组 */
  __users_[connfd].Init(connfd, client_addr);
}

void DummyServer::__SignalProcess() {
  int ret = -1;
  char signals[1024];
  ret = Recv(__sig_sktpipefd_[0], signals, sizeof(signals), 0);
  if (ret <= 0) {
    PRINT_ERRMSG(__SignalProcess, "Failed to pick up the signal");
    return;
  } else {
    for (int i = 0; i < ret; ++i) {
      switch (signals[i]) {
        case SIGTERM:
        case SIGINT:
          printf("Stop server now...\n");
          Close(__sig_sktpipefd_[0]);
          Close(__sig_sktpipefd_[1]);
          __stop_server_ = true;
          break;
        default:
          break;
      }
    }
  }
}

void DummyServer::__ReadFromClient(int sockfd) {
  /* Proactor 模式，父线程负责读写，子线程负责处理逻辑 */
  /* 根据读的结果，决定是添加任务还是关闭连接 */
  if (__users_[sockfd].Read()) {
    __pool_->append(&__users_[sockfd]);
  } else {
    SendError(sockfd, "Internal read error");
    __users_[sockfd].CloseConn();
  }
}

void DummyServer::__WriteToClient(int sockfd) {
  /* Proactor 模式，父线程负责读写，子线程负责处理逻辑 */
  /* 根据写的结果，决定是添加任务还是关闭连接 */
  if (!__users_[sockfd].Write()) {
    SendError(sockfd, "Internal write error");
    __users_[sockfd].CloseConn();
  }
}
