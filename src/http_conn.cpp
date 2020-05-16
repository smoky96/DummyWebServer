#include "http_conn.h"

/* 定义 HTTP 响应的状态信息 */
const char *ok_200_title = "OK";

const char *error_400_title = "Bad Request";

const char *error_400_form =
    "Your request has bad syntax or is inherently impossible to staisfy.\n";

const char *error_403_title = "Forbidden";

const char *error_403_form =
    "You do not have permission to get file form this server.\n";

const char *error_404_title = "Not Found";

const char *error_404_form =
    "The requested file was not found on this server.\n";

const char *error_500_title = "Internal Error";

const char *error_500_form =
    "There was an unusual problem serving the request file.\n";

/* 网站根目录 */
const char *doc_root = "/home/dong/blog/";

int http_conn::user_cnt = 0;
int http_conn::epollfd = -1;

void http_conn::close_conn(bool real_close) {
  if (real_close && (__sockfd != -1)) {
    removefd(epollfd, __sockfd);
    __sockfd = -1;
    --user_cnt;
  }
}

void http_conn::init(int sockfd, const sockaddr_in &addr) {
  __sockfd = sockfd;
  __addr = addr;
  addfd(epollfd, sockfd, true);
  ++user_cnt;
  __init();
}

void http_conn::__init() {
  __check_state = CHECK_STATE_REQUESTLINE;
  __linger = false;
  __method = GET;
  __url = 0;
  __version = 0;
  __content_length = 0;
  __host = 0;
  __start_line = 0;
  __cur_idx = 0;
  __read_idx = 0;
  __write_idx = 0;
  __bytes_to_send = 0;
  __bytes_have_sent = 0;
  memset(__read_buf, '\0', READ_BUF_SIZE);
  memset(__write_buf, '\0', WRITE_BUF_SIZE);
  memset(__real_file, '\0', FILENAME_LEN);
}

/* 从状态机 */
http_conn::LINE_STATE http_conn::__parse_line() {
  char tmp;
  for (; __cur_idx < __read_idx; ++__cur_idx) {
    tmp = __read_buf[__cur_idx];
    if (tmp == '\r') {
      if ((__cur_idx + 1) == __read_idx) {
        /* 还需要继续读 */
        return LINE_OPEN;
      } else if (__read_buf[__cur_idx + 1] == '\n') {
        /* 一行读完了，将 '\r\n' 变为 '\0\0' */
        __read_buf[__cur_idx++] = '\0';
        __read_buf[__cur_idx++] = '\0';
        return LINE_OK;
      }
      /* 请求有问题 */
      return LINE_BAD;
    } else if (tmp == '\n') {
      if (__cur_idx > 1 && __read_buf[__cur_idx - 1] == '\r') {
        /* 一行读完了，将 '\r\n' 变为 '\0\0' */
        __read_buf[__cur_idx - 1] = '\0';
        __read_buf[__cur_idx++] = '\0';
        return LINE_OK;
      }
      return LINE_BAD;
    }
  }
  /* 还要继续读 */
  return LINE_OPEN;
}

/* 循环读取客户数据，直到无数据可读或对方关闭连接 */
bool http_conn::read() {
  if (__read_idx >= READ_BUF_SIZE) {
    /* 缓存区溢出 */
    return false;
  }

  int bytes_read = 0;
  while (1) {
    bytes_read =
        Recv(__sockfd, __read_buf + __read_idx, READ_BUF_SIZE - __read_idx, 0);
    if (bytes_read == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        /* 非阻塞 */
        break;
      }
      return false;
    } else if (bytes_read == 0) {
      /* 对方关闭连接 */
      return false;
    } else {
      __read_idx += bytes_read;
    }
  }
  return true;
}

/* 解析 HTTP 请求行，获取请求方法、目标 URL、HTTP 版本号 */
http_conn::HTTP_CODE http_conn::__parse_request_line(char *text) {
  /* 在 text 中找到第一个 ' ' 或 '\t' 出现的位置 */
  __url = strpbrk(text, " \t");
  if (!__url) {
    /* 没找到分隔符 */
    return BAD_REQUEST;
  }
  /* 将分隔符改为 '\0' */
  *(__url++) = '\0';
  char *method = text;
  if (strcasecmp(method, "GET") == 0) {
    __method = GET;
  } else {
    /* 暂时只支持 GET 方法 */
    return BAD_REQUEST;
  }

  /* 将 __url 前进到下一字段，下字段内容即为 url */
  __url += strspn(__url, " \t");  // 返回第一个不含 ' ' 或 '\t' 的下标
  /* 最后一字段为 HTTP 版本号，将 __version 指向最后一段 */
  __version = strpbrk(__url, " \t");
  if (!__version) {
    return BAD_REQUEST;
  }
  *(__version++) = '\0';
  __version += strspn(__version, " \t");
  if (strcasecmp(__version, "HTTP/1.1") != 0) {
    return BAD_REQUEST;
  }

  if (strncasecmp(__url, "http://", 7) == 0) {
    /* 直接跳到资源位置 ("http://www.xxxx.com/resource") */
    __url += 7;
    __url = strchr(__url, '/');
  }
  if (!__url || __url[0] != '/') {
    /* 没有给出请求的资源 */
    return BAD_REQUEST;
  }
  /* 请求行解析完毕，状态转化为解析请求头 */
  __check_state = CHECK_STATE_HEADER;
  return NO_REQUEST;
}

/* 解析 HTTP 请求的一个头部信息 */
http_conn::HTTP_CODE http_conn::__parse_headers(char *text) {
  /* 空行表示头部字段解析完毕 */
  if (text[0] == '\0') {
    /* 如果 HTTP 请求有消息体，则还需读取 __content_length 字节的消息体，
     * 且状态转换为 CHECK_STATE_CONTENT 状态 */
    if (__content_length != 0) {
      __check_state = CHECK_STATE_CONTENT;
      return NO_REQUEST;
    }
    /* 已获得一个完整的 HTTP 请求 */
    return GET_REQUEST;
  } else if (strncasecmp(text, "Connection:", 11) == 0) {
    /* 处理 Connection 字段 */
    text += 11;
    text += strspn(text, " \t");
    if (strcasecmp(text, "keep-alive") == 0) {
      __linger = true;
    }
  } else if (strncasecmp(text, "Content-Length:", 15) == 0) {
    /* 处理 Content-Length 字段 */
    text += 15;
    text += strspn(text, " \t");
    __content_length = atol(text);
  } else if (strncasecmp(text, "Host:", 5) == 0) {
    /* 处理 Host 字段 */
    text += 5;
    text += strspn(text, " \t");
    __host = text;
  } else {
    printf("unknown header: %s\n", text);
  }
  return NO_REQUEST;
}

/* 判断 HTTP 请求的消息体是否完整读入 */
http_conn::HTTP_CODE http_conn::__parse_content(char *text) {
  if (__read_idx >= (__content_length + __cur_idx)) {
    text[__content_length] = '\0';
    return GET_REQUEST;
  }
  return NO_REQUEST;
}

/* 主状态机 */
http_conn::HTTP_CODE http_conn::__process_read() {
  LINE_STATE line_status = LINE_OK;
  HTTP_CODE ret = NO_REQUEST;
  char *text = 0;
  while ((__check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
         ((line_status = __parse_line()) == LINE_OK)) {
    text = __get_line();
    __start_line = __cur_idx;
    printf("got 1 http line: %s\n", text);
    switch (__check_state) {
      case CHECK_STATE_REQUESTLINE:
        ret = __parse_request_line(text);
        if (ret == BAD_REQUEST) {
          return BAD_REQUEST;
        }
        break;
      case CHECK_STATE_HEADER:
        ret = __parse_headers(text);
        if (ret == BAD_REQUEST) {
          return BAD_REQUEST;
        }
        if (ret == GET_REQUEST) {
          return __do_request();
        }
        break;
      case CHECK_STATE_CONTENT:
        ret = __parse_content(text);
        if (ret == GET_REQUEST) {
          return __do_request();
        }
        line_status = LINE_OPEN;
        break;
      default:
        return INTERNAL_ERROR;
    }
  }
  /* 还没有完整的 HTTP 请求 */
  return NO_REQUEST;
}

/* 得到完整 HTTP 请求后，分析目标文件属性，将其映射到内存地址 __file_addr 处 */
http_conn::HTTP_CODE http_conn::__do_request() {
  strcpy(__real_file, doc_root);
  int len = strlen(doc_root);
  strncpy(__real_file + len, __url, FILENAME_LEN - len - 1);
  if (Stat(__real_file, &__file_stat) < 0) {
    return NO_RESOURCE;
  }
  if (!(__file_stat.st_mode & S_IROTH)) {
    return FORBIDDEN_REQUEST;
  }
  if (S_ISDIR(__file_stat.st_mode)) {
    return BAD_REQUEST;
  }
  int fd = Open(__real_file, O_RDONLY);
  __file_addr =
      (char *)Mmap(NULL, __file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  Close(fd);
  return FILE_REQUEST;
}

/* 对内存映射区执行 munmap 操作 */
void http_conn::__unmap() {
  if (__file_addr) {
    Munmap(__file_addr, __file_stat.st_size);
    __file_addr = nullptr;
  }
}

/* 写 HTTP 响应 */
bool http_conn::write() {
  int tmp = 0;
  printf("---debug---%d bytes to send\n", __bytes_to_send);
  if (__bytes_to_send == 0) {
    modfd(epollfd, __sockfd, EPOLLIN);
    __init();
    return true;
  }
  while (1) {
    tmp = Writev(__sockfd, &__iv[0], __iv_cnt);
    if (tmp < 0) {
      if (errno == EAGAIN) {
        if (__bytes_have_sent >= __write_idx) {
          __iv[0].iov_len = 0;
          __iv[1].iov_base = __file_addr + (__bytes_have_sent - __write_idx);
          __iv[1].iov_len = __bytes_to_send;
        } else {
          __iv[0].iov_base = __write_buf + __bytes_have_sent;
          __iv[0].iov_len = __write_idx - __bytes_have_sent;
        }
        /* 若写缓冲区没有空间，则等待缓冲区可写，在此期间无法再接收客户端请求
         */
        modfd(epollfd, __sockfd, EPOLLOUT);
        return true;
      }
      __unmap();
      return false;
    }
    printf("---debug---sent %d bytes\n", tmp);
    __bytes_to_send -= tmp;
    __bytes_have_sent += tmp;
    if (__bytes_to_send <= 0) {
      __unmap();
      /* HTTP 响应发送成功，根据 Connection 字段决定是否立即关闭连接 */
      if (__linger) {
        __init();
        modfd(epollfd, __sockfd, EPOLLIN);
        return true;
      } else {
        modfd(epollfd, __sockfd, EPOLLIN);
        return false;
      }
    }
  }
}

/* 往写缓冲区中写入待发送的数据 */
bool http_conn::__add_response(const char *format, ...) {
  if (__write_idx >= WRITE_BUF_SIZE) {
    return false;
  }

  va_list arg_list;
  va_start(arg_list, format);
  int len = vsnprintf(__write_buf + __write_idx,
                      WRITE_BUF_SIZE - __write_idx - 1, format, arg_list);
  if (len >= (WRITE_BUF_SIZE - __write_idx - 1)) {
    return false;
  }
  __write_idx += len;
  va_end(arg_list);
  return true;
}

bool http_conn::__add_status_line(int status, const char *title) {
  return __add_response("%s%d%s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::__add_headers(int content_len) {
  if (!__add_content_length(content_len)) return false;
  if (!__add_linger()) return false;
  if (!__add_blank_line()) return false;
  return true;
}

bool http_conn::__add_content_length(int content_len) {
  return __add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::__add_linger() {
  return __add_response("Connection: %s\r\n",
                        (__linger == true) ? "keep-alive" : "close");
}

bool http_conn::__add_blank_line() { return __add_response("%s", "\r\n"); }

bool http_conn::__add_content(const char *content) {
  return __add_response("%s", content);
}

/* 根据服务器处理 HTTP 请求的结果，决定返回给客户端的内容 */
bool http_conn::__process_write(HTTP_CODE ret) {
  switch (ret) {
    case INTERNAL_ERROR:
      __add_status_line(500, error_500_title);
      __add_headers(strlen(error_500_form));
      if (!__add_content(error_500_form)) return false;
      break;
    case BAD_REQUEST:
      __add_status_line(400, error_400_title);
      __add_headers(strlen(error_400_form));
      if (!__add_content(error_400_form)) return false;
      break;
    case NO_RESOURCE:
      __add_status_line(404, error_404_title);
      __add_headers(strlen(error_404_form));
      if (!__add_content(error_404_form)) return false;
      break;
    case FORBIDDEN_REQUEST:
      __add_status_line(403, error_403_title);
      __add_headers(strlen(error_403_form));
      if (!__add_content(error_403_form)) return false;
      break;
    case FILE_REQUEST:
      __add_status_line(200, ok_200_title);
      if (__file_stat.st_size != 0) {
        __add_headers(__file_stat.st_size);
        __iv[0].iov_base = __write_buf;
        __iv[0].iov_len = __write_idx;
        __iv[1].iov_base = __file_addr;
        __iv[1].iov_len = __file_stat.st_size;
        __bytes_to_send = __write_idx + __file_stat.st_size;
        __iv_cnt = 2;
        return true;
      } else {
        const char *ok_string = "<html><body></body></html>";
        __add_headers(strlen(ok_string));
        if (!__add_content(ok_string)) return false;
      }
      break;
    default:
      return false;
  }
  __iv[0].iov_base = __write_buf;
  __iv[0].iov_len = __write_idx;
  __iv_cnt = 1;
  __bytes_to_send = __write_idx;
  return true;
}

/* 有线程池中的工作线程调用，是处理 HTTP 请求的入口函数 */
void http_conn::process() {
  HTTP_CODE read_ret = __process_read();
  if (read_ret == NO_REQUEST) {
    /* 还没收到完整请求，继续监听 */
    modfd(epollfd, __sockfd, EPOLLIN);
    return;
  }
  bool write_ret = __process_write(read_ret);
  if (!write_ret) {
    /* 出错关闭连接 */
    close_conn();
  }
  /* 监听是否可写 */
  modfd(epollfd, __sockfd, EPOLLOUT);
}