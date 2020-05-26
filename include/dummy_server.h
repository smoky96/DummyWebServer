#ifndef __DUMMY_SERVER__H__
#define __DUMMY_SERVER__H__

#include <exception>
#include <memory>
#include <vector>

#include "conmmon.h"
#include "http_conn.h"
#include "threadpool.h"

#define MAX_FD 65535         // 最大文件描述符
#define MAX_EVENT_NUM 10000  // 最大事件数

using std::vector;

enum TrigerMode { ET = 0, LT };

class Config {
 public:
  int port_;        // 端口号
  int thread_num_;  // 线程数
  const char* root_;

 public:
  Config(char* root, int port = 8080, int thread_num = 8) {
    root_ = root;  // 注意这里为浅拷贝
    port_ = port;
    thread_num_ = thread_num;
  };
  ~Config() {}

  void ParseArg(int argc, char** argv);
};

class DummyServer {
 private:
  int __port_;                 // 端口号
  char* __root_;               // 网站根目录
  vector<http_conn> __users_;  // 客户端数组
  int __thread_num_;           // 线程数

  std::unique_ptr<threadpool<http_conn>> __pool_;

  epoll_event __events_[MAX_EVENT_NUM];  // 触发事件数组
  int __epollfd_;                        // epoll 内核事件表描述符
  int __listenfd_;                       // 监听描述符
  TrigerMode __triger_mode_;             // 触发模式，暂时只支持 ET

  /* 信号处理函数，sig 为待处理信号，信号处理函数必须为静态 */
  static void __SigHandler(int sig);

  volatile bool __stop_server_;

 public:
  DummyServer(Config config);
  ~DummyServer();

  /* 不允许复制 */
  DummyServer(const DummyServer& rhs) = delete;
  DummyServer& operator=(const DummyServer& rhs) = delete;

  /* 开始运行 Dummy Server */
  void Start();

  /* get 方法 */
  int port() const { return __port_; }
  const char* root() const { return __root_; }
  TrigerMode triger_mode() const { return __triger_mode_; };

 private:
  void __AddClient();
  void __Listen();
  void __SignalProcess();
  void __ReadFromClient(int sockfd);
  void __WriteToClient(int sockfd);
};

#endif  //!__DUMMY_SERVER__H__