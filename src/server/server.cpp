#include <memory>
#include <vector>

#include "conmmon.h"
#include "dummy_server.h"
#include "http_conn.h"
#include "locker.h"
#include "threadpool.h"

#define MAX_FD 65535
#define MAX_EVENT_NUM 10000

using std::unique_ptr;
using std::vector;

int main(int argc, char** argv) {
  char root[] = "/home/dong/blog/";
  Config config(root, 8080, 8, ET);

  DummyServer server(config);

  server.Start();

  return 0;
}
