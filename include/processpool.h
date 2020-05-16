#ifndef __PROCESSPOOL__H__
#define __PROCESSPOOL__H__

#include <atomic>
#include <cassert>
#include <vector>

#include "conmmon.h"
#include "locker.h"

using std::vector;

/* 描述子进程的类 */
class process {
 public:
  pid_t pid;         // 进程 PID
  int sktpipefd[2];  // 与父进程通信的管道

  process() { pid = -1; }
};

/* 对 processpool 中的 __instance 上锁 */
static locker __instance_locker = locker();

/** 进程池类模板
 * T: 处理逻辑任务的类
 */
template <typename T>
class processpool {
 private:
  static const int MAX_PROCESS_NUMBER = 16;  // 线程池最大子线程数量
  static const int USER_PER_PROCESS = 65535;  // 每个子进程最多处理的客户端数量
  static const int MAX_EVENT_NUMBER = 10000;  // epoll 最多能处理的事件数

  int __process_number;  // 进程池中的进程总数
  int __idx;             // 子进程在进程池中的序号，从 0 开始
  int __epfd;            // 子进程的 epoll 内核事件表描述符
  int __listenfd;        // 监听 socket
  volatile int __stop;   // 是否停止子进程

  vector<process> __sub_process;  // 保存所有子进程的描述信息
  static std::atomic<processpool*> __instance;  // processpool 实例，为原子对象

  /* 删除构造函数，通过 create 方法来创建 processpool 实例 */
  processpool(int listenfd, int process_number);

 public:
  static processpool* create(int listenfd, int process_number = 8) {
    /* 单件模式，只创建一个进程池实例 */

    processpool* tmp =
        __instance.load(std::memory_order_relaxed);  // load 为原子操作

    std::atomic_thread_fence(std::memory_order_acquire);  //获取内存屏障
    /* 这里只读，不用加锁，如果不为空直接返回，减少加锁开销 */
    if (tmp == nullptr) {
      /* 接下来要写了，所以需要锁上 */
      __instance_locker.lock();
      tmp = __instance.load(std::memory_order_relaxed);
      if (tmp == nullptr) {
        tmp = new processpool(listenfd, process_number);
        std::atomic_thread_fence(std::memory_order_release);  // 释放内存屏障
        /* 直到这里，__instance 还是为 nullptr，执行完下面语句后，
         * __instance 才不为 nullptr 这样就防止了 reorder 后，
         * __instance 虽然不为空，但其还没有构造完成的情况 */
        __instance.store(tmp, std::memory_order_relaxed);
      }
      __instance_locker.unlock();
    }
    return tmp;
  }

  ~processpool() {
    if (__instance != nullptr) {
      __instance_locker.lock();
      // printf("destructor in child %d\n", getpid());
      if (__instance != nullptr) delete __instance;
      __instance_locker.unlock();
    }
  }

  /* 启动线程池 */
  void run();

 private:
  void __setup();
  void __run_parent();
  void __run_child();
};

/* 初始化静态变量 */
template <typename T>
std::atomic<processpool<T>*> processpool<T>::__instance(nullptr);

/* 处理信号的管道，统一事件源 */
static int sig_sktpipefd[2];  // 0 端读信号，1 端发送信号

static void sig_handler(int sig) {
  // 保存 errno，防止在其他函数读取 errno 前触发了信号处理函数
  int errno_copy = errno;
  int msg = sig;
  Send(sig_sktpipefd[1], (char*)&msg, 1, 0);
  errno = errno_copy;
}

/** 进程池构造函数
 * listenfd: 监听 socket
 * process_number: 进程池中子进程的数量
 */
template <typename T>
processpool<T>::processpool(int listenfd, int process_number)
    : __sub_process(process_number) {
  __process_number = process_number;
  __idx = -1;
  __listenfd = listenfd;
  __stop = false;
  assert((process_number > 0) && (process_number <= MAX_PROCESS_NUMBER));

  /* 创建 process_number 个子进程 */
  for (int i = 0; i < process_number; ++i) {
    Socketpair(AF_UNIX, SOCK_STREAM, 0, __sub_process[i].sktpipefd);
    __sub_process[i].pid = Fork();
    if (__sub_process[i].pid > 0) {
      // 父进程
      Close(__sub_process[i].sktpipefd[1]);
      continue;
    } else {
      // 子进程
      Close(__sub_process[i].sktpipefd[0]);
      __idx = i;
      break;
    }
  }
}

/* 统一事件源 */
template <typename T>
void processpool<T>::__setup() {
  __epfd = Epoll_create(5);
  Socketpair(AF_UNIX, SOCK_STREAM, 0, sig_sktpipefd);
  setnonblocking(sig_sktpipefd[1]);
  addfd(__epfd, sig_sktpipefd[0]);  // 监听 sig_sktpipedfd[0] 的读事件
  /* 设置信号处理函数，将接收到的信号全部发送到 sig_sktpipefd[1] */
  addsig(SIGCHLD, sig_handler);
  addsig(SIGTERM, sig_handler);
  addsig(SIGINT, sig_handler);
  addsig(SIGPIPE, SIG_IGN);  // 忽略 SIGPIPE 信号，防止进程被终止
}

template <typename T>
void processpool<T>::run() {
  /* 父进程的 idx = -1，子进程的 idx >= 0 */
  if (__idx != -1) {
    __run_child();
    return;
  }
  __run_parent();
}

template <typename T>
void processpool<T>::__run_child() {
  __setup();

  /* 1 端为子进程端，0 端为父进程端，子进程通过 sktpipefd[1] 和父进程通信 */
  int sktpipefd = __sub_process[__idx].sktpipefd[1];

  /* 监听从父进程发来的消息，父进程会通过这个管道来通知子进程 accept 新连接 */
  addfd(__epfd, sktpipefd);

  epoll_event events[MAX_EVENT_NUMBER];
  vector<T> users(USER_PER_PROCESS);
  int n_events = 0;
  int ret = -1;
  while (!__stop) {
    n_events = Epoll_wait(__epfd, events, MAX_EVENT_NUMBER, -1);
    for (int i = 0; i < n_events; ++i) {
      int sockfd = events[i].data.fd;
      if (sockfd == sktpipefd && (events[i].events & EPOLLIN)) {  // 接收新连接
        int client = 0;
        /* 读取成功表示有新客户端 */
        ret = Recv(sockfd, &client, sizeof(client), 0);
        if (((ret < 0) && (errno != EAGAIN)) || ret == 0) {
          continue;
        } else {
          struct sockaddr_in client_addr;
          socklen_t client_addrlen = sizeof(client_addr);
          int connfd = Accept(__listenfd, &client_addr, &client_addrlen);
          if (connfd < 0) continue;
          addfd(__epfd, connfd);
          /* 逻辑处理类 T 需要实现 init 方法来初始化一个客户端连接
           * 使用 connfd来索引逻辑处理对象 */
          users[connfd].init(__epfd, connfd, client_addr);
        }
      } else if (sockfd == sig_sktpipefd[0] &&
                 (events[i].events & EPOLLIN)) {  // 接收到信号
        int sig;
        char signals[1024];
        ret = Recv(sig_sktpipefd[0], signals, sizeof(signals), 0);
        if (ret <= 0) continue;
        for (int i = 0; i < ret; ++i) {
          switch (signals[i]) {
            case SIGCHLD: {
              pid_t pid;
              int stat;
              while ((pid == waitpid(-1, &stat, WNOHANG)) > 0) continue;
              break;
            }
            case SIGTERM:
            case SIGINT:
              __stop = true;
              break;
            default:
              break;
          }
        }
      } else if (events[i].events & EPOLLIN) {  // 客户端的数据
        users[sockfd].process();
      }
    }
  }
  Close(sktpipefd);
  Close(__epfd);
  // Close(__listenfd); listenfd 应该由其创建者来关闭
}

template <typename T>
void processpool<T>::__run_parent() {
  __setup();
  /* 父进程监听 __listenfd */
  addfd(__epfd, __listenfd);
  epoll_event events[MAX_EVENT_NUMBER];

  int sub_process_cnt = 0;
  int new_conn = 1;
  int n_events = 0;
  int ret = -1;
  while (!__stop) {
    n_events = Epoll_wait(__epfd, events, MAX_EVENT_NUMBER, -1);
    for (int i = 0; i < n_events; ++i) {
      int sockfd = events[i].data.fd;
      if (sockfd == __listenfd) {  // 有新连接
        /* 采用 Round Robin 方式将新客户端交给子进程 */
        int i = sub_process_cnt;
        do {
          if (__sub_process[i].pid != -1) {
            break;
          }
          i = (i + 1) % __process_number;
        } while (i != sub_process_cnt);

        if (__sub_process[i].pid == -1) {
          __stop = true;
          break;
        }

        sub_process_cnt = (i + 1) % __process_number;

        Send(__sub_process[i].sktpipefd[0], &new_conn, sizeof(new_conn), 0);
        printf("send request to child %d\n", i);
      } else if (sockfd == sig_sktpipefd[0] &&
                 (events[i].events & EPOLLIN)) {  // 接收信号
        int sig;
        char signals[1024];
        ret = Recv(sig_sktpipefd[0], signals, sizeof(signals), 0);
        if (ret <= 0) continue;
        for (int i = 0; i < ret; ++i) {
          switch (signals[i]) {
            case SIGCHLD: {
              pid_t pid;
              int stat;
              while ((pid = waitpid(-1, &stat, WNOHANG)) > 0) {
                for (int i = 0; i < __process_number; ++i) {
                  /* 若第 i 个子进程退出，则关闭信号接收管道，将 pid 置为 -1 */
                  if (__sub_process[i].pid == pid) {
                    printf("child %d: killed\n", i);
                    Close(__sub_process[i].sktpipefd[0]);
                    __sub_process[i].pid = -1;
                  }
                }
              }
              /* 若所有子进程都退出，则父进程也退出 */
              __stop = true;
              for (int i = 0; i < __process_number; ++i) {
                if (__sub_process[i].pid != -1) {
                  __stop = false;
                  break;
                }
              }
              break;
            }
            case SIGTERM:
            case SIGINT: {
              /* 若父进程收到终止信号，则结束所有子进程 */
              printf("killing children now...\n");
              for (int i = 0; i < __process_number; ++i) {
                pid_t pid = __sub_process[i].pid;
                if (pid != -1) {
                  Kill(pid, SIGTERM);
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
  close(__epfd);
  // close(__listenfd); 由创建者关闭
  printf("parent out\n");
}

#endif  //!__PROCESSPOOL__H__