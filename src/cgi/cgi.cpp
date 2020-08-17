#include "common.h"
#include "processpool.h"
#include "python_cgi.h"

int Cgi::__epollfd_ = -1;

int main(int argc, char** argv) {
  if (argc <= 2) {
    printf("Usage: %s ip_address port_number\n", basename(argv[0]));
    return 1;
  }

  const char* ip = argv[1];
  int port = atoi(argv[2]);

  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenfd < 0) {
    LOGERR("socket error");
    exit(-1);
  }
  struct sockaddr_in addr;
  bzero(&addr, sizeof(addr));
  inet_pton(AF_INET, ip, &addr.sin_addr);
  addr.sin_port = htons(port);
  addr.sin_family = AF_INET;

  if (bind(listenfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
    LOGERR("bind error");
    exit(-1);
  }

  if (listen(listenfd, 5) < 0) {
    LOGERR("listen error");
    exit(-1);
  }

  Processpool<PythonCgi>* pool = Processpool<PythonCgi>::Create(listenfd);
  if (pool) {
    pool->Run();
  }
  if (close(listenfd) < 0) {
    LOGERR("close error");
    exit(-1);
  }

  return 0;
}
