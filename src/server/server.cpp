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

void send_error(int connfd, const char* info) {
  printf("%s", info);
  Send(connfd, info, strlen(info), 0);
  Close(connfd);
}

int main(int argc, char** argv) {
  char root[] = "/home/dong/blog/";
  Config config(root);

  DummyServer server(config);

  server.Start();

  return 0;
}
