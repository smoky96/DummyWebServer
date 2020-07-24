#include <netdb.h>

#include "common.h"

#define BUFSIZE 2048

/* 每个客户连接不停地向服务器发送这个请求 */
static char request[BUFSIZE];
static char host[BUFSIZE];
volatile bool stop = false;
int speed = 0;
int bytes = 0;
int failed = 0;
int conn_num = 1;

void alarm_handler(int) { stop = true; }

void build_request(const char* url) {
  memset(request, '\0', sizeof(request));
  strcpy(request, "GET ");
  if (strstr(url, "://") == NULL) {
    printf("Invalid URL: %s\n", url);
    exit(-1);
  }
  const char* h = strstr(url, "://") + 3;
  if (strstr(h, "/") == NULL) {
    printf("hostname should be end with '/'\n");
    exit(-1);
  }

  strncpy(host, h, strcspn(h, "/"));
  strcat(request, h + strcspn(h, "/"));
  strcat(request, " HTTP/1.1");
  strcat(request, "\r\n");
  strcat(request, "User-Agent: DummyWebServer\r\n");
  strcat(request, "Host: ");
  strcat(request, host);
  strcat(request, "\r\n");
  strcat(request, "Connection: keep-alive\r\n\r\n");
}

/* 向服务器写入 len 字节的数据 */
bool write_nbytes(int sockfd, const char* buffer, int len) {
  int bytes_write = 0;
  // printf("write out %d bytes to socket %d\n", len, sockfd);
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
  while ((bytes_read = Recv(sockfd, buffer, len, 0)) > 0) {
    // printf("read in %d bytes from socket %d\n", bytes_read, sockfd);
    if (stop) return true;
    bytes += bytes_read;
  }
  if (bytes_read == -1 && errno != EAGAIN) return false;
  return true;
}

/* 向服务器发起 num 个 TCP 连接，可以通过 num 来调整测试压力 */
void start_conn(int epollfd, int num, const char* host, int port) {
  struct sockaddr_in addr;
  bzero(&addr, sizeof(addr));

  addr.sin_port = htons(port);
  addr.sin_family = AF_INET;

  struct hostent* he = gethostbyname(host);
  memcpy(&(addr.sin_addr), he->h_addr_list[0], sizeof(in_addr));

  int sockfd[num];
  for (int i = 0; i < num; ++i) {
    sockfd[i] = Socket(AF_INET, SOCK_STREAM, 0);
    if (Connect(sockfd[i], &addr, sizeof(addr)) < 0) {
      ++failed;
    }
    printf("connected %d of %d\n", i + 1, num);
    epoll_event event;
    event.data.fd = sockfd[i];
    event.events = EPOLLOUT | EPOLLET | EPOLLERR;
    Epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd[i], &event);
    SetNonBlocking(sockfd[i]);
  }
}

int main(int argc, char** argv) {
  if (argc < 5) {
    printf("Usage: %s host port connection_number time(sec)\n",
           basename(argv[0]));
    return 1;
  }

  int epollfd = Epoll_create(5);
  epoll_event events[100000];
  char buffer[BUFSIZE];

  int num = atoi(argv[3]);
  build_request(argv[1]);
  start_conn(epollfd, num, host, atoi(argv[2]));

  struct sigaction sa;
  sa.sa_handler = alarm_handler;
  sa.sa_flags = 0;
  sigaction(SIGALRM, &sa, NULL);
  alarm(atoi(argv[4]));

  while (!stop) {
    int n = Epoll_wait(epollfd, events, 100000, 2000);
    for (int i = 0; i < n; ++i) {
      int sockfd = events[i].data.fd;
      if (events[i].events & EPOLLIN) {
        if (!read_once(sockfd, buffer, BUFSIZE)) {
          // RemoveFd(epollfd, sockfd);
          ++failed;
          // printf("have %d socket left\n", num);
          continue;
        }

        epoll_event event;
        event.data.fd = sockfd;
        event.events = EPOLLOUT | EPOLLET | EPOLLERR;
        Epoll_ctl(epollfd, EPOLL_CTL_MOD, sockfd, &event);
      } else if (events[i].events & EPOLLOUT) {
        if (!write_nbytes(sockfd, request, strlen(request))) {
          // RemoveFd(epollfd, sockfd);
          ++failed;
          // printf("have %d socket left\n", num);
          continue;
        }

        ++speed;
        epoll_event event;
        event.data.fd = sockfd;
        event.events = EPOLLIN | EPOLLET | EPOLLERR;
        Epoll_ctl(epollfd, EPOLL_CTL_MOD, sockfd, &event);
      } else {
        // RemoveFd(epollfd, sockfd);
        ++failed;
      }
    }
  }

  int duration = atoi(argv[4]);
  printf(
      "\nSpeed=%d pages/min, %.2f KB/sec. Requests: %d succeed, %d failed.\n",
      (int)((speed + failed) / (duration / 60.0f)),
      (float)(bytes / (float)duration / 1024.0f), speed, failed);

  return 0;
}
