#ifndef __SQL_CONNPOOL__H__
#define __SQL_CONNPOOL__H__

#include <mysql/mysql.h>

#include <list>
#include <string>

#include "common.h"
#include "locker.h"

using std::list;
using std::string;

class SqlConnpool {
 public:
  static SqlConnpool* GetInstance();

  /* 取值函数 */
  int max_conn();   // 返回最大连接数
  int cur_conn();   // 返回已使用连接数
  int free_conn();  // 返回空闲连接数

 public:                                       // 静态方法
  static MYSQL* GetConnection();               // 获取数据库连接
  static bool ReleaseConnection(MYSQL* conn);  // 释放连接
  static void DestroyPool();                   // 销毁连接池
  static void Init(const string& url, const string& user, const string& passwd,
                   const string& db_name, int port, int max_conn);

 private:
  /* 单例模式，禁用构造函数 */
  SqlConnpool();
  SqlConnpool(const SqlConnpool&);
  SqlConnpool& operator=(const SqlConnpool&);
  ~SqlConnpool();

  MYSQL* __GetConnectionImp();
  bool __ReleaseConnectionImp(MYSQL* conn);
  void __DestroyPoolImp();
  void __InitImp(const string& url, const string& user, const string& passwd,
                 const string& db_name, int port, int max_conn);

  int __max_conn_;            // 最大连接数
  int __cur_conn_;            // 已使用连接数
  int __free_conn_;           // 空闲连接数
  Locker __lock_;             // 锁
  list<MYSQL*> __conn_list_;  // 连接池
  Sem __reserve_;             // 信号量

 public:
  string url_;      // 主机地址
  string port_;     // 端口号
  string user_;     // 用户名
  string passwd_;   // 密码
  string db_name_;  // 数据库名称
};

class ConnectionRaii {
 public:
  ConnectionRaii(MYSQL*& conn);
  ~ConnectionRaii();

 private:
  MYSQL* conn_raii;
};

#endif  //!__SQL_CONNPOOL__H__