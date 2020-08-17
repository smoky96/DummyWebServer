#include "python_cgi.h"

/* 初始化客户连接，清空读缓冲区 */
void PythonCgi::Init(int epollfd, int sockfd, const sockaddr_in& client_addr) {
  __epollfd_ = epollfd;
  __sockfd_ = sockfd;
  __addr_ = client_addr;
  __content_length_ = 0;
  __ResetBuf();
}

void PythonCgi::Process() {
  int idx = 0;
  int ret = -1;

  while (1) {
    ret = recv(__sockfd_, __buf_ + __read_idx_, __kBufferSize_ - idx - 1, 0);
    if (ret < 0) {
      /* 若读操作发生错误，则关闭客户端连接；若无数据可读，退出循环 */
      if (errno != EAGAIN) {
        LOGWARN("recv error");
        if (RemoveFd(__epollfd_, __sockfd_) < 0) {
          LOGWARN("RemoveFd error");
        }
      }
      break;
    } else if (ret > 0) {
      __read_idx_ += ret;
      // printf("user content is: %s\n", __buf_);
      for (; idx < __read_idx_; ++idx) {
        /* 若遇到字符 "\r\n" 则长度信息接收完毕 */
        if (idx >= 1 && (__buf_[idx - 1] == '\r' && __buf_[idx] == '\n')) {
          __buf_[idx - 1] = '\0';
          __content_length_ = atoi(__buf_);
          // printf("content length: %d\n", __content_length_);
          break;
        }
      }
      /* 若读完还没有遇到字符 '\r\n' 则继续接收 */
      if (idx == __read_idx_) {
        if (idx == __kBufferSize_ - 1) {
          char msg[] = "content overflow\n";
          printf("user content overflow\n");
          if (send(__sockfd_, msg, sizeof(msg), 0) < 0) {
            LOGWARN("send error");
          }
          if (RemoveFd(__epollfd_, __sockfd_) < 0) {
            LOGWARN("RemoveFd error");
          }
          break;
        }
        continue;
      }
      ret = fork();
      if (ret == -1 || ret > 0) {
        if (RemoveFd(__epollfd_, __sockfd_) < 0) {
          LOGWARN("RemoveFd error");
        }
        /* 一条请求处理完毕，重置缓存 */
        __ResetBuf();
        break;
      } else {
        if (dup2(__sockfd_, STDOUT_FILENO) < 0 ||
            dup2(__sockfd_, STDERR_FILENO) < 0) {
          LOGERR("dup2 error");
          exit(-1);
        }
        char decoded[__kBufferSize_];
        memset(decoded, '\0', sizeof(decoded));
        UrlDecode(__buf_ + idx + 1, decoded, __kBufferSize_);
        PyRun_SimpleString(decoded);
        exit(0);
      }
    } else {
      if (RemoveFd(__epollfd_, __sockfd_) < 0) LOGWARN("RemoveFd error");
      break;
    }
  }
}
