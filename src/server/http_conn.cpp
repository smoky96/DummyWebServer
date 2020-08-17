#include "http_conn.h"

#include "urlcode.h"

/* 定义 HTTP 响应的状态信息 */
const char *ok_200_title = "OK";

const char *ok_206_title = "Partial Content";

const char *error_400_title = "Bad Request";

const char *error_400_form =
    "Your request has bad syntax or is inherently impossible to satisfy.\n";

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
const char *default_page = "index.html";

int HttpConn::user_cnt_ = 0;
int HttpConn::epollfd_ = -1;

static map<string, string> users;  // 所用用户名和密码
static Locker locker;              // 用户名数据加锁

map<string, File> HttpConn::__resources_;

void HttpConn::CloseConn(bool real_close) {
  if (real_close && (__sockfd_ != -1)) {
    if (RemoveFd(epollfd_, __sockfd_) < 0) LOGWARN("RemoveFd error");
    g_timer_heap.DelTimer(g_timer_client_data[__sockfd_].timer);  // 删除定时器
    __sockfd_ = -1;
    --user_cnt_;
  }
}

void HttpConn::Init(int sockfd, const sockaddr_in &addr,
                    TriggerMode trigger_mode) {
  if (AddFd(epollfd_, sockfd, true, trigger_mode) < 0) {
    LOGWARN("AddFd error");
    if (close(sockfd) < 0) LOGERR("close error");
    return;
  }
  __sockfd_ = sockfd;
  __addr_ = addr;
  __trigger_mode_ = trigger_mode;
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
  __request_file_ = NULL;
  __range_start_ = 0;
  __range_end_ = -1;
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
  if (__trigger_mode_ == ET) {
    while (1) {
      bytes_read = recv(__sockfd_, __read_buf + __read_idx_,
                        kReadBufSize_ - __read_idx_, 0);
      if (bytes_read == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          /* 非阻塞 */
          break;
        }
        LOGWARN("recv error");
        if (SendError(__sockfd_, "Internal read error") < 0)
          LOGWARN("SendError error");
        return false;
      } else if (bytes_read == 0) {
        /* 对方关闭连接 */
        return false;
      } else {
        __read_idx_ += bytes_read;
      }
    }
  } else {
    bytes_read = recv(__sockfd_, __read_buf + __read_idx_,
                      kReadBufSize_ - __read_idx_, 0);
    if (bytes_read == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        /* 非阻塞 */
        return true;
      }
      LOGERR("recv error");
      if (SendError(__sockfd_, "Internal read error") < 0)
        LOGWARN("SendError error");
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
  } else if (strncasecmp(text, "Range:", 6) == 0) {
    /* 处理 Range 字段 */
    text += 6;
    text += strspn(text, " \t");
    text += strspn(text, "bytes=");
    if (*text == '-') {
      ++text;
      __range_start_ = -1;
      __range_end_ = atol(text);
    } else {
      __range_start_ = atol(text);
      text += strcspn(text, "-") + 1;
      if (*text == '\0') {
        __range_end_ = -1;
      } else {
        __range_end_ = atol(text);
      }
    }
  } else {
    LOGINFO("unknown header: %s\n", text);
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
    LOGINFO("got 1 http line: %s\n", text);
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
          return __DoRequest(text);
        }
        break;
      case CHECK_STATE_CONTENT:
        ret = __ParseContent(text);
        if (ret == GET_REQUEST) {
          return __DoRequest(text);
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
HttpConn::HttpCode_ HttpConn::__DoRequest(char *text) {
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
    char *basename = strrchr(__real_file_, '/');
    ++basename;
    if (strcmp(basename, "sqllogin") == 0) {
      __Login(basename);
    } else if (strcmp(basename, "sqlregister") == 0) {
      __Regist(basename);
    } else if (strcmp(basename, "login") == 0) {  // 进入登录页面
      strcpy(basename, "login.html");
    } else if (strcmp(basename, "register") == 0) {  // 进入注册页面
      strcpy(basename, "register.html");
    } else if (strcmp(basename, "run") == 0) {
      return __RunPython(text);
    }
  }

  if (strcmp(__real_file_, "root/") == 0) {
    /* 返回 default_page */
    strcat(__real_file_, default_page);
  }
  if (__resources_.count(__real_file_) == 0) {
    return NO_RESOURCE;
  }
  __request_file_ = &__resources_[__real_file_];
  if (!(__request_file_->file_stat_.st_mode & S_IROTH)) {
    return FORBIDDEN_REQUEST;
  }
  if (__request_file_->addr_ == NULL) {
    return BAD_REQUEST;
  }
  if (__range_end_ == -1) {
    __range_end_ = __request_file_->file_stat_.st_size - 1;
  } else if (__range_start_ == -1) {
    __range_start_ = __request_file_->file_stat_.st_size - __range_end_;
    __range_end_ = __request_file_->file_stat_.st_size - 1;
  }
  __range_end_ = __range_end_ >= __request_file_->file_stat_.st_size
                     ? __request_file_->file_stat_.st_size - 1
                     : __range_end_;
  if (__range_end_ - __range_start_ > 10485759) {  // 超过 10MB 分块传输
    __range_end_ = __range_start_ + 10485759;
  }

  return FILE_REQUEST;
}

bool HttpConn::__Login(char *basename) {
  /* 提取 POST 参数 */
  char username[51];
  char password[31];
  if (!__GetUserPasswd(username, password)) return false;

  if (users.find(username) != users.end() && users[username] == password) {
    strcpy(basename, "welcome.html");
    return true;
  }
  strcpy(basename, "login_error.html");
  return false;
}

bool HttpConn::__Regist(char *basename) {
  /* 提取 POST 参数 */
  char username[51];
  char password[31];
  if (!__GetUserPasswd(username, password)) return false;

  char sql_statement[300];
  sprintf(sql_statement,
          "INSERT INTO user(username, passwd) VALUES('%s', '%s')", username,
          password);
  if (users.count(username)) {
    strcpy(basename, "register_error.html");
    return false;
  }
  MYSQL *mysql;
  ConnectionRaii conn(mysql);
  locker.Lock();
  int result = mysql_query(mysql, sql_statement);
  users.insert(std::make_pair(username, password));
  locker.Unlock();

  if (!result) {
    strcpy(basename, "login.html");
    return true;
  }
  strcpy(basename, "register_error.html");
  return false;
}

bool HttpConn::__GetUserPasswd(char *username, char *passwd) {
  char *tmp = strpbrk(&__read_buf[__cur_idx_], "=");
  if (tmp == nullptr) {
    username[0] = '\0';
    return false;
  }
  int i = 0;
  for (; *(++tmp) != '&'; ++i) {
    username[i] = *tmp;
  }
  username[i] = '\0';
  tmp = strpbrk(tmp, "=");
  if (tmp == nullptr) {
    passwd[0] = '\0';
    return false;
  }
  for (i = 0; *(++tmp) != '\0'; ++i) {
    passwd[i] = *tmp;
  }
  passwd[i] = '\0';
  return true;
}

HttpConn::HttpCode_ HttpConn::__RunPython(char *text) {
  int cgisockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (cgisockfd < 0) {
    LOGERR("socket error");
    exit(-1);
  }

  struct sockaddr_in addr;
  bzero(&addr, sizeof(addr));

  addr.sin_port = htons(10801);
  addr.sin_family = AF_INET;
  struct hostent *he = gethostbyname("127.0.0.1");

  memcpy(&(addr.sin_addr), he->h_addr_list[0], sizeof(in_addr));
  if (connect(cgisockfd, (sockaddr *)&addr, sizeof(addr)) < 0) {
    LOGWARN("connect error");
    return INTERNAL_ERROR;
  }
  text = text + 7;
  char len[32];
  memset(len, '\0', sizeof(len));
  sprintf(len, "%d\r\n", __content_length_ - 7);
  char msg[kReadBufSize_];
  memset(msg, '\0', sizeof(msg));
  strcpy(msg, len);
  strcat(msg, text);
  int ret = -1;
  ret = send(cgisockfd, msg, __content_length_, 0);
  if (ret < 0) {
    LOGWARN("send error");
    return INTERNAL_ERROR;
  }

  int content_length = 0;
  ret = recv(cgisockfd, __cgiret_buf_ + content_length, kWriteBufSize, 0);
  if (ret < 0) {
    LOGWARN("recv error");
    return INTERNAL_ERROR;
  }
  content_length += ret;
  __cgiret_buf_[content_length] = '\0';
  if (close(cgisockfd) < 0) {
    LOGERR("close error");
    exit(-1);
  }
  return CGI_REQUEST;
}

/* 释放缓存的资源 */
void HttpConn::ReleaseStaticResource() {
  for (auto it = __resources_.begin(); it != __resources_.end(); ++it) {
    if (it->second.addr_) {
      if (munmap((void *)it->second.addr_, it->second.file_stat_.st_size) < 0) {
        LOGERR("munmap error");
        exit(-1);
      }
      it->second.addr_ = NULL;
    }
  }
}

/* 每次调用 writev 都需要调整 __iov_ */
void HttpConn::__AdjustIov() {
  if (__bytes_have_sent_ > __write_idx_) {
    __iov_[0].iov_len = 0;
    __iov_[1].iov_base = __request_file_->addr_ + __range_start_ +
                         (__bytes_have_sent_ - __write_idx_);
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
    if (ModFd(epollfd_, __sockfd_, EPOLLIN, __trigger_mode_) < 0) {
      LOGWARN("ModFd error");
      return false;
    }
    __Init();
    return true;
  }
  if (__trigger_mode_ == ET) {
    while (1) {
      tmp = writev(__sockfd_, __iov_, __iov_cnt_);
      if (tmp < 0) {
        if (errno == EAGAIN) {
          /* 若写缓冲区没有空间，则等待缓冲区可写，在此期间无法接收客户端请求 */
          if (ModFd(epollfd_, __sockfd_, EPOLLOUT, __trigger_mode_) < 0) {
            LOGWARN("ModFd error");
            return false;
          }
          return true;
        }
        LOGERR("writev error");
        if (SendError(__sockfd_, "Internal write error") < 0)
          LOGWARN("SendError error");
        return false;
      }
      __bytes_to_send_ -= tmp;
      __bytes_have_sent_ += tmp;
      __AdjustIov();

      if (__bytes_to_send_ <= 0) {
        /* HTTP 响应发送成功，根据 Connection 字段决定是否立即关闭连接 */
        if (__linger_) {
          __Init();
          if (ModFd(epollfd_, __sockfd_, EPOLLIN, __trigger_mode_) < 0) {
            LOGWARN("ModFd error");
            return false;
          }
          return true;
        } else {
          if (ModFd(epollfd_, __sockfd_, EPOLLIN, __trigger_mode_) < 0)
            LOGWARN("ModFd error");
          return false;
        }
      }
    }
  } else {
    tmp = writev(__sockfd_, __iov_, __iov_cnt_);
    if (tmp < 0) {
      if (errno == EAGAIN) {
        /* 若写缓冲区没有空间，则等待缓冲区可写，在此期间无法接收客户端请求 */
        if (ModFd(epollfd_, __sockfd_, EPOLLOUT, __trigger_mode_) < 0) {
          LOGWARN("ModFd error");
          return false;
        }
        return true;
      }
      LOGERR("writev error");
      if (SendError(__sockfd_, "Internal write error") < 0)
        LOGWARN("SendError error");
      return false;
    }
    __bytes_to_send_ -= tmp;
    __bytes_have_sent_ += tmp;
    __AdjustIov();

    if (__bytes_to_send_ <= 0) {
      /* HTTP 响应发送成功，根据 Connection 字段决定是否立即关闭连接 */
      if (__linger_) {
        __Init();
        if (ModFd(epollfd_, __sockfd_, EPOLLIN, __trigger_mode_) < 0) {
          LOGWARN("ModFd error");
          return false;
        }
        return true;
      } else {
        if (ModFd(epollfd_, __sockfd_, EPOLLIN, __trigger_mode_) < 0)
          LOGWARN("ModFd error");
        return false;
      }
    } else {
      if (ModFd(epollfd_, __sockfd_, EPOLLIN, __trigger_mode_) < 0) {
        LOGWARN("ModFd error");
        return false;
      }
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
  return __AddResponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool HttpConn::__AddHeaders(int content_len) {
  if (!__AddContentLength(content_len)) return false;
  if (!__AddAcceptRanges()) return false;
  if (!__AddContentRange()) return false;
  if (!__AddLinger()) return false;
  if (!__AddBlankLine()) return false;
  return true;
}

bool HttpConn::__AddContentLength(int content_len) {
  return __AddResponse("Content-Length: %d\r\n", content_len);
}

bool HttpConn::__AddAcceptRanges() {
  return __AddResponse("Accept-Range: bytes\r\n");
}

bool HttpConn::__AddContentRange() {
  if (__request_file_ != NULL)
    return __AddResponse("Content-Range: bytes %d-%d/%d\r\n", __range_start_,
                         __range_end_, __request_file_->file_stat_.st_size);
  return true;
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
    case FILE_REQUEST: {
      int send_file_size = __range_end_ - __range_start_ + 1;
      if (send_file_size < __request_file_->file_stat_.st_size) {
        __AddStatusLine(206, ok_206_title);
      } else {
        __AddStatusLine(200, ok_200_title);
      }
      if (send_file_size != 0) {
        __AddHeaders(send_file_size);
        __iov_[0].iov_base = __write_buf_;
        __iov_[0].iov_len = __write_idx_;
        __iov_[1].iov_base = __request_file_->addr_ + __range_start_;
        __iov_[1].iov_len = send_file_size;
        __bytes_to_send_ = __iov_[0].iov_len + __iov_[1].iov_len;
        __iov_cnt_ = 2;
        return true;
      } else {
        const char *ok_string = "<html><body></body></html>";
        __AddHeaders(strlen(ok_string));
        if (!__AddContent(ok_string)) return false;
      }
    } break;
    case CGI_REQUEST: {
      __AddStatusLine(200, ok_200_title);
      int content_length = strlen(__cgiret_buf_);
      __AddHeaders(content_length);
      __iov_[0].iov_base = __write_buf_;
      __iov_[0].iov_len = __write_idx_;
      __iov_[1].iov_base = __cgiret_buf_;
      __iov_[1].iov_len = content_length;
      __bytes_to_send_ = __write_idx_ + content_length;
      __iov_cnt_ = 2;
      return true;
      break;
    }
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
    if (ModFd(epollfd_, __sockfd_, EPOLLIN, __trigger_mode_)) {
      LOGWARN("ModFd error");
      CloseConn();
    }
    return;
  }
  bool write_ret = __ProcessWrite(read_ret);
  if (!write_ret) {
    /* 出错关闭连接 */
    CloseConn();
    return;
  }
  /* 监听是否可写 */
  if (ModFd(epollfd_, __sockfd_, EPOLLOUT, __trigger_mode_) < 0) {
    LOGWARN("ModFd error");
    CloseConn();
  }
}

void HttpConn::InitSqlResult() {
  /* 数据库连接资源获取 */
  MYSQL *mysql = nullptr;
  ConnectionRaii conn(mysql);

  /* 获取数据库中的用户名密码 */
  if (mysql_query(mysql, "SELECT username, passwd FROM user")) {
    LOGERR("mysql_query error: %s", mysql_error(mysql));
    exit(-1);
  }

  /* 获取检索结果 */
  MYSQL_RES *result = mysql_store_result(mysql);

  /* 讲用户名密码全部缓存进 map 中 */
  while (MYSQL_ROW row = mysql_fetch_row(result)) {
    users[row[0]] = row[1];
  }
}

void HttpConn::InitStaticResource(const char *root) {
  DIR *dp;
  dirent *dirp;
  dp = opendir(root);
  if (dp == NULL) {
    LOGERR("opendir error");
    exit(-1);
  }

  while ((dirp = readdir(dp)) != NULL) {
    if (strcmp(dirp->d_name, ".") == 0 || strcmp(dirp->d_name, "..") == 0 ||
        dirp->d_type == DT_LNK) {
      continue;
    }

    string path = string(root) + dirp->d_name;
    struct stat file_stat;
    if (stat(path.c_str(), &file_stat) < 0) {
      LOGERR("stat error");
      exit(-1);
    }
    if (dirp->d_type == DT_DIR) {
      /* 将目录放入也放入 __resources_ 中，若请求的是目录，则返回 BAD_REQUEST */
      __resources_[path] = File(NULL, file_stat);
      InitStaticResource((path + "/").c_str());
    } else if (dirp->d_type == DT_REG) {
      int fd = open(path.c_str(), O_RDONLY);
      if (fd < 0) {
        LOGERR("open error");
        exit(-1);
      }
      char *addr =
          (char *)mmap(NULL, file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
      if (addr == MAP_FAILED) {
        LOGERR("mmap error");
        exit(-1);
      }
      __resources_[path] = File(addr, file_stat);
      if (close(fd) < 0) {
        LOGERR("close error");
        exit(-1);
      }
    }
  }
  if (closedir(dp) < 0) {
    LOGERR("closedir error");
    exit(-1);
  }
}