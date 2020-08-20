#ifndef __THREADPOOL__H__
#define __THREADPOOL__H__

#include <atomic>
#include <cassert>
#include <list>
#include <vector>

#include "common.h"
#include "locker.h"

using std::list;
using std::vector;

/** 线程池类模板
 * T: 处理逻辑任务的类
 */
template <typename T>
class Threadpool {
 private:
  size_t __thread_number_;       // 线程池中线程的数量
  size_t __max_requests_;        // 请求队列中允许的最大请求数
  vector<pthread_t> __threads_;  // 记录线程 id
  list<T*> __jobs_;              // 任务队列
  Locker __jobs_locker_;         // 对 jobs 的互斥锁
  Sem __jobs_stat_;              // 是否有任务
  volatile std::atomic<bool> __stop_;  // 是否停止线程

  /* 工作线程运行函数 */
  static void* __Worker(void* arg);

  void __Run();

 public:
  Threadpool(int thread_number = 8, int max_request = 1000);
  ~Threadpool();

  /* 往请求队列中添加任务 */
  bool Append(T* request);
};

template <typename T>
Threadpool<T>::Threadpool(int thread_number, int max_request)
    : __threads_(thread_number) {
  __thread_number_ = thread_number;
  __max_requests_ = max_request;
  __stop_ = false;

  assert((thread_number > 0) && (max_request > 0));

  for (int i = 0; i < thread_number; ++i) {
    LOGINFO("create thread no.%d", i);
    /* worker 只能为静态函数，而静态函数需要用到类中成员，所以传递 this 指针 */
    if (pthread_create(&__threads_[i], NULL, __Worker, this) < 0) {
      LOGERR("pthread_create error");
      exit(-1);
    }
    if (pthread_detach(__threads_[i]) < 0) {
      LOGERR("pthread_detach error");
      exit(-1);
    }
  }
}

template <typename T>
Threadpool<T>::~Threadpool() {
  __stop_ = true;
}

template <typename T>
bool Threadpool<T>::Append(T* request) {
  if (__jobs_.size() > __max_requests_) {
    return false;
  }
  __jobs_locker_.Lock();
  __jobs_.push_back(request);
  __jobs_locker_.Unlock();
  __jobs_stat_.Post();
  return true;
}

template <typename T>
void* Threadpool<T>::__Worker(void* arg) {
  Threadpool* pool = (Threadpool*)arg;
  pool->__Run();
  return pool;
}

template <typename T>
void Threadpool<T>::__Run() {
  while (!__stop_) {
    __jobs_stat_.Wait();
    __jobs_locker_.Lock();
    T* request = nullptr;
    if (!__jobs_.empty()) {
      request = __jobs_.front();
      __jobs_.pop_front();
    }
    __jobs_locker_.Unlock();
    if (request) request->Process();
  }
}

#endif  //!__THREADPOOL__H__