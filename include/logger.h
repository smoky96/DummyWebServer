#ifndef __LOGGER__H__
#define __LOGGER__H__

#include <aio.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

using std::string;
using std::vector;

static const int kBufSize = 4096;

enum LogLevel { kInfo = 0, kDebug, kWarning, kError };

struct AioBuf {
  struct aiocb aiocb;
  char data[kBufSize];
};

class Logger {
 public:
  ~Logger();

  static Logger* GetLogger();
  static void Init(LogLevel loglev, string file_path, bool verbose);

  static void Info(const char* file, int line, const char* func,
                   const char* msg, ...);
  static void Debug(const char* file, int line, const char* func,
                    const char* msg, ...);
  static void Warning(const char* file, int line, const char* func,
                      const char* msg, ...);
  static void Error(const char* file, int line, const char* func,
                    const char* msg, ...);

 private:
  void __InitImp(LogLevel loglev, string& file_path, bool verbose);

  void __WriteLog(LogLevel loglev, const char* file, int line, const char* func,
                  const char* msg);

  AioBuf* __PrepareAiobuf(char* data);

  static void __ThreadHandler(union sigval val);

 private:
  Logger();
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger& rhs) = delete;

  static const vector<string> __level_str_;

  LogLevel __loglev_;  // 记录日志的级别
  bool __verbose_;     // 是否输出的标准输出（这里是同步 IO）
  int __fd_;           // 写入文件的描述符
};

#define LOGINFO(FMT, ...)                                               \
  do {                                                                  \
    Logger::Info(__FILE__, __LINE__, __FUNCTION__, FMT, ##__VA_ARGS__); \
  } while (0)

#define LOGDEBUG(FMT, ...)                                               \
  do {                                                                   \
    Logger::Debug(__FILE__, __LINE__, __FUNCTION__, FMT, ##__VA_ARGS__); \
  } while (0)

#define LOGWARN(FMT, ...)                                                  \
  do {                                                                     \
    Logger::Warning(__FILE__, __LINE__, __FUNCTION__, FMT, ##__VA_ARGS__); \
  } while (0)

#define LOGERR(FMT, ...)                                                 \
  do {                                                                   \
    Logger::Error(__FILE__, __LINE__, __FUNCTION__, FMT, ##__VA_ARGS__); \
  } while (0)

#endif  //!__LOGGER__H__