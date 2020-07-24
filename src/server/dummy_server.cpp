#include "dummy_server.h"

Config::Config(int argc, char** argv) { ParseArg(argc, argv); }

extern int optind, opterr, optopt;
extern char* optarg;

static struct option long_options[] = {
    {"user", required_argument, NULL, 'u'},
    {"passwd", required_argument, NULL, 'p'},
    {"dbname", required_argument, NULL, 'd'},
    {"sqlnum", required_argument, NULL, 's'},
    {"port", required_argument, NULL, 'P'},
    {"threadnum", required_argument, NULL, 't'},
    {"trigger", required_argument, NULL, 'T'}};

void Config::ParseArg(int argc, char** argv) {
  int index;
  int c = 0;
  if (argc < 8) {
    usage();
    exit(-1);
  }
  while (EOF != (c = getopt_long(argc, argv, "u:p:d:s:P:t:T:", long_options,
                                 &index))) {
    switch (c) {
      case 'u':
        sql_user_ = optarg;
        break;
      case 'p':
        sql_passwd_ = optarg;
        break;
      case 'd':
        db_name_ = optarg;
        break;
      case 's':
        sql_num_ = atoi(optarg);
        break;
      case 'P':
        port_ = atoi(optarg);
        break;
      case 't':
        thread_num_ = atoi(optarg);
        break;
      case 'T':
        triger_mode_ = (TrigerMode)atoi(optarg);
        break;
      case '?':
        fprintf(stderr, "Unknown option: %c\n", optopt);
        usage();
        exit(-1);
        break;
    }
  }
}

void Config::usage() {
  fprintf(stderr,
          "server [option]...\n"
          "   -u|--user       MySQL user name\n"
          "   -p|--passwd     MySQL user password\n"
          "   -d|--dbname     MySQL database name\n"
          "   -s|--sqlnum     MySQL connection number of connection pool\n"
          "   -P|--port       Server port\n"
          "   -t|--threadnum  Number thread of thread pool\n"
          "   -T|--trigger    Trigger mode of epoll, ET=0 LT=1\n");
}

static int __sig_sktpipefd_[2];  // 统一事件源，传输信号

DummyServer::DummyServer(Config config)
    : __users_(MAX_FD),
      __pool_(new Threadpool<HttpConn>(config.thread_num_)),
      __sql_user_(config.sql_user_),
      __sql_passwd_(config.sql_passwd_),
      __db_name_(config.db_name_) {
  __port_ = config.port_;
  __triger_mode_ = config.triger_mode_;
  __sql_num = config.sql_num_;
}

DummyServer::~DummyServer() {
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
  if (__triger_mode_ == ET) SetNonBlocking(__listenfd_);
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
  __SqlConnpool();
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
  if (__triger_mode_ == ET) {
    while (1) {
      int connfd = Accept(__listenfd_, &client_addr, &client_addrlen);
      if (connfd < 0) {
        if (errno != EAGAIN) PRINT_ERRMSG(__AddClient, "Accept failure");
        return;
      }
      if (HttpConn::user_cnt_ >= MAX_FD) {
        SendError(connfd, "Internal server busy");
        return;
      }
      /* 将新用户加入用户数组 */
      __users_[connfd].Init(connfd, client_addr);
    }
  } else {
    int connfd = Accept(__listenfd_, &client_addr, &client_addrlen);
    if (connfd < 0) {
      if (errno != EAGAIN) PRINT_ERRMSG(__AddClient, "Accept failure");
      return;
    }
    if (HttpConn::user_cnt_ >= MAX_FD) {
      SendError(connfd, "Internal server busy");
      return;
    }
    /* 将新用户加入用户数组 */
    __users_[connfd].Init(connfd, client_addr);
  }
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
    __pool_->Append(&__users_[sockfd]);
  } else {
    __users_[sockfd].CloseConn();
  }
}

void DummyServer::__WriteToClient(int sockfd) {
  /* Proactor 模式，父线程负责读写，子线程负责处理逻辑 */
  /* 根据写的结果，决定是添加任务还是关闭连接 */
  if (!__users_[sockfd].Write()) {
    __users_[sockfd].CloseConn();
  }
}

void DummyServer::__SqlConnpool() {
  SqlConnpool::Init("localhost", __sql_user_, __sql_passwd_, __db_name_, 3306,
                    __sql_num);
  HttpConn::InitSqlResult();
}
