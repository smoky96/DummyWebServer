#ifndef __COMPILER_CGI__H__
#define __COMPILER_CGI__H__

#include <Python.h>

#include <string>

#include "cgi.h"
#include "common.h"
#include "processpool.h"
#include "urlcode.h"

class PythonCgi : public Cgi {
 public:
  PythonCgi() { Py_Initialize(); }
  virtual ~PythonCgi() { Py_Finalize(); };

  virtual void Init(int epollfd, int sockfd, const sockaddr_in& client_addr);
  virtual void Process();

 private:
  int __content_length_;
};

#endif  //!__COMPILER_CGI__H__