#include "http_conn.h"

#include "urlcode.h"

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
const char *doc_root = "root/";
const char *default_page = "/index.html";

int HttpConn::user_cnt_ = 0;
int HttpConn::epollfd_ = -1;

static map<string, string> users;  // 所用用户名和密码
static Locker locker;              // 用户名数据加锁

void HttpConn::CloseConn(bool real_close) {
  if (real_close && (__sockfd_ != -1)) {
    RemoveFd(epollfd_, __sockfd_);
    __sockfd_ = -1;
    --user_cnt_;
  }
}

void HttpConn::Init(int sockfd, const sockaddr_in &addr,
                    TrigerMode triger_mode) {
  __sockfd_ = sockfd;
  __addr_ = addr;
  __triger_mode_ = triger_mode;
  AddFd(epollfd_, sockfd, true, __triger_mode_);
  ++user_cnt_;
  __Init();
}

void HttpConn::__Init() {
  __check_state_ = CHECK_STATE_REQUESTLINE;
  __linger_ = false;
  __method_ = GET;
  __url_ = 0;
  __version_ = 0;
  __content_length_ = 0;
  __host_ = 0;
  __start_line_ = 0;
  __cur_idx_ = 0;
  __read_idx_ = 0;
  __write_idx_ = 0;
  __bytes_to_send_ = 0;
  __bytes_have_sent_ = 0;
  memset(__read_buf, '\0', kReadBufSize_);
  memset(__write_buf_, '\0', kWriteBufSize);
  memset(__real_file_, '\0', kFileNameLen_);
}

/* 从状态机 */
HttpConn::LineState_ HttpConn::__ParseLine() {
  char tmp;
  for (; __cur_idx_ < __read_idx_; ++__cur_idx_) {
    tmp = __read_buf[__cur_idx_];
    if (tmp == '\r') {
      if ((__cur_idx_ + 1) == __read_idx_) {
        /* 还需要继续读 */
        return LINE_OPEN;
      } else if (__read_buf[__cur_idx_ + 1] == '\n') {
        /* 一行读完了，将 '\r\n' 变为 '\0\0' */
        __read_buf[__cur_idx_++] = '\0';
        __read_buf[__cur_idx_++] = '\0';
        return LINE_OK;
      }
      /* 请求有问题 */
      return LINE_BAD;
    } else if (tmp == '\n') {
      if (__cur_idx_ > 1 && __read_buf[__cur_idx_ - 1] == '\r') {
        /* 一行读完了，将 '\r\n' 变为 '\0\0' */
        __read_buf[__cur_idx_ - 1] = '\0';
        __read_buf[__cur_idx_++] = '\0';
        return LINE_OK;
      }
      return LINE_BAD;
    }
  }
  /* 还要继续读 */
  return LINE_OPEN;
}

/* 循环读取客户数据，直到无数据可读或对方关闭连接 */
bool HttpConn::Read() {
  if (__read_idx_ >= kReadBufSize_) {
    /* 缓存区溢出 */
    return false;
  }

  int bytes_read = 0;
  if (__triger_mode_ == ET) {
    while (1) {
      bytes_read = Recv(__sockfd_, __read_buf + __read_idx_,
                        kReadBufSize_ - __read_idx_, 0);
      if (bytes_read == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          /* 非阻塞 */
          break;
        }
        SendError(__sockfd_, "Internal read error");
        return false;
      } else if (bytes_read == 0) {
        /* 对方关闭连接 */
        return false;
      } else {
        __read_idx_ += bytes_read;
      }
    }
  } else {
    bytes_read = Recv(__sockfd_, __read_buf + __read_idx_,
                      kReadBufSize_ - __read_idx_, 0);
    if (bytes_read == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        /* 非阻塞 */
        return true;
      }
      SendError(__sockfd_, "Internal read error");
      return false;
    } else if (bytes_read == 0) {
      /* 对方关闭连接 */
      return false;
    } else {
      __read_idx_ += bytes_read;
    }
  }
  return true;
}

/* 解析 HTTP 请求行，获取请求方法、目标 URL、HTTP 版本号 */
HttpConn::HttpCode_ HttpConn::__ParseRequestLine(char *text) {
  /* 在 text 中找到第一个 ' ' 或 '\t' 出现的位置 */
  __url_ = strpbrk(text, " \t");
  if (!__url_) {
    /* 没找到分隔符 */
    return BAD_REQUEST;
  }
  /* 将分隔符改为 '\0' */
  *(__url_++) = '\0';
  char *method = text;
  if (strcasecmp(method, "GET") == 0) {
    __method_ = GET;
  } else if (strcasecmp(method, "POST") == 0) {
    __method_ = POST;
  } else {
    /* 暂时只支持 GET 方法 */
    return BAD_REQUEST;
  }

  /* 将 __url 前进到下一字段，下字段内容即为 url */
  __url_ += strspn(__url_, " \t");  // 返回第一个不含 ' ' 或 '\t' 的下标
  /* 最后一字段为 HTTP 版本号，将 __version 指向最后一段 */
  __version_ = strpbrk(__url_, " \t");
  if (!__version_) {
    return BAD_REQUEST;
  }
  *(__version_++) = '\0';
  __version_ += strspn(__version_, " \t");
  if (strcasecmp(__version_, "HTTP/1.1") != 0) {
    return BAD_REQUEST;
  }

  if (strncasecmp(__url_, "http://", 7) == 0) {
    /* 直接跳到资源位置 ("http://www.xxxx.com/resource") */
    __url_ += 7;
    __url_ = strchr(__url_, '/');
  }
  if (!__url_ || __url_[0] != '/') {
    /* 没有给出请求的资源 */
    return BAD_REQUEST;
  }
  /* 请求行解析完毕，状态转化为解析请求头 */
  __check_state_ = CHECK_STATE_HEADER;
  return NO_REQUEST;
}

/* 解析 HTTP 请求的一个头部信息 */
HttpConn::HttpCode_ HttpConn::__ParseHeaders(char *text) {
  /* 空行表示头部字段解析完毕 */
  if (text[0] == '\0') {
    /* 如果 HTTP 请求有消息体，则还需读取 __content_length_ 字节的消息体，
     * 且状态转换为 CHECK_STATE_CONTENT 状态 */
    if (__content_length_ != 0) {
      __check_state_ = CHECK_STATE_CONTENT;
      return NO_REQUEST;
    }
    /* 已获得一个完整的 HTTP 请求 */
    return GET_REQUEST;
  } else if (strncasecmp(text, "Connection:", 11) == 0) {
    /* 处理 Connection 字段 */
    text += 11;
    text += strspn(text, " \t");
    if (strcasecmp(text, "keep-alive") == 0) {
      __linger_ = true;
    }
  } else if (strncasecmp(text, "Content-Length:", 15) == 0) {
    /* 处理 Content-Length 字段 */
    text += 15;
    text += strspn(text, " \t");
    __content_length_ = atol(text);
  } else if (strncasecmp(text, "Host:", 5) == 0) {
    /* 处理 Host 字段 */
    text += 5;
    text += strspn(text, " \t");
    __host_ = text;
  } else {
    printf("unknown header: %s\n", text);
  }
  return NO_REQUEST;
}

/* 判断 HTTP 请求的消息体是否完整读入 */
HttpConn::HttpCode_ HttpConn::__ParseContent(char *text) {
  if (__read_idx_ >= (__content_length_ + __cur_idx_)) {
    text[__content_length_] = '\0';
    return GET_REQUEST;
  }
  return NO_REQUEST;
}

/* 主状态机 */
HttpConn::HttpCode_ HttpConn::__ProcessRead() {
  LineState_ line_status = LINE_OK;
  HttpCode_ ret = NO_REQUEST;
  char *text = 0;
  while ((__check_state_ == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
         ((line_status = __ParseLine()) == LINE_OK)) {
    text = __GetLine();
    __start_line_ = __cur_idx_;
    printf("got 1 http line: %s\n", text);
    switch (__check_state_) {
      case CHECK_STATE_REQUESTLINE:
        ret = __ParseRequestLine(text);
        if (ret == BAD_REQUEST) {
          return BAD_REQUEST;
        }
        break;
      case CHECK_STATE_HEADER:
        ret = __ParseHeaders(text);
        if (ret == BAD_REQUEST) {
          return BAD_REQUEST;
        }
        if (ret == GET_REQUEST) {
          return __DoRequest();
        }
        break;
      case CHECK_STATE_CONTENT:
        ret = __ParseContent(text);
        if (ret == GET_REQUEST) {
          return __DoRequest();
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

/* 得到完整 HTTP 请求后，分析目标文件属性，将其映射到内存地址 __file_addr_ 处 */
HttpConn::HttpCode_ HttpConn::__DoRequest() {
  strcpy(__real_file_, doc_root);
  int len = strlen(doc_root);
  char buf[kFileNameLen_ - len];
  /* url 解码 */
  if (UrlDecode(__url_, buf, kFileNameLen_ - len) < 0) {
    return BAD_REQUEST;
  }
  /* 获取请求参数 */
  char *arg = strchr(buf, '?');
  if (arg) *(arg++) = '\0';

  strncpy(__real_file_ + len - 1, buf, kFileNameLen_ - len);

  if (__method_ == POST) {
    /* 提取 POST 参数 */
    char username[100];
    char password[100];
    do {
      char *tmp = strpbrk(&__read_buf[__cur_idx_], "=");
      if (tmp == nullptr) {
        username[0] = '\0';
        break;
      }
      int i = 0;
      for (; *(++tmp) != '&'; ++i) {
        username[i] = *tmp;
      }
      username[i] = '\0';
      tmp = strpbrk(tmp, "=");
      if (tmp == nullptr) {
        password[0] = '\0';
        break;
      }
      for (i = 0; *(++tmp) != '\0'; ++i) {
        password[i] = *tmp;
      }
      password[i] = '\0';
    } while (0);

    char *basename = strrchr(__real_file_, '/');
    ++basename;
    if (strcasecmp(basename, "sqllogin") == 0) {  // 登录按钮
      if (users.find(username) != users.end() && users[username] == password)
        strcpy(basename, "welcome.html");
      else
        strcpy(basename, "login_error.html");
    } else if (strcasecmp(basename, "sqlregister") == 0) {  // 注册按钮
      char sql_statement[300];
      sprintf(sql_statement,
              "INSERT INTO user(username, passwd) VALUES('%s', '%s')", username,
              password);
      if (users.count(username)) {
        strcpy(basename, "register_error.html");
      } else {
        MYSQL *mysql;
        ConnectionRaii conn(mysql);
        locker.Lock();
        int result = mysql_query(mysql, sql_statement);
        users.insert(std::make_pair(username, password));
        locker.Unlock();

        if (!result)
          strcpy(basename, "login.html");
        else
          strcpy(basename, "register_error.html");
      }
    } else if (strcasecmp(basename, "login") == 0) {  // 进入登录页面
      strcpy(basename, "login.html");
    } else if (strcasecmp(basename, "register") == 0) {  // 进入注册页面
      strcpy(basename, "register.html");
    }
  }

  if (Stat(__real_file_, &__file_stat_) < 0) {
    return NO_RESOURCE;
  }
  /* 若请求未指定文件，则将目录下的 default_page 文件返回给客户端 */
  if (S_ISDIR(__file_stat_.st_mode)) {
    len = strlen(__real_file_);
    strncpy(__real_file_ + len, default_page, kFileNameLen_ - len - 1);
    if (Stat(__real_file_, &__file_stat_) < 0) {
      return NO_RESOURCE;
    }
  }
  if (!(__file_stat_.st_mode & S_IROTH)) {
    return FORBIDDEN_REQUEST;
  }
  if (S_ISDIR(__file_stat_.st_mode)) {
    return BAD_REQUEST;
  }
  int fd = Open(__real_file_, O_RDONLY);
  __file_addr_ =
      (char *)Mmap(NULL, __file_stat_.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  Close(fd);
  return FILE_REQUEST;
}

/* 对内存映射区执行 munmap 操作 */
void HttpConn::__Unmap() {
  if (__file_addr_) {
    Munmap(__file_addr_, __file_stat_.st_size);
    __file_addr_ = nullptr;
  }
}

/* 每次调用 writev 都需要调整 __iov_ */
void HttpConn::__AdjustIov() {
  if (__bytes_have_sent_ >= __write_idx_) {
    __iov_[0].iov_len = 0;
    __iov_[1].iov_base = __file_addr_ + (__bytes_have_sent_ - __write_idx_);
    __iov_[1].iov_len = __bytes_to_send_;
  } else {
    __iov_[0].iov_base = __write_buf_ + __bytes_have_sent_;
    __iov_[0].iov_len = __write_idx_ - __bytes_have_sent_;
  }
}

/* 写 HTTP 响应 */
bool HttpConn::Write() {
  int tmp = 0;
  if (__bytes_to_send_ == 0) {
    ModFd(epollfd_, __sockfd_, EPOLLIN, __triger_mode_);
    __Init();
    return true;
  }
  if (__triger_mode_ == ET) {
    while (1) {
      tmp = Writev(__sockfd_, __iov_, __iov_cnt_);
      if (tmp < 0) {
        if (errno == EAGAIN) {
          /* 若写缓冲区没有空间，则等待缓冲区可写，在此期间无法接收客户端请求 */
          ModFd(epollfd_, __sockfd_, EPOLLOUT, __triger_mode_);
          return true;
        }
        __Unmap();
        SendError(__sockfd_, "Internal write error");
        return false;
      }
      __bytes_to_send_ -= tmp;
      __bytes_have_sent_ += tmp;
      __AdjustIov();

      if (__bytes_to_send_ <= 0) {
        __Unmap();
        /* HTTP 响应发送成功，根据 Connection 字段决定是否立即关闭连接 */
        if (__linger_) {
          __Init();
          ModFd(epollfd_, __sockfd_, EPOLLIN, __triger_mode_);
          return true;
        } else {
          ModFd(epollfd_, __sockfd_, EPOLLIN, __triger_mode_);
          return false;
        }
      }
    }
  } else {
    tmp = Writev(__sockfd_, __iov_, __iov_cnt_);
    if (tmp < 0) {
      if (errno == EAGAIN) {
        /* 若写缓冲区没有空间，则等待缓冲区可写，在此期间无法接收客户端请求 */
        ModFd(epollfd_, __sockfd_, EPOLLOUT, __triger_mode_);
        return true;
      }
      __Unmap();
      SendError(__sockfd_, "Internal write error");
      return false;
    }
    __bytes_to_send_ -= tmp;
    __bytes_have_sent_ += tmp;
    __AdjustIov();

    if (__bytes_to_send_ <= 0) {
      __Unmap();
      /* HTTP 响应发送成功，根据 Connection 字段决定是否立即关闭连接 */
      if (__linger_) {
        __Init();
        ModFd(epollfd_, __sockfd_, EPOLLIN, __triger_mode_);
        return true;
      } else {
        ModFd(epollfd_, __sockfd_, EPOLLIN, __triger_mode_);
        return false;
      }
    } else {
      ModFd(epollfd_, __sockfd_, EPOLLIN, __triger_mode_);
      return true;
    }
  }
  return false;
}

/* 往写缓冲区中写入待发送的数据 */
bool HttpConn::__AddResponse(const char *format, ...) {
  if (__write_idx_ >= kWriteBufSize) {
    return false;
  }

  va_list arg_list;
  va_start(arg_list, format);
  int len = vsnprintf(__write_buf_ + __write_idx_,
                      kWriteBufSize - __write_idx_ - 1, format, arg_list);
  if (len >= (kWriteBufSize - __write_idx_ - 1)) {
    return false;
  }
  __write_idx_ += len;
  va_end(arg_list);
  return true;
}

bool HttpConn::__AddStatusLine(int status, const char *title) {
  return __AddResponse("%s%d%s\r\n", "HTTP/1.1", status, title);
}

bool HttpConn::__AddHeaders(int content_len) {
  if (!__AddContentLength(content_len)) return false;
  if (!__AddLinger()) return false;
  if (!__AddBlankLine()) return false;
  return true;
}

bool HttpConn::__AddContentLength(int content_len) {
  return __AddResponse("Content-Length: %d\r\n", content_len);
}

bool HttpConn::__AddLinger() {
  return __AddResponse("Connection: %s\r\n",
                       (__linger_ == true) ? "keep-alive" : "close");
}

bool HttpConn::__AddBlankLine() { return __AddResponse("%s", "\r\n"); }

bool HttpConn::__AddContent(const char *content) {
  return __AddResponse("%s", content);
}

/* 根据服务器处理 HTTP 请求的结果，决定返回给客户端的内容 */
bool HttpConn::__ProcessWrite(HttpCode_ ret) {
  switch (ret) {
    case INTERNAL_ERROR:
      __AddStatusLine(500, error_500_title);
      __AddHeaders(strlen(error_500_form));
      if (!__AddContent(error_500_form)) return false;
      break;
    case BAD_REQUEST:
      __AddStatusLine(400, error_400_title);
      __AddHeaders(strlen(error_400_form));
      if (!__AddContent(error_400_form)) return false;
      break;
    case NO_RESOURCE:
      __AddStatusLine(404, error_404_title);
      __AddHeaders(strlen(error_404_form));
      if (!__AddContent(error_404_form)) return false;
      break;
    case FORBIDDEN_REQUEST:
      __AddStatusLine(403, error_403_title);
      __AddHeaders(strlen(error_403_form));
      if (!__AddContent(error_403_form)) return false;
      break;
    case FILE_REQUEST:
      __AddStatusLine(200, ok_200_title);
      if (__file_stat_.st_size != 0) {
        __AddHeaders(__file_stat_.st_size);
        __iov_[0].iov_base = __write_buf_;
        __iov_[0].iov_len = __write_idx_;
        __iov_[1].iov_base = __file_addr_;
        __iov_[1].iov_len = __file_stat_.st_size;
        __bytes_to_send_ = __write_idx_ + __file_stat_.st_size;
        __iov_cnt_ = 2;
        return true;
      } else {
        const char *ok_string = "<html><body></body></html>";
        __AddHeaders(strlen(ok_string));
        if (!__AddContent(ok_string)) return false;
      }
      break;
    default:
      return false;
  }
  __iov_[0].iov_base = __write_buf_;
  __iov_[0].iov_len = __write_idx_;
  __iov_cnt_ = 1;
  __bytes_to_send_ = __write_idx_;
  return true;
}

/* 有线程池中的工作线程调用，是处理 HTTP 请求的入口函数 */
void HttpConn::Process() {
  HttpCode_ read_ret = __ProcessRead();
  if (read_ret == NO_REQUEST) {
    /* 还没收到完整请求，继续监听 */
    ModFd(epollfd_, __sockfd_, EPOLLIN, __triger_mode_);
    return;
  }
  bool write_ret = __ProcessWrite(read_ret);
  if (!write_ret) {
    /* 出错关闭连接 */
    CloseConn();
  }
  /* 监听是否可写 */
  ModFd(epollfd_, __sockfd_, EPOLLOUT, __triger_mode_);
}

void HttpConn::InitSqlResult() {
  /* 数据库连接资源获取 */
  MYSQL *mysql = nullptr;
  ConnectionRaii conn(mysql);

  /* 获取数据库中的用户名密码 */
  if (mysql_query(mysql, "SELECT username, passwd FROM user")) {
    PRINT_ERRMSG(mysql_query, mysql_error(mysql));
  }

  /* 获取检索结果 */
  MYSQL_RES *result = mysql_store_result(mysql);

  /* 讲用户名密码全部缓存进 map 中 */
  while (MYSQL_ROW row = mysql_fetch_row(result)) {
    users[row[0]] = row[1];
  }
}