#include "logger.h"

const vector<string> Logger::__level_str_{"info", "debug", "warning", "error"};

Logger::Logger() { __fd_ = -1; }

Logger::~Logger() {
  if (close(__fd_)) {
    perror("file close error");
    exit(-1);
  }
}

Logger* Logger::GetLogger() {
  static Logger logger;
  return &logger;
}

void Logger::__InitImp(LogLevel loglev, string& file_path, bool verbose) {
  __loglev_ = loglev;
  __verbose_ = verbose;

  time_t now = time(0);
  tm tm_res;
  memset(&tm_res, 0, sizeof(tm_res));
  localtime_r(&now, &tm_res);

  std::ostringstream fp;
  fp << file_path << "/" << 1900 + tm_res.tm_year << std::right
     << std::setfill('0') << "-" << std::setw(2) << 0 + tm_res.tm_mon << "-"
     << std::setw(2) << tm_res.tm_mday << "_" << std::setw(2) << tm_res.tm_hour
     << "-" << std::setw(2) << tm_res.tm_min << "-" << std::setw(2)
     << tm_res.tm_sec;

  __fd_ = open(fp.str().c_str(), O_CREAT | O_APPEND | O_RDWR,
               S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
  if (__fd_ < 0) {
    perror("file open error");
    exit(-1);
  }
}

void Logger::__ThreadHandler(union sigval val) {
  struct AioBuf* aiobuf = (AioBuf*)val.sival_ptr;
  int err = 0;
  while ((err = aio_error(&(aiobuf->aiocb))) == EINPROGRESS) {
    sleep(1);
  }

  if (err != 0) {
    perror("aio error");
    exit(-1);
  }

  if (aio_return(&aiobuf->aiocb) < 0) {
    perror("aio return error");
    exit(-1);
  }
  free(aiobuf);
}

AioBuf* Logger::__PrepareAiobuf(char* data) {
  AioBuf* aiobuf = (AioBuf*)malloc(sizeof(AioBuf));
  memset(aiobuf->data, '\0', kBufSize);
  memset(&aiobuf->aiocb, 0, sizeof(struct aiocb));
  aiobuf->aiocb.aio_fildes = __fd_;
  strcpy(aiobuf->data, data);
  aiobuf->aiocb.aio_buf = aiobuf->data;
  aiobuf->aiocb.aio_nbytes = strlen(data);
  aiobuf->aiocb.aio_offset = 0;
  aiobuf->aiocb.aio_sigevent.sigev_notify = SIGEV_THREAD;
  aiobuf->aiocb.aio_sigevent.sigev_notify_function = __ThreadHandler;
  aiobuf->aiocb.aio_sigevent.sigev_value.sival_ptr = aiobuf;

  return aiobuf;
}

void Logger::__WriteLog(LogLevel loglev, const char* file, int line,
                        const char* func, const char* msg) {
  char logmsg[kBufSize];
  memset(logmsg, '\0', kBufSize);

  time_t now = time(0);
  tm tm_res;
  memset(&tm_res, 0, sizeof(tm_res));
  localtime_r(&now, &tm_res);

  if (loglev == kError) {
    sprintf(
        logmsg, "[%d-%02d-%02d %02d:%02d:%02d][%s] %s - line: %d - %s: %s (%s)",
        1900 + tm_res.tm_year, 1 + tm_res.tm_mon, tm_res.tm_mday,
        tm_res.tm_hour, tm_res.tm_min, tm_res.tm_sec,
        __level_str_[loglev].c_str(), file, line, func, msg, strerror(errno));
  } else {
    sprintf(logmsg, "[%d-%02d-%02d %02d:%02d:%02d][%s] %s - line: %d - %s: %s",
            1900 + tm_res.tm_year, 1 + tm_res.tm_mon, tm_res.tm_mday,
            tm_res.tm_hour, tm_res.tm_min, tm_res.tm_sec,
            __level_str_[loglev].c_str(), file, line, func, msg);
  }

  if (__verbose_) {
    write(STDOUT_FILENO, logmsg, strlen(logmsg));
  }

  if (__loglev_ <= loglev) {
    AioBuf* aiobuf = __PrepareAiobuf(logmsg);
    if (aio_write(&(aiobuf->aiocb)) < 0) {
      perror("aio write error");
      exit(-1);
    }
  }
}

void Logger::Init(LogLevel loglev, string file_path, bool verbose) {
  GetLogger()->__InitImp(loglev, file_path, verbose);
}

void Logger::Info(const char* file, int line, const char* func, const char* msg,
                  ...) {
  va_list args;
  va_start(args, msg);
  char logmsg[kBufSize];
  vsnprintf(logmsg, kBufSize - 1, msg, args);
  va_end(args);

  GetLogger()->__WriteLog(kInfo, file, line, func, logmsg);
}

void Logger::Debug(const char* file, int line, const char* func,
                   const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  char logmsg[kBufSize];
  vsnprintf(logmsg, kBufSize - 1, msg, args);
  va_end(args);

  GetLogger()->__WriteLog(kDebug, file, line, func, logmsg);
}

void Logger::Warning(const char* file, int line, const char* func,
                     const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  char logmsg[kBufSize];
  vsnprintf(logmsg, kBufSize - 1, msg, args);
  va_end(args);

  GetLogger()->__WriteLog(kWarning, file, line, func, logmsg);
}

void Logger::Error(const char* file, int line, const char* func,
                   const char* msg, ...) {
  va_list args;
  va_start(args, msg);
  char logmsg[kBufSize];
  vsnprintf(logmsg, kBufSize - 1, msg, args);
  va_end(args);

  GetLogger()->__WriteLog(kError, file, line, func, logmsg);
}
