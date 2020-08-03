#ifndef __HTTP_CONN__H__
#define __HTTP_CONN__H__

#include <netdb.h>

#include <map>
#include <string>
#include <vector>

#include "common.h"
#include "locker.h"
#include "sql_connpool.h"
#include "timer.h"

using std::map;
using std::string;
using std::vector;

extern vector<TimerClientData> g_timer_client_data;  // 定时器用的用户数据
extern TimerHeap g_timer_heap;                       // 堆定时器

class HttpConn {
 public:
  static const int kFileNameLen_ = 200;   // 文件名最大长度
  static const int kReadBufSize_ = 2048;  // 读缓冲区大小
  static const int kWriteBufSize = 2048;  // 写缓冲区大小

  /* HTTP 请求方法 */
  enum Method_ { GET, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH };
  /* 状态机状态 */
  enum CheckState_ {
    CHECK_STATE_REQUESTLINE,
    CHECK_STATE_HEADER,
    CHECK_STATE_CONTENT
  };
  /* 行读取状态 */
  enum LineState_ { LINE_OK, LINE_BAD, LINE_OPEN };
  /* HTTP 请求结果 */
  enum HttpCode_ {
    NO_REQUEST,
    GET_REQUEST,
    BAD_REQUEST,
    NO_RESOURCE,
    FORBIDDEN_REQUEST,
    FILE_REQUEST,
    INTERNAL_ERROR,
    CLOSED_CONNECTION,
    CGI_REQUEST
  };

 public:
  HttpConn() {}
  ~HttpConn() {}

  /* 初始化新接收的连接 */
  void Init(int sockfd, const sockaddr_in& addr, TriggerMode trigger_mode = ET);
  /* 关闭连接 */
  void CloseConn(bool real_close = true);
  /* 处理客户请求 */
  void Process();
  /* 非阻塞读 */
  bool Read();
  /* 非阻塞写 */
  bool Write();
  /* 将用户名密码加载到内存 */
  static void InitSqlResult();

 public:
  /* epoll 内核事件表，所有 socket 事件都注册到同一个事件表，所以设为静态 */
  static int epollfd_;
  /* 统计用户数量 */
  static int user_cnt_;

 private:
  int __sockfd_;                   // 该 HTTP 连接的 socket
  struct sockaddr_in __addr_;      // 客户端 socket 地址
  char __read_buf[kReadBufSize_];  // 读缓冲区
  int __read_idx_;    // 已读客户数据的最后一个字节的下个位置
  int __cur_idx_;     // 当前正在分析的字符位置
  int __start_line_;  // 当前正在解析的行的起始位置
  char __write_buf_[kWriteBufSize];  // 写缓冲区
  int __write_idx_;                  // 写缓冲区中待发送的字节数
  CheckState_ __check_state_;        // 主状态机所处状态
  Method_ __method_;                 // 请求方法
  char __real_file_[kFileNameLen_];  // 客户端请求目标完整路径
  char* __url_;                      // 客户端请求目标的文件名
  char* __version_;                  // HTTP 版本号，只支持 HTTP/1.1
  char* __host_;                     // 主机名
  int __content_length_;             // HTTP 请求消息体的长度
  bool __linger_;                    // 是否保持连接
  char* __file_addr_;  // 客户端请求的目标文件在 mmap 的内存中的起始位置
  struct stat __file_stat_;           // 目标文件的状态
  struct iovec __iov_[2];             // 集中写
  int __iov_cnt_;                     // 被写内存块的数量
  int __bytes_to_send_;               // 待发送字节数
  int __bytes_have_sent_;             // 已发送字节数
  TriggerMode __trigger_mode_;        // epoll 触发模式
  char __cgiret_buf_[kWriteBufSize];  // cgi 返回数据的缓冲区

  string __sql_user_;
  string __sql_passwd_;
  string __sql_name_;

 private:
  /* 初始化连接 */
  void __Init();
  /* 解析 HTTP 请求 */
  HttpCode_ __ProcessRead();
  /* 填充 HTTP 应答 */
  bool __ProcessWrite(HttpCode_ ret);
  /* 以下一组函数由 __ProcessRead() 调用以分析 HTTP 请求 */
  HttpCode_ __ParseRequestLine(char* text);
  HttpCode_ __ParseHeaders(char* text);
  HttpCode_ __ParseContent(char* text);
  HttpCode_ __DoRequest(char* text);
  inline char* __GetLine() { return __read_buf + __start_line_; }
  LineState_ __ParseLine();
  /* 以下一组函数由 __ProcessWrite() 调用以填充 HTTP 应答 */
  void __Unmap();
  bool __AddResponse(const char* format, ...);
  bool __AddContent(const char* content);
  bool __AddStatusLine(int status, const char* title);
  bool __AddHeaders(int content_length);
  bool __AddContentLength(int content_length);
  bool __AddLinger();
  bool __AddBlankLine();
  /* 调整 __iov_ 内容 */
  void __AdjustIov();
  /* 登录、注册、提取用户名密码 */
  bool __Login(char* basename);
  bool __Regist(char* basename);
  bool __GetUserPasswd(char* username, char* passwd);
  /* Python 在线环境 */
  HttpCode_ __RunPython(char* text);
};

#endif  //!__HTTP_CONN__H__