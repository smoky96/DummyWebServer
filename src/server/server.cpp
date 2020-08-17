#include <memory>
#include <vector>

#include "common.h"
#include "dummy_server.h"
#include "http_conn.h"
#include "locker.h"
#include "threadpool.h"

#define MAX_FD 65535
#define MAX_EVENT_NUM 10000

using std::unique_ptr;
using std::vector;

int main(int argc, char** argv) {
  Config config(argc, argv);

  // 初始化 logger
  Logger::Init(kInfo, config.log_path_, config.verbose_);

  DummyServer server(config);

  server.Start();

  return 0;
}
