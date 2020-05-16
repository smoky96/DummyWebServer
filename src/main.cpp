#include <memory>
#include <vector>

#include "conmmon.h"
#include "http_conn.h"
#include "locker.h"
#include "threadpool.h"

#define MAX_FD 65535
#define MAX_EVENT_NUM 10000

using std::unique_ptr;
using std::vector;

void send_error(int connfd, const char* info) {
  printf("%s", info);
  Send(connfd, info, strlen(info), 0);
  Close(connfd);
}

int main(int argc, char** argv) {
  if (argc <= 2) {
    printf("Usage: %s ip_address port_number\n", basename(argv[0]));
    return 1;
  }

  const char* ip = argv[1];
  int port = atoi(argv[2]);

  /* 忽略 SIGPIPE 信号 */
  addsig(SIGPIPE, SIG_IGN);
  /* 创建线程 */
  unique_ptr<threadpool<http_conn>> pool(new threadpool<http_conn>(1));

  /* 为每个可能的客户端连接分配一个 http_conn 对象 */
  vector<http_conn> users(MAX_FD);
  int listenfd = Socket(AF_INET, SOCK_STREAM, 0);
  struct linger tmp = {1, 0};
  setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

  struct sockaddr_in addr;
  bzero(&addr, sizeof(addr));
  inet_pton(AF_INET, ip, &addr.sin_addr);
  addr.sin_port = htons(port);
  addr.sin_family = AF_INET;
  Bind(listenfd, &addr, sizeof(addr));
  Listen(listenfd, 5);

  epoll_event events[MAX_EVENT_NUM];
  int epollfd = Epoll_create(5);
  addfd(epollfd, listenfd, false);
  http_conn::epollfd = epollfd;

  while (1) {
    int n = Epoll_wait(epollfd, events, MAX_EVENT_NUM, -1);
    for (int i = 0; i < n; ++i) {
      int sockfd = events[i].data.fd;
      if (sockfd == listenfd) {
        struct sockaddr_in client_addr;
        socklen_t client_addrlen = sizeof(client_addr);
        int connfd = Accept(sockfd, &client_addr, &client_addrlen);
        // int send_buf = 2048;
        // setsockopt(connfd, SOL_SOCKET, SO_SNDBUF, (const char*)&send_buf,
        //            sizeof(send_buf));
        if (http_conn::user_cnt >= MAX_FD) {
          send_error(connfd, "Internal server busy");
          continue;
        }
        /* 初始化客户端连接 */
        users[connfd].init(connfd, client_addr);
      } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
        /* 异常，关闭连接 */
        users[sockfd].close_conn();
      } else if (events[i].events & EPOLLIN) {
        /* 根据读的结果，决定是添加任务还是关闭连接 */
        if (users[sockfd].read()) {
          pool->append(&users[sockfd]);
        } else {
          users[sockfd].close_conn();
        }
      } else if (events[i].events & EPOLLOUT) {
        /* 根据写的结果，决定是添加任务还是关闭连接 */
        if (!users[sockfd].write()) {
          users[sockfd].close_conn();
        }
      }
    }
  }
  Close(epollfd);
  Close(listenfd);

  return 0;
}
