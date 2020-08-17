#ifndef __COMMON__H__
#define __COMMON__H__

#include <arpa/inet.h>
#include <dirent.h>
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

#include "logger.h"

#define MAX_FD 65535         // 最大文件描述符
#define MAX_EVENT_NUM 10000  // 最大事件数
#define TIMEOUT 600          // 超时时间

enum TriggerMode { ET = 0, LT };

/* 设置非阻塞 io，成功返回 old_opt，错误返回 -1 */
int SetNonBlocking(int fd);

/* 将文件描述符 fd 加入到 epoll 事件表中，监听读事件
 * one_shot: 是否采用 one-shot 行为，默认 false
 * trigger_mode: 触发模式，0 为 ET，1 为 LT，默认 ET
 * 成功返回 0，错误返回 -1 */
int AddFd(int epollfd, int fd, bool one_shot = false,
          TriggerMode trigger_mode = ET);

/* 重设 one-shot，
 * ev 为附加监听事件，最终监听事件为 ev | EPOLLONESHOT | EPOLLRDHUP
 * trigger_mode: 触发模式，0 为 ET，1 为 LT，默认 ET */
int ModFd(int epollfd, int fd, int ev, TriggerMode trigger_mode = ET);

/* 从 epoll 事件表中删除 fd，成功返回 0，出错返回 -1 */
int RemoveFd(int epollfd, int fd);

/* 设置捕获信号，成功返回 0，错误返回 -1 */
int AddSig(int signum, void (*handler)(int), bool restart = true);

/* 发送错误信息，成功返回 0，错误返回 -1 */
int SendError(int connfd, const char* info);

#endif  //!__COMMON__H__