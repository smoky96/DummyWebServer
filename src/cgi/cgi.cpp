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

  int listenfd = Socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  bzero(&addr, sizeof(addr));
  inet_pton(AF_INET, ip, &addr.sin_addr);
  addr.sin_port = htons(port);
  addr.sin_family = AF_INET;

  Bind(listenfd, &addr, sizeof(addr));

  Listen(listenfd, 5);

  Processpool<PythonCgi>* pool = Processpool<PythonCgi>::Create(listenfd);
  if (pool) {
    pool->Run();
  }
  Close(listenfd);

  return 0;
}
