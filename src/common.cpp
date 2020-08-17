#include "common.h"

/* 设置非阻塞 io，成功返回 old_opt，错误返回 -1 */
int SetNonBlocking(int fd) {
  int old_opt = fcntl(fd, F_GETFL);
  if (old_opt < 0) {
    LOGERR("fcntl error");
    return -1;
  }
  int new_opt = old_opt | O_NONBLOCK;
  if (fcntl(fd, F_SETFL, new_opt) < 0) {
    LOGERR("fcntl error");
    return -1;
  }
  return old_opt;
}

/* 将文件描述符 fd 加入到 epoll 事件表中，监听读事件
 * one_shot: 是否采用 one-shot 行为，默认 false
 * trigger_mode: 触发模式，0 为 ET，1 为 LT，默认 ET
 * 成功返回 0，错误返回 -1 */
int AddFd(int epollfd, int fd, bool one_shot, TriggerMode trigger_mode) {
  epoll_event event;
  event.data.fd = fd;
  event.events = EPOLLIN | EPOLLRDHUP;

  if (trigger_mode == 0) event.events |= EPOLLET;
  if (one_shot) event.events |= EPOLLONESHOT;

  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event) < 0) {
    LOGERR("epoll_ctl error");
    return -1;
  }
  if (SetNonBlocking(fd) < 0) {
    LOGERR("SetNonBlocking error");
    return -1;
  }
  return 0;
}

/* 重设 one-shot，
 * ev 为附加监听事件，最终监听事件为 ev | EPOLLONESHOT | EPOLLRDHUP
 * trigger_mode: 触发模式，0 为 ET，1 为 LT，默认 ET
 * 成功返回 0，错误返回 -1 */
int ModFd(int epollfd, int fd, int ev, TriggerMode trigger_mode) {
  epoll_event event;
  event.data.fd = fd;
  event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

  if (trigger_mode == 0) event.events |= EPOLLET;

  if (epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event) < 0) {
    LOGERR("epoll_ct error");
    return -1;
  }
  return 0;
}

/* 从 epoll 事件表中删除 fd，成功返回 0，错误返回 -1 */
int RemoveFd(int epollfd, int fd) {
  if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0) < 0) {
    LOGERR("epoll_ctl error");
    return -1;
  }
  shutdown(fd, SHUT_RDWR);
  return 0;
}

/* 设置捕获信号，成功返回 0，错误返回 -1 */
int AddSig(int signum, void (*handler)(int), bool restart) {
  struct sigaction sa;
  bzero(&sa, sizeof(sa));
  sa.sa_handler = handler;
  if (restart) sa.sa_flags |= SA_RESTART;
  if (sigfillset(&sa.sa_mask) < 0) {
    LOGERR("sigfillset error");
    return -1;
  }
  if (sigaction(signum, &sa, NULL) < 0) {
    LOGERR("sigaction error");
    return -1;
  }
  return 0;
}

/* 发送错误信息，成功返回 0，错误返回 -1 */
int SendError(int connfd, const char* info) {
  if (send(connfd, info, strlen(info), 0) < 0) {
    LOGERR("send error");
    return -1;
  }
  if (close(connfd) < 0) {
    LOGERR("close error");
    return -1;
  }
  return 0;
}
