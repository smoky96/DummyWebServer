#include "conmmon.h"
#include "processpool.h"

/* 处理客户 CGI 请求，可作为 Processpool 类的模板参数 */
class Cgi {
 private:
  static const int __kBufferSize_ = 1024;
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
  ~Cgi() {}

  /* 初始化客户连接，清空读缓冲区 */
  void Init(int epollfd, int sockfd, const sockaddr_in& client_addr) {
    __epollfd_ = epollfd;
    __sockfd_ = sockfd;
    __addr_ = client_addr;
    __ResetBuf();
  }

  void Process() {
    int idx = 0;
    int ret = -1;
    while (1) {
      ret = Recv(__sockfd_, __buf_ + idx, __kBufferSize_ - idx - 1, 0);
      if (ret < 0) {
        /* 若读操作发生错误，则关闭客户端连接；若无数据可读，退出循环 */
        if (errno != EAGAIN) {
          RemoveFd(__epollfd_, __sockfd_);
        }
        break;
      } else if (ret == 0) {
        RemoveFd(__epollfd_, __sockfd_);
        break;
      } else {
        __read_idx_ += ret;
        printf("user content is: %s\n", __buf_);
        for (; idx < __read_idx_; ++idx) {
          /* 若遇到字符 "\r\n" 则开始处理客户请求 */
          if (idx >= 1 && (__buf_[idx - 1] == '\r' && __buf_[idx] == '\n')) {
            break;
          }
        }
        /* 若读完还没有遇到字符 '\r\n' 则继续接收 */
        if (idx == __read_idx_) {
          if (__read_idx_ == __kBufferSize_ - 1) {
            char msg[] = "content overflow\n";
            printf("user content overflow\n");
            Send(__sockfd_, msg, sizeof(msg), 0);
            RemoveFd(__epollfd_, __sockfd_);
            break;
          }
          continue;
        }

        __buf_[idx - 1] = '\0';
        char* filename = __buf_;
        /* 判断文件是否存在 */
        if (access(filename, F_OK) < 0) {
          RemoveFd(__epollfd_, __sockfd_);

          /* 一条请求处理完毕，重置缓存 */
          __ResetBuf();
          break;
        }
        ret = fork();
        if (ret == -1 || ret > 0) {
          RemoveFd(__epollfd_, __sockfd_);

          /* 一条请求处理完毕，重置缓存 */
          __ResetBuf();
          break;
        } else {
          Dup2(__sockfd_, STDOUT_FILENO);
          execl(__buf_, __buf_, NULL);
          exit(0);
        }
      }
    }
  }
};

int Cgi::__epollfd_ = -1;

int main(int argc, char** argv) {
  if (argc <= 2) {
    printf("Usage: %s ip_address port_number\n", basename(argv[0]));
    return 1;
  }

  const char* ip = argv[1];
  int port = atoi(argv[2]);

  int listenfd = Socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  bzero(&addr, sizeof(addr));
  inet_pton(AF_INET, ip, &addr.sin_addr);
  addr.sin_port = htons(port);
  addr.sin_family = AF_INET;

  Bind(listenfd, &addr, sizeof(addr));

  Listen(listenfd, 5);

  Processpool<Cgi>* pool = Processpool<Cgi>::Create(listenfd);
  if (pool) {
    pool->Run();
  }
  Close(listenfd);

  return 0;
}
