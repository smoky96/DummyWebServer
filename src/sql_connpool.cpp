#include "sql_connpool.h"

SqlConnpool::SqlConnpool() {
  __cur_conn_ = 0;
  __free_conn_ = 0;
}

SqlConnpool* SqlConnpool::GetInstance() {
  static SqlConnpool conn_pool;
  return &conn_pool;
}

/* 获取一个可用连接，更新空闲连接数和已使用连接数 */
MYSQL* SqlConnpool::__GetConnectionImp() {
  MYSQL* conn = nullptr;

  if (__conn_list_.size() == 0) return nullptr;

  __reserve_.Wait();
  __lock_.Lock();

  conn = __conn_list_.front();
  __conn_list_.pop_front();
  --__free_conn_;
  ++__cur_conn_;

  __lock_.Unlock();

  return conn;
}

/* 释放连接，更新空闲连接数和已使用连接数 */
bool SqlConnpool::__ReleaseConnectionImp(MYSQL* conn) {
  if (conn == nullptr) return false;

  __lock_.Lock();

  __conn_list_.push_back(conn);
  ++__free_conn_;
  --__cur_conn_;

  __lock_.Unlock();
  __reserve_.Post();

  return true;
}

/* 初始化连接 */
void SqlConnpool::__InitImp(const string& url, const string& user,
                            const string& passwd, const string& database_name,
                            int port, int max_conn) {
  url_ = url;
  user_ = user;
  passwd_ = passwd;
  db_name_ = database_name;

  for (int i = 0; i < max_conn; ++i) {
    MYSQL* conn = nullptr;
    conn = mysql_init(conn);

    if (conn == nullptr) {
      PRINT_ERRMSG(mysql_init, "MySQL error");
      exit(-1);
    }

    conn =
        mysql_real_connect(conn, url_.c_str(), user_.c_str(), passwd_.c_str(),
                           db_name_.c_str(), port, nullptr, 0);
    if (conn == nullptr) {
      PRINT_ERRMSG(mysql_real_connect, "MySQL error");
      exit(-1);
    }
    __conn_list_.push_back(conn);
    ++__free_conn_;
  }
  __reserve_ = Sem(0, __free_conn_);
  __max_conn_ = __free_conn_;
}

/* 销毁连接池 */
void SqlConnpool::__DestroyPoolImp() {
  __lock_.Lock();
  while (__conn_list_.size() > 0) {
    auto it = __conn_list_.begin();
    mysql_close(*it);
    --__cur_conn_;
    --__free_conn_;
    __conn_list_.erase(it);
  }
  __lock_.Unlock();
}

int SqlConnpool::free_conn() { return __free_conn_; }

SqlConnpool::~SqlConnpool() { __DestroyPoolImp(); }

MYSQL* SqlConnpool::GetConnection() {
  return GetInstance()->__GetConnectionImp();
}

bool SqlConnpool::ReleaseConnection(MYSQL* conn) {
  return GetInstance()->__ReleaseConnectionImp(conn);
}

void SqlConnpool::DestroyPool() { GetInstance()->__DestroyPoolImp(); }

void SqlConnpool::Init(const string& url, const string& user,
                       const string& passwd, const string& db_name, int port,
                       int max_conn) {
  GetInstance()->__InitImp(url, user, passwd, db_name, port, max_conn);
}

ConnectionRaii::ConnectionRaii(MYSQL*& conn) {
  conn = SqlConnpool::GetConnection();
  conn_raii = conn;
}

ConnectionRaii::~ConnectionRaii() { SqlConnpool::ReleaseConnection(conn_raii); }