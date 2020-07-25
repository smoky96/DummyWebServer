#ifndef __CGI__H__
#define __CGI__H__

#include "common.h"
#include "processpool.h"

class Cgi {
 protected:
  static const int __kBufferSize_ = 2048;
  static int __epollfd_;
  int __sockfd_;
  sockaddr_in __addr_;
  char __buf_[__kBufferSize_];
  int __read_idx_;  // 标记已读客户端数据的最后一个字节的下一个位置

  void __ResetBuf() {
    memset(__buf_, '\0', __kBufferSize_);
    __read_idx_ = 0;
  }

 public:
  Cgi() {}
  virtual ~Cgi() {}

  virtual void Init(int epollfd, int sockfd, const sockaddr_in& client_addr) = 0;
  virtual void Process() = 0;
};


#endif  //!__CGI__H__