#include "dummy_server.h"

vector<TimerClientData> g_timer_client_data(MAX_FD);  // 定时器用的用户数据
TimerHeap g_timer_heap(MAX_FD);                       // 堆定时器

Config::Config(int argc, char** argv) {
  verbose_ = false;
  log_path_ = "./";
  ParseArg(argc, argv);
}

extern int optind, opterr, optopt;
extern char* optarg;

static struct option long_options[] = {
    {"user", required_argument, NULL, 'u'},
    {"passwd", required_argument, NULL, 'p'},
    {"dbname", required_argument, NULL, 'd'},
    {"sqlnum", required_argument, NULL, 's'},
    {"port", required_argument, NULL, 'P'},
    {"threadnum", required_argument, NULL, 't'},
    {"trigger", required_argument, NULL, 'T'},
    {"verbose", no_argument, NULL, 'v'},
    {"logpath", required_argument, NULL, 'L'}};

void Config::ParseArg(int argc, char** argv) {
  int index;
  int c = 0;
  if (argc < 8) {
    usage();
    exit(-1);
  }
  while (EOF != (c = getopt_long(argc, argv, "u:p:d:s:P:t:T:vL:", long_options,
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
        trigger_mode_ = (TriggerMode)atoi(optarg);
        break;
      case 'v':
        verbose_ = true;
        break;
      case 'L':
        log_path_ = optarg;
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
          "   -T|--trigger    Trigger mode of epoll, ET=0 LT=1\n"
          "   -v|--verbose    output information\n"
          "   -L|--logpath    log path\n");
}

static int __sig_sktpipefd_[2];  // 统一事件源，传输信号

DummyServer::DummyServer(const Config& config)
    : __port_(config.port_),
      __users_(MAX_FD),
      __pool_(new Threadpool<HttpConn>(config.thread_num_)),
      __trigger_mode_(config.trigger_mode_),
      __sql_user_(config.sql_user_),
      __sql_passwd_(config.sql_passwd_),
      __db_name_(config.db_name_),
      __sql_num(config.sql_num_) {
  extern const char* doc_root;
  HttpConn::InitStaticResource(doc_root);
}

DummyServer::~DummyServer() {
  if (close(__epollfd_) < 0 || close(__listenfd_) < 0) {
    LOGERR("close error");
    exit(-1);
  }
  HttpConn::ReleaseStaticResource();
}

/* 信号处理函数，sig 为待处理信号 */
void DummyServer::__SigHandler(int sig) {
  // 保存 errno，防止在其他函数读取 errno 前触发了信号处理函数
  int errno_copy = errno;
  int msg = sig;
  if (send(__sig_sktpipefd_[1], (char*)&msg, 1, 0) < 0) {
    LOGERR("send error");
    exit(-1);
  }
  errno = errno_copy;
}

/* 创建监听事件与 epoll 内核事件表 */
void DummyServer::__Listen() {
  /* 创建监听描述符 */
  __listenfd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (__listenfd_ < 0) {
    LOGERR("socket error");
    exit(-1);
  }
  struct linger tmp = {1, 0};
  setsockopt(__listenfd_, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

  struct sockaddr_in addr;
  bzero(&addr, sizeof(addr));
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(__port_);
  addr.sin_family = AF_INET;
  if (bind(__listenfd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
    LOGERR("bind error");
    exit(-1);
  }
  if (listen(__listenfd_, 5) < 0) {
    LOGERR("listen error");
    exit(-1);
  }

  /* 创建 epoll 内核事件表 */
  __epollfd_ = epoll_create(5);
  if (__epollfd_ < 0) {
    LOGERR("epoll_create error");
    exit(-1);
  }
  if (AddFd(__epollfd_, __listenfd_, false, __trigger_mode_) < 0) {
    LOGERR("AddFd error");
    exit(-1);
  }
  HttpConn::epollfd_ = __epollfd_;

  /* 统一事件源 */
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, __sig_sktpipefd_) < 0) {
    LOGERR("socketpair error");
    exit(-1);
  }
  if (SetNonBlocking(__sig_sktpipefd_[1]) < 0) {  // 非阻塞写
    LOGERR("SetNonBlocking error");
    exit(-1);
  }
  if (AddFd(__epollfd_, __sig_sktpipefd_[0], false, __trigger_mode_) < 0) {
    LOGERR("AddFd error");
    exit(-1);
  }

  if (AddSig(SIGTERM, __SigHandler, false) < 0 ||
      AddSig(SIGINT, __SigHandler, false) < 0 ||
      AddSig(SIGALRM, __SigHandler, true) || AddSig(SIGPIPE, SIG_IGN) < 0) {
    LOGERR("AddSig error");
    exit(-1);
  }
  alarm(TIMEOUT);
}

/* 启动服务器 */
void DummyServer::Start() {
  __SqlConnpool();
  __Listen();

  __stop_server_ = false;

  while (!__stop_server_) {
    int num = epoll_wait(__epollfd_, __events_, MAX_EVENT_NUM, -1);
    if (num < 0 && (errno != EINTR)) {
      LOGERR("epoll_wait error");
      exit(-1);
    }
    for (int i = 0; i < num; ++i) {
      int sockfd = __events_[i].data.fd;

      if (sockfd == __listenfd_) {
        /* 新连接 */
        __AddClient();
      } else if (__events_[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
        /* 异常，移除定时器，关闭连接 */
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
  if (__trigger_mode_ == ET) {
    while (1) {
      int connfd =
          accept(__listenfd_, (sockaddr*)&client_addr, &client_addrlen);
      if (connfd < 0) {
        if (errno != EAGAIN) LOGERR("accept error");
        return;
      }
      if (HttpConn::user_cnt_ >= MAX_FD) {
        if (SendError(connfd, "Internal server busy") < 0)
          LOGWARN("SendError error");
        return;
      }
      /* 将新用户加入用户数组 */
      __users_[connfd].Init(connfd, client_addr);
      /* 设置定时器 */
      __SetTimer(connfd, client_addr);
    }
  } else {
    int connfd = accept(__listenfd_, (sockaddr*)&client_addr, &client_addrlen);
    if (connfd < 0) {
      if (errno != EAGAIN) LOGERR("accept error");
      return;
    }
    if (HttpConn::user_cnt_ >= MAX_FD) {
      if (SendError(connfd, "Internal server busy") < 0)
        LOGWARN("SendError error");
      return;
    }
    /* 将新用户加入用户数组 */
    __users_[connfd].Init(connfd, client_addr);
    /* 设置定时器 */
    __SetTimer(connfd, client_addr);
  }
}

void DummyServer::__SignalProcess() {
  int ret = -1;
  char signals[1024];
  ret = recv(__sig_sktpipefd_[0], signals, sizeof(signals), 0);
  if (ret <= 0) {
    LOGERR("recv error");
    exit(-1);
  } else {
    for (int i = 0; i < ret; ++i) {
      switch (signals[i]) {
        case SIGALRM:
          g_timer_heap.Tick();
          alarm(TIMEOUT);
          break;
        case SIGTERM:
        case SIGINT:
          printf("Stop server now...\n");
          if (close(__sig_sktpipefd_[0]) < 0 ||
              close(__sig_sktpipefd_[1]) < 0) {
            LOGERR("close error");
            exit(-1);
          }
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
    __ResetTimer(sockfd);
    __pool_->Append(&__users_[sockfd]);
  } else {
    __users_[sockfd].CloseConn();
  }
}

void DummyServer::__WriteToClient(int sockfd) {
  /* Proactor 模式，父线程负责读写，子线程负责处理逻辑 */
  /* 根据写的结果，决定是添加任务还是关闭连接 */
  if (__users_[sockfd].Write()) {
    __ResetTimer(sockfd);
  } else {
    __users_[sockfd].CloseConn();
  }
}

void DummyServer::__SqlConnpool() {
  SqlConnpool::Init("localhost", __sql_user_, __sql_passwd_, __db_name_, 3306,
                    __sql_num);
  HttpConn::InitSqlResult();
}

/* 设置 TimerClientData 数据和定时器 */
void DummyServer::__SetTimer(int sockfd, sockaddr_in client_addr) {
  g_timer_client_data[sockfd].addr = client_addr;
  g_timer_client_data[sockfd].epollfd = __epollfd_;
  g_timer_client_data[sockfd].sockfd = sockfd;
  auto timer = std::make_shared<Timer>(TIMEOUT);
  timer->user_data_ = &g_timer_client_data[sockfd];
  timer->cb_func_ = __TimerCallback;
  g_timer_client_data[sockfd].timer = timer;
  g_timer_heap.AddTimer(timer);
}

void DummyServer::__TimerCallback(TimerClientData* timer_client_data) {
  if (epoll_ctl(timer_client_data->epollfd, EPOLL_CTL_DEL,
                timer_client_data->sockfd, NULL) < 0) {
    LOGERR("epoll_ctl error");
    exit(-1);
  }
  if (close(timer_client_data->sockfd) < 0) {
    LOGERR("close error");
    exit(-1);
  }
  --HttpConn::user_cnt_;
}

/* 若连接还是活动状态，则重设定时器 */
void DummyServer::__ResetTimer(int sockfd) {
  auto old_timer = g_timer_client_data[sockfd].timer;
  g_timer_heap.DelTimer(old_timer);
  auto new_timer = std::make_shared<Timer>(TIMEOUT);
  new_timer->user_data_ = old_timer->user_data_;
  new_timer->cb_func_ = __TimerCallback;
  g_timer_client_data[sockfd].timer = new_timer;
  g_timer_heap.AddTimer(new_timer);
}