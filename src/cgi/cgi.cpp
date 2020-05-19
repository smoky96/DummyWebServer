#include "conmmon.h"
#include "processpool.h"

/* 处理客户 CGI 请求，可作为 processpool 类的模板参数 */
class cgi {
 private:
  static const int BUFFER_SIZE = 1024;
  static int __epollfd;
  int __sockfd;
  sockaddr_in __addr;
  char __buf[BUFFER_SIZE];
  int __read_idx;  // 标记已读客户端数据的最后一个字节的下一个位置

  void __reset_buf() {
    memset(__buf, '\0', BUFFER_SIZE);
    __read_idx = 0;
  }

 public:
  cgi() {}
  ~cgi() {}

  /* 初始化客户连接，清空读缓冲区 */
  void init(int epollfd, int sockfd, const sockaddr_in& client_addr) {
    __epollfd = epollfd;
    __sockfd = sockfd;
    __addr = client_addr;
    __reset_buf();
  }

  void process() {
    int idx = 0;
    int ret = -1;
    while (1) {
      ret = Recv(__sockfd, __buf + idx, BUFFER_SIZE - idx - 1, 0);
      if (ret < 0) {
        /* 若读操作发生错误，则关闭客户端连接；若无数据可读，退出循环 */
        if (errno != EAGAIN) {
          removefd(__epollfd, __sockfd);
        }
        break;
      } else if (ret == 0) {
        removefd(__epollfd, __sockfd);
        break;
      } else {
        __read_idx += ret;
        printf("user content is: %s\n", __buf);
        for (; idx < __read_idx; ++idx) {
          /* 若遇到字符 "\r\n" 则开始处理客户请求 */
          if (idx >= 1 && (__buf[idx - 1] == '\r' && __buf[idx] == '\n')) {
            break;
          }
        }
        /* 若读完还没有遇到字符 '\r\n' 则继续接收 */
        if (idx == __read_idx) {
          if (__read_idx == BUFFER_SIZE - 1) {
            char msg[] = "content overflow\n";
            printf("user content overflow\n");
            Send(__sockfd, msg, sizeof(msg), 0);
            removefd(__epollfd, __sockfd);
            break;
          }
          continue;
        }

        __buf[idx - 1] = '\0';
        char* filename = __buf;
        /* 判断文件是否存在 */
        if (access(filename, F_OK) < 0) {
          removefd(__epollfd, __sockfd);

          /* 一条请求处理完毕，重置缓存 */
          __reset_buf();
          break;
        }
        ret = fork();
        if (ret == -1 || ret > 0) {
          removefd(__epollfd, __sockfd);

          /* 一条请求处理完毕，重置缓存 */
          __reset_buf();
          break;
        } else {
          Dup2(__sockfd, STDOUT_FILENO);
          execl(__buf, __buf, NULL);
          exit(0);
        }
      }
    }
  }
};

int cgi::__epollfd = -1;

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

  processpool<cgi>* pool = processpool<cgi>::create(listenfd);
  if (pool) {
    pool->run();
  }
  Close(listenfd);

  return 0;
}
