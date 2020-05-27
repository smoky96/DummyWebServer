#include "conmmon.h"

int Fork() {
  int ret = fork();
  if (ret < 0) {
    PRINT_ERRNO(fork);
    exit(-1);
  }
  return ret;
}

int Dup2(int oldfd, int newfd) {
  int ret = dup2(oldfd, newfd);
  if (ret < 0) {
    PRINT_ERRNO(dup2);
    exit(-1);
  }
  return ret;
}

int Socket(int domain, int type, int protocol) {
  int ret = socket(domain, type, protocol);
  if (ret < 0) {
    PRINT_ERRNO(socket);
    exit(-1);
  }
  return ret;
}

int Bind(int sockfd, const struct sockaddr_in* addr, socklen_t addrlen) {
  if (bind(sockfd, (sockaddr*)addr, addrlen) < 0) {
    PRINT_ERRNO(bind);
    exit(-1);
  }
  return 0;
}

int Listen(int sockfd, int backlog) {
  if (listen(sockfd, backlog) < 0) {
    PRINT_ERRNO(listen);
    exit(-1);
  }
  return 0;
}

/* 出错返回 -1 */
int Accept(int sockfd, struct sockaddr_in* addr, socklen_t* addrlen) {
  int ret = accept(sockfd, (sockaddr*)addr, addrlen);
  if (ret < 0) {
    PRINT_ERRNO(accept);
  }
  return ret;
}

/* 出错返回 -1 */
int Connect(int sockfd, const struct sockaddr_in* addr, socklen_t addrlen) {
  if (connect(sockfd, (sockaddr*)addr, addrlen) < 0) {
    PRINT_ERRNO(connect);
  }
  return 0;
}

int Epoll_create(int size) {
  int epfd = epoll_create(size);
  if (epfd < 0) {
    PRINT_ERRNO(epoll_create);
    exit(-1);
  }
  return epfd;
}

int Epoll_ctl(int epfd, int op, int fd, struct epoll_event* event) {
  if (epoll_ctl(epfd, op, fd, event) < 0) {
    PRINT_ERRNO(epoll_ctl);
    exit(-1);
  }
  return 0;
}

int Epoll_wait(int epfd, struct epoll_event* events, int maxevents,
               int timeout) {
  int n = epoll_wait(epfd, events, maxevents, timeout);
  if ((n < 0) && (errno != EINTR)) {
    // 若不是由于中断引起的错误返回，则结束进程
    PRINT_ERRNO(epoll_wait);
    exit(-1);
  }
  return n;
}

int Socketpair(int domain, int type, int protocol, int sv[2]) {
  if (socketpair(domain, type, protocol, sv) < 0) {
    PRINT_ERRNO(socketpair);
    exit(-1);
  }
  return 0;
}

int Sigfillset(sigset_t* set) {
  int ret = sigfillset(set);
  if (ret < 0) {
    PRINT_ERRNO(sigfillset);
    exit(-1);
  }
  return ret;
}

int Sigaction(int signum, const struct sigaction* act,
              struct sigaction* oldact) {
  if (sigaction(signum, act, oldact) < 0) {
    PRINT_ERRNO(sigaction);
    exit(-1);
  }
  return 0;
}

int Open(const char* pathname, int flags) {
  int ret = open(pathname, flags);
  if (ret < 0) {
    PRINT_ERRNO(open);
    exit(-1);
  }
  return ret;
}

int Close(int fd) {
  if (close(fd) < 0) {
    PRINT_ERRNO(close);
    exit(-1);
  }
  return 0;
}

/* 出错返回 -1 */
int Stat(const char* pathname, struct stat* statbuf) {
  if (stat(pathname, statbuf) < 0) {
    PRINT_ERRNO(stat);
    return -1;
  }
  return 0;
}

int Shm_open(const char* name, int oflag, mode_t mode) {
  int ret = shm_open(name, oflag, mode);
  if (ret < 0) {
    PRINT_ERRMSG(shm_open, "something wrong");
    exit(-1);
  }
  return ret;
}

int Shm_unlink(const char* name) {
  if (shm_unlink(name) < 0) {
    PRINT_ERRMSG(shm_unlink, "something wrong");
    exit(-1);
  }
  return 0;
}

void* Mmap(void* addr, size_t length, int prot, int flags, int fd,
           off_t offset) {
  void* ret = mmap(addr, length, prot, flags, fd, offset);
  if (ret == MAP_FAILED) {
    PRINT_ERRNO(mmap);
  }
  return ret;
}

int Munmap(void* addr, size_t length) {
  if (munmap(addr, length) < 0) {
    PRINT_ERRNO(munmap);
    exit(-1);
  }
  return 0;
}

int Ftruncate(int fd, off_t length) {
  if (ftruncate(fd, length) < 0) {
    PRINT_ERRNO(ftruncate);
    exit(-1);
  }
  return 0;
}

int Pipe(int pipefd[2]) {
  if (pipe(pipefd) < 0) {
    PRINT_ERRNO(pipe);
    exit(-1);
  }
  return 0;
}

ssize_t Read(int fd, void* buf, size_t count) {
  ssize_t ret = read(fd, buf, count);
  if (ret < 0) {
    PRINT_ERRNO(read);
    exit(-1);
  }
  return ret;
}

ssize_t Write(int fd, void* buf, size_t count) {
  ssize_t ret = write(fd, buf, count);
  if (ret < 0) {
    PRINT_ERRNO(write);
    exit(-1);
  }
  return ret;
}

/* 出错时返回 -1 */
ssize_t Writev(int fd, const struct iovec* iov, int iovcnt) {
  ssize_t ret = writev(fd, iov, iovcnt);
  if (ret < 0) {
    PRINT_ERRNO(writev);
    return -1;
  }
  return ret;
}

/* 出错时返回 -1 */
ssize_t Readv(int fd, const struct iovec* iov, int iovcnt) {
  ssize_t ret = readv(fd, iov, iovcnt);
  if (ret < 0) {
    PRINT_ERRNO(readv);
    return -1;
  }
  return ret;
}

/* 出错时返回 -1 */
ssize_t Send(int sockfd, const void* buf, size_t len, int flags) {
  ssize_t ret = send(sockfd, buf, len, flags);
  if (ret < 0) {
    PRINT_ERRNO(send);
  }
  return ret;
}

/* 出错时返回 -1 */
ssize_t Recv(int sockfd, void* buf, size_t len, int flags) {
  ssize_t ret = recv(sockfd, buf, len, flags);
  if (ret < -1) {
    PRINT_ERRNO(recv);
  }
  return ret;
}

int Kill(pid_t pid, int sig) {
  if (kill(pid, sig) < 0) {
    PRINT_ERRNO(kill);
    exit(-1);
  }
  return 0;
}

int Pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                   void* (*start_routine)(void*), void* arg) {
  int ret = pthread_create(thread, attr, start_routine, arg);
  if (ret < 0) {
    PRINT_ERRNO(ret);
    exit(-1);
  }
  return ret;
}

int Pthread_detach(pthread_t thread) {
  int ret = pthread_detach(thread);
  if (ret < 0) {
    PRINT_ERRNO(ret);
    exit(-1);
  }
  return 0;
}

/* 设置非阻塞 io */
int SetNonBlocking(int fd) {
  int old_opt = fcntl(fd, F_GETFL);
  int new_opt = old_opt | O_NONBLOCK;
  fcntl(fd, F_SETFL, new_opt);
  return old_opt;
}

/* 将文件描述符 fd 加入到 epoll 事件表中，监听读事件
 * one_shot: 是否采用 one-shot 行为，默认 false
 * triger_mode: 触发模式，0 为 ET，1 为 LT，默认 ET */
void AddFd(int epollfd, int fd, bool one_shot, int triger_mode) {
  epoll_event event;
  event.data.fd = fd;
  event.events = EPOLLIN | EPOLLRDHUP;

  if (triger_mode == 0) event.events |= EPOLLET;
  if (one_shot) event.events |= EPOLLONESHOT;

  Epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
  SetNonBlocking(fd);
}

/* 重设 one-shot，
 * ev 为附加监听事件，最终监听事件为 ev | EPOLLONESHOT | EPOLLRDHUP
 * triger_mode: 触发模式，0 为 ET，1 为 LT，默认 ET */
void ModFd(int epollfd, int fd, int ev, int triger_mode) {
  epoll_event event;
  event.data.fd = fd;
  event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

  if (triger_mode == 0) event.events |= EPOLLET;

  Epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

/* 从 epoll 事件表中删除 fd */
void RemoveFd(int epollfd, int fd) {
  Epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
  Close(fd);
}

/* 设置捕获信号 */
void AddSig(int signum, void (*handler)(int), bool restart) {
  struct sigaction sa;
  bzero(&sa, sizeof(sa));
  sa.sa_handler = handler;
  if (restart) sa.sa_flags |= SA_RESTART;
  Sigfillset(&sa.sa_mask);
  Sigaction(signum, &sa, NULL);
}

/* 发送错误信息 */
void SendError(int connfd, const char* info) {
  printf("%s\n", info);
  Send(connfd, info, strlen(info), 0);
  Close(connfd);
}