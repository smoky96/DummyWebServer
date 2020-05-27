#include "conmmon.h"

/* 每个客户连接不停地向服务器发送这个请求 */
static const char* request =
    "GET http://192.168.226.131/ "
    "HTTP/1.1\r\nConnection:keep-alive\r\n\r\n";

/* 向服务器写入 len 字节的数据 */
bool write_nbytes(int sockfd, const char* buffer, int len) {
  int bytes_write = 0;
  printf("write out %d bytes to socket %d\n", len, sockfd);
  while (1) {
    bytes_write = Send(sockfd, buffer, len, 0);
    if (bytes_write < 0 || bytes_write == 0) return false;
    len -= bytes_write;
    buffer = buffer + bytes_write;
    if (len <= 0) {
      return true;
    }
  }
}

/* 从服务器读取数据 */
bool read_once(int sockfd, char* buffer, int len) {
  int bytes_read = 0;
  memset(buffer, '\0', len);
  while ((bytes_read = Recv(sockfd, buffer, len, 0)) > 0)
    printf("read in %d bytes from socket %d\n", bytes_read, sockfd);
  if (bytes_read == -1 && errno != EAGAIN) return false;
  return true;
}

/* 向服务器发起 num 个 TCP 连接，可以通过 num 来调整测试压力 */
void start_conn(int epollfd, int* num, const char* ip, int port) {
  struct sockaddr_in addr;
  bzero(&addr, sizeof(addr));
  inet_pton(AF_INET, ip, &addr.sin_addr);
  addr.sin_port = htons(port);
  addr.sin_family = AF_INET;

  int sockfd[*num];
  for (int i = 0; i < *num; ++i) {
    sockfd[i] = Socket(AF_INET, SOCK_STREAM, 0);
    Connect(sockfd[i], &addr, sizeof(addr));
    printf("connected %d of %d\n", i + 1, *num);
    epoll_event event;
    event.data.fd = sockfd[i];
    event.events = EPOLLOUT | EPOLLET | EPOLLERR;
    Epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd[i], &event);
    SetNonBlocking(sockfd[i]);
  }
}

int main(int argc, char** argv) {
  if (argc < 4) {
    printf("Usage: %s ip_address port_number connection_number\n",
           basename(argv[0]));
    return 1;
  }

  int epollfd = Epoll_create(5);
  epoll_event events[10000];
  char buffer[2048];

  int num = atoi(argv[3]);
  start_conn(epollfd, &num, argv[1], atoi(argv[2]));

  while (num) {
    int n = Epoll_wait(epollfd, events, 10000, 2000);
    for (int i = 0; i < n; ++i) {
      int sockfd = events[i].data.fd;
      if (events[i].events & EPOLLIN) {
        if (!read_once(sockfd, buffer, 2048)) {
          RemoveFd(epollfd, sockfd);
          --num;
          printf("have %d socket left\n", num);
          continue;
        }

        epoll_event event;
        event.data.fd = sockfd;
        event.events = EPOLLOUT | EPOLLET | EPOLLERR;
        Epoll_ctl(epollfd, EPOLL_CTL_MOD, sockfd, &event);
      } else if (events[i].events & EPOLLOUT) {
        if (!write_nbytes(sockfd, request, strlen(request))) {
          RemoveFd(epollfd, sockfd);
          --num;
          printf("have %d socket left\n", num);
          continue;
        }

        epoll_event event;
        event.data.fd = sockfd;
        event.events = EPOLLIN | EPOLLET | EPOLLERR;
        Epoll_ctl(epollfd, EPOLL_CTL_MOD, sockfd, &event);
      } else {
        RemoveFd(epollfd, sockfd);
      }
    }
  }

  return 0;
}
