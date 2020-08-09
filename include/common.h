#ifndef __COMMON__H__
#define __COMMON__H__

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <wait.h>

#define MAX_FD 65535         // 最大文件描述符
#define MAX_EVENT_NUM 10000  // 最大事件数
#define TIMEOUT 600          // 超时时间

#define PRINT_ERRMSG(func, msg) printf("%s error: %s\n", #func, (msg))
#define PRINT_ERRNO(func) printf("%s error: %s\n", #func, strerror(errno))

enum TriggerMode { ET = 0, LT };

int Fork();

int Dup2(int oldfd, int newfd);

int Socket(int domain, int type, int protocol);

int Bind(int sockfd, const struct sockaddr_in* addr, socklen_t addrlen);

int Listen(int sockfd, int backlog);

/* 出错返回 -1 */
int Accept(int sockfd, struct sockaddr_in* addr, socklen_t* addrlen);

/* 出错返回 -1 */
int Connect(int sockfd, const struct sockaddr_in* addr, socklen_t addrlen);

int Epoll_create(int size);

int Epoll_ctl(int epfd, int op, int fd, epoll_event* event);

int Epoll_wait(int epfd, struct epoll_event* events, int maxevents,
               int timeout);

int Socketpair(int domain, int type, int protocol, int sv[2]);

int Sigfillset(sigset_t* set);

int Sigaction(int signum, const struct sigaction* act,
              struct sigaction* oldact);

int Open(const char* pathname, int flags);

int Close(int fd);

/* 出错返回 -1 */
int Stat(const char* pathname, struct stat* statbuf);

int Shm_open(const char* name, int oflag, mode_t mode);

int Shm_unlink(const char* name);

void* Mmap(void* addr, size_t length, int prot, int flags, int fd,
           off_t offset);

int Munmap(void* addr, size_t length);

int Ftruncate(int fd, off_t length);

int Pipe(int pipefd[2]);

ssize_t Read(int fd, void* buf, size_t count);

ssize_t Write(int fd, void* buf, size_t count);

/* 出错时返回 -1 */
ssize_t Writev(int fd, const struct iovec* iov, int iovcnt);

/* 出错时返回 -1 */
ssize_t Readv(int fd, const struct iovec* iov, int iovcnt);

/* 出错时返回 -1 */
ssize_t Send(int sockfd, const void* buf, size_t len, int flags);

/* 出错时返回 -1 */
ssize_t Recv(int sockfd, void* buf, size_t len, int flags);

int Kill(pid_t pid, int sig);

int Pthread_create(pthread_t* thread, const pthread_attr_t* attr,
                   void* (*start_routine)(void*), void* arg);

int Pthread_detach(pthread_t thread);

/* 设置非阻塞 io */
int SetNonBlocking(int fd);

/* 将文件描述符 fd 加入到 epoll 事件表中，监听读事件
 * one_shot: 是否采用 one-shot 行为，默认 false
 * trigger_mode: 触发模式，0 为 ET，1 为 LT，默认 ET */
void AddFd(int epollfd, int fd, bool one_shot = false,
           TriggerMode trigger_mode = ET);

/* 重设 one-shot，
 * ev 为附加监听事件，最终监听事件为 ev | EPOLLONESHOT | EPOLLRDHUP
 * trigger_mode: 触发模式，0 为 ET，1 为 LT，默认 ET */
void ModFd(int epollfd, int fd, int ev, TriggerMode trigger_mode = ET);

/* 从 epoll 事件表中删除 fd */
void RemoveFd(int epollfd, int fd);

/* 设置捕获信号 */
void AddSig(int signum, void (*handler)(int), bool restart = true);

/* 发送错误信息 */
void SendError(int connfd, const char* info);

#endif  //!__COMMON__H__