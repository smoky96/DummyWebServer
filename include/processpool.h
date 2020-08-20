#ifndef __PROCESSPOOL__H__
#define __PROCESSPOOL__H__

#include <atomic>
#include <cassert>
#include <vector>

#include "common.h"
#include "locker.h"

using std::vector;

/* 描述子进程的类 */
class process {
 public:
  pid_t pid;         // 进程 PID
  int sktpipefd[2];  // 与父进程通信的管道

  process() { pid = -1; }
};

/* 对 Processpool 中的 __instance_ 上锁 */
static Locker __instance_locker = Locker();

/** 进程池类模板
 * T: 处理逻辑任务的类
 */
template <typename T>
class Processpool {
 private:
  static const int kMaxProcessNum = 16;  // 线程池最大子线程数量
  static const int kUserPerProcess = 65535;  // 每个子进程最多处理的客户端数量
  static const int kMaxEventNum = 10000;  // epoll 最多能处理的事件数

  int __process_number_;  // 进程池中的进程总数
  int __idx_;             // 子进程在进程池中的序号，从 0 开始
  int __epollfd_;         // 子进程的 epoll 内核事件表描述符
  int __listenfd_;        // 监听 socket
  volatile bool __stop_;  // 是否停止子进程

  vector<process> __sub_process_;  // 保存所有子进程的描述信息
  static std::atomic<Processpool*> __instance_;  // Processpool 实例，为原子对象

  /* 删除构造函数，通过 Create 方法来创建 Processpool 实例 */
  Processpool(int listenfd, int process_number);

 public:
  static Processpool* Create(int listenfd, int process_number = 8) {
    /* 单件模式，只创建一个进程池实例 */

    Processpool* tmp =
        __instance_.load(std::memory_order_relaxed);  // load 为原子操作

    std::atomic_thread_fence(std::memory_order_acquire);  //获取内存屏障
    /* 这里只读，不用加锁，如果不为空直接返回，减少加锁开销 */
    if (tmp == nullptr) {
      /* 接下来要写了，所以需要锁上 */
      __instance_locker.Lock();
      tmp = __instance_.load(std::memory_order_relaxed);
      if (tmp == nullptr) {
        tmp = new Processpool(listenfd, process_number);
        std::atomic_thread_fence(std::memory_order_release);  // 释放内存屏障
        /* 直到这里，__instance 还是为 nullptr，执行完下面语句后，
         * __instance_ 才不为 nullptr 这样就防止了 reorder 后，
         * __instance_ 虽然不为空，但其还没有构造完成的情况 */
        __instance_.store(tmp, std::memory_order_relaxed);
      }
      __instance_locker.Unlock();
    }
    return tmp;
  }

  ~Processpool() {
    if (__instance_ != nullptr) {
      __instance_locker.Lock();
      // printf("destructor in child %d\n", getpid());
      if (__instance_ != nullptr) delete __instance_;
      __instance_locker.Unlock();
    }
  }

  /* 启动线程池 */
  void Run();

 private:
  void __Setup();
  void __RunParent();
  void __RunChild();
};

/* 初始化静态变量 */
template <typename T>
std::atomic<Processpool<T>*> Processpool<T>::__instance_(nullptr);

/* 处理信号的管道，统一事件源 */
static int sig_sktpipefd[2];  // 0 端读信号，1 端发送信号

static void SigHandler(int sig) {
  // 保存 errno，防止在其他函数读取 errno 前触发了信号处理函数
  int errno_copy = errno;
  int msg = sig;
  if (send(sig_sktpipefd[1], (char*)&msg, 1, 0) < 0) {
    LOGERR("send error");
    exit(-1);
  }
  errno = errno_copy;
}

/** 进程池构造函数
 * listenfd: 监听 socket
 * process_number: 进程池中子进程的数量
 */
template <typename T>
Processpool<T>::Processpool(int listenfd, int process_number)
    : __sub_process_(process_number) {
  __process_number_ = process_number;
  __idx_ = -1;
  __listenfd_ = listenfd;
  __stop_ = false;
  assert((process_number > 0) && (process_number <= kMaxProcessNum));

  /* 创建 process_number 个子进程 */
  for (int i = 0; i < process_number; ++i) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, __sub_process_[i].sktpipefd) < 0) {
      LOGERR("socketpair error");
      exit(-1);
    }
    __sub_process_[i].pid = fork();
    if (__sub_process_[i].pid < 0) {
      LOGERR("fork error");
      exit(-1);
    }

    if (__sub_process_[i].pid > 0) {
      // 父进程
      if (close(__sub_process_[i].sktpipefd[1]) < 0) {
        LOGERR("close error");
        exit(-1);
      }
      continue;
    } else {
      // 子进程
      if (close(__sub_process_[i].sktpipefd[0]) < 0) {
        LOGERR("close error");
        exit(-1);
      }
      __idx_ = i;
      break;
    }
  }
}

/* 统一事件源 */
template <typename T>
void Processpool<T>::__Setup() {
  __epollfd_ = epoll_create(5);
  if (__epollfd_ < 0) {
    LOGERR("epoll_create error");
    exit(-1);
  }
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sig_sktpipefd) < 0) {
    LOGERR("socketpair error");
    exit(-1);
  }
  if (SetNonBlocking(sig_sktpipefd[1]) < 0) {
    LOGERR("SetNonBlocking error");
    exit(-1);
  }
  /* 监听 sig_sktpipedfd[0] 的读事件 */
  if (AddFd(__epollfd_, sig_sktpipefd[0]) < 0) {
    LOGERR("AddFd error");
    exit(-1);
  }
  /* 设置信号处理函数，将接收到的信号全部发送到 sig_sktpipefd[1] */
  if (AddSig(SIGCHLD, SigHandler) < 0 || AddSig(SIGTERM, SigHandler) < 0 ||
      AddSig(SIGINT, SigHandler) < 0 || AddSig(SIGPIPE, SIG_IGN) < 0) {
    LOGERR("AddSig error");
    exit(-1);
  }
}

template <typename T>
void Processpool<T>::Run() {
  /* 父进程的 idx = -1，子进程的 idx >= 0 */
  if (__idx_ != -1) {
    __RunChild();
    return;
  }
  __RunParent();
}

template <typename T>
void Processpool<T>::__RunChild() {
  __Setup();

  /* 1 端为子进程端，0 端为父进程端，子进程通过 sktpipefd[1] 和父进程通信 */
  int sktpipefd = __sub_process_[__idx_].sktpipefd[1];

  /* 监听从父进程发来的消息，父进程会通过这个管道来通知子进程 accept 新连接 */
  if (AddFd(__epollfd_, sktpipefd) < 0) {
    LOGERR("AddFd error");
    exit(-1);
  }

  epoll_event events[kMaxEventNum];
  vector<T> users(kUserPerProcess);
  int n_events = 0;
  int ret = -1;
  while (!__stop_) {
    n_events = epoll_wait(__epollfd_, events, kMaxEventNum, -1);
    if (n_events < 0 && (errno != EINTR)) {
      LOGERR("epoll_wait error");
      exit(-1);
    }
    for (int i = 0; i < n_events; ++i) {
      int sockfd = events[i].data.fd;
      if (sockfd == sktpipefd && (events[i].events & EPOLLIN)) {  // 接收新连接
        int client = 0;
        /* 读取成功表示有新客户端 */
        ret = recv(sockfd, &client, sizeof(client), 0);
        if (ret <= 0) {
          if (ret < 0 && errno != EAGAIN) {
            LOGWARN("recv error");
          }
          continue;
        } else {
          struct sockaddr_in client_addr;
          socklen_t client_addrlen = sizeof(client_addr);
          int connfd =
              accept(__listenfd_, (sockaddr*)&client_addr, &client_addrlen);
          if (connfd < 0) continue;
          if (AddFd(__epollfd_, connfd) < 0) {
            LOGWARN("AddFd error");
            if (close(connfd)) LOGERR("close errro");
            continue;
          }
          /* 逻辑处理类 T 需要实现 Init 方法来初始化一个客户端连接
           * 使用 connfd来索引逻辑处理对象 */
          users[connfd].Init(__epollfd_, connfd, client_addr);
        }
      } else if (sockfd == sig_sktpipefd[0] &&
                 (events[i].events & EPOLLIN)) {  // 接收到信号
        char signals[1024];
        ret = recv(sig_sktpipefd[0], signals, sizeof(signals), 0);
        if (ret <= 0) {
          if (ret < 0 && errno != EAGAIN) {
            LOGERR("recv error");
            exit(-1);
          }
          continue;
        }
        for (int i = 0; i < ret; ++i) {
          switch (signals[i]) {
            case SIGCHLD: {
              int stat;
              while (waitpid(-1, &stat, WNOHANG) > 0) continue;
              break;
            }
            case SIGTERM:
            case SIGINT:
              __stop_ = true;
              break;
            default:
              break;
          }
        }
      } else if (events[i].events & EPOLLIN) {  // 客户端的数据
        users[sockfd].Process();
      }
    }
  }
  if (close(sktpipefd) < 0 || close(__epollfd_) < 0) {
    LOGERR("close error");
    exit(-1);
  }
  // Close(__listenfd_); listenfd 应该由其创建者来关闭
}

template <typename T>
void Processpool<T>::__RunParent() {
  __Setup();
  /* 父进程监听 __listenfd_ */
  if (AddFd(__epollfd_, __listenfd_) < 0) {
    LOGERR("AddFd error");
    exit(-1);
  }
  epoll_event events[kMaxEventNum];

  int sub_process_cnt = 0;
  int new_conn = 1;
  int n_events = 0;
  int ret = -1;
  while (!__stop_) {
    n_events = epoll_wait(__epollfd_, events, kMaxEventNum, -1);
    if (n_events < 0 && (errno != EINTR)) {
      LOGERR("epoll_wait error");
      exit(-1);
    }
    for (int i = 0; i < n_events; ++i) {
      int sockfd = events[i].data.fd;
      if (sockfd == __listenfd_) {  // 有新连接
        /* 采用 Round Robin 方式将新客户端交给子进程 */
        int i = sub_process_cnt;
        do {
          if (__sub_process_[i].pid != -1) {
            break;
          }
          i = (i + 1) % __process_number_;
        } while (i != sub_process_cnt);

        if (__sub_process_[i].pid == -1) {
          __stop_ = true;
          break;
        }

        sub_process_cnt = (i + 1) % __process_number_;

        if (send(__sub_process_[i].sktpipefd[0], &new_conn, sizeof(new_conn),
                 0) < 0) {
          LOGERR("send error");
          exit(-1);
        }
        printf("send request to child %d\n", i);
      } else if (sockfd == sig_sktpipefd[0] &&
                 (events[i].events & EPOLLIN)) {  // 接收信号
        char signals[1024];
        ret = recv(sig_sktpipefd[0], signals, sizeof(signals), 0);
        if (ret <= 0) {
          if (ret < 0 && errno != EAGAIN) {
            LOGERR("recv error");
            exit(-1);
          }
          continue;
        }
        for (int i = 0; i < ret; ++i) {
          switch (signals[i]) {
            case SIGCHLD: {
              pid_t pid;
              int stat;
              while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {
                for (int i = 0; i < __process_number_; ++i) {
                  /* 若第 i 个子进程退出，则关闭信号接收管道，将 pid 置为 -1 */
                  if (__sub_process_[i].pid == pid) {
                    printf("child %d: killed\n", i);
                    if (close(__sub_process_[i].sktpipefd[0]) < 0) {
                      LOGERR("close error");
                      exit(-1);
                    }
                    __sub_process_[i].pid = -1;
                  }
                }
              }
              /* 若所有子进程都退出，则父进程也退出 */
              __stop_ = true;
              for (int i = 0; i < __process_number_; ++i) {
                if (__sub_process_[i].pid != -1) {
                  __stop_ = false;
                  break;
                }
              }
              break;
            }
            case SIGTERM:
            case SIGINT: {
              /* 若父进程收到终止信号，则结束所有子进程 */
              printf("killing children now...\n");
              for (int i = 0; i < __process_number_; ++i) {
                pid_t pid = __sub_process_[i].pid;
                if (pid != -1) {
                  if (kill(pid, SIGTERM) < 0) {
                    LOGERR("kill error");
                    exit(-1);
                  }
                }
              }
              break;
            }
            default:
              break;
          }
        }
      }
    }
  }
  close(__epollfd_);
  // close(__listenfd_); 由创建者关闭
  printf("parent out\n");
}

#endif  //!__PROCESSPOOL__H__