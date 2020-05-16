#ifndef __HTTP_CONN__H__
#define __HTTP_CONN__H__

#include "conmmon.h"
#include "locker.h"

class http_conn {
 public:
  static const int FILENAME_LEN = 200;     // 文件名最大长度
  static const int READ_BUF_SIZE = 2048;   // 读缓冲区大小
  static const int WRITE_BUF_SIZE = 2048;  // 写缓冲区大小

  /* HTTP 请求方法 */
  enum METHOD { GET, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH };
  /* 状态机状态 */
  enum CHECK_STATE {
    CHECK_STATE_REQUESTLINE,
    CHECK_STATE_HEADER,
    CHECK_STATE_CONTENT
  };
  /* 行读取状态 */
  enum LINE_STATE { LINE_OK, LINE_BAD, LINE_OPEN };
  /* HTTP 请求结果 */
  enum HTTP_CODE {
    NO_REQUEST,
    GET_REQUEST,
    BAD_REQUEST,
    NO_RESOURCE,
    FORBIDDEN_REQUEST,
    FILE_REQUEST,
    INTERNAL_ERROR,
    CLOSED_CONNECTION
  };

 public:
  http_conn() {}
  ~http_conn() {}

  /* 初始化新接收的连接 */
  void init(int sockfd, const sockaddr_in& addr);
  /* 关闭连接 */
  void close_conn(bool real_close = true);
  /* 处理客户请求 */
  void process();
  /* 非阻塞读 */
  bool read();
  /* 非阻塞写 */
  bool write();

 public:
  /* epoll 内核事件表，所有 socket 事件都注册到同一个事件表，所以设为静态 */
  static int epollfd;
  /* 统计用户数量 */
  static int user_cnt;

 private:
  int __sockfd;                    // 该 HTTP 连接的 socket
  struct sockaddr_in __addr;       // 客户端 socket 地址
  char __read_buf[READ_BUF_SIZE];  // 读缓冲区
  int __read_idx;    // 已读客户数据的最后一个字节的下个位置
  int __cur_idx;     // 当前正在分析的字符位置
  int __start_line;  // 当前正在解析的行的起始位置
  char __write_buf[WRITE_BUF_SIZE];  // 写缓冲区
  int __write_idx;                   // 写缓冲区中待发送的字节数
  CHECK_STATE __check_state;         // 主状态机所处状态
  METHOD __method;                   // 请求方法
  char __real_file[FILENAME_LEN];    // 客户端请求目标完整路径
  char* __url;                       // 客户端请求目标的文件名
  char* __version;                   // HTTP 版本号，只支持 HTTP/1.1
  char* __host;                      // 主机名
  int __content_length;              // HTTP 请求消息体的长度
  bool __linger;                     // 是否保持连接
  char* __file_addr;  // 客户端请求的目标文件在 mmap 的内存中的起始位置
  struct stat __file_stat;  // 目标文件的状态
  struct iovec __iv[2];     // 集中写
  int __iv_cnt;             // 被写内存块的数量
  int __bytes_to_send;      // 待发送字节数
  int __bytes_have_sent;    // 已发送字节数

 private:
  /* 初始化连接 */
  void __init();
  /* 解析 HTTP 请求 */
  HTTP_CODE __process_read();
  /* 填充 HTTP 应答 */
  bool __process_write(HTTP_CODE ret);
  /* 以下一组函数由 __process_read() 调用以分析 HTTP 请求 */
  HTTP_CODE __parse_request_line(char* text);
  HTTP_CODE __parse_headers(char* text);
  HTTP_CODE __parse_content(char* text);
  HTTP_CODE __do_request();
  inline char* __get_line() { return __read_buf + __start_line; }
  LINE_STATE __parse_line();
  /* 以下一组函数由 __process_write() 调用以填充 HTTP 应答 */
  void __unmap();
  bool __add_response(const char* format, ...);
  bool __add_content(const char* content);
  bool __add_status_line(int status, const char* title);
  bool __add_headers(int content_length);
  bool __add_content_length(int content_length);
  bool __add_linger();
  bool __add_blank_line();
};

#endif  //!__HTTP_CONN__H__