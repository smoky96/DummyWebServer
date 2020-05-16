#ifndef __THREADPOOL__H__
#define __THREADPOOL__H__

#include <atomic>
#include <cassert>
#include <list>
#include <vector>

#include "conmmon.h"
#include "locker.h"

using std::list;
using std::vector;

/** 线程池类模板
 * T: 处理逻辑任务的类
 */
template <typename T>
class threadpool {
 private:
  size_t __thread_number;             // 线程池中线程的数量
  size_t __max_requests;              // 请求队列中允许的最大请求数
  vector<pthread_t> __threads;        // 记录线程 id
  list<T*> __jobs;                    // 任务队列
  locker __jobs_locker;               // 对 jobs 的互斥锁
  sem __jobs_stat;                    // 是否有任务
  volatile std::atomic<bool> __stop;  // 是否停止线程

  /* 工作线程运行函数 */
  static void* __worker(void* arg);

  void __run();

 public:
  threadpool(int thread_number = 8, int max_request = 1000);
  ~threadpool();

  /* 往请求队列中添加任务 */
  bool append(T* request);
};

template <typename T>
threadpool<T>::threadpool(int thread_number, int max_request)
    : __threads(thread_number) {
  __thread_number = thread_number;
  __max_requests = max_request;
  __stop = false;

  assert((thread_number > 0) && (max_request > 0));

  for (int i = 0; i < thread_number; ++i) {
    printf("create thread no.%d\n", i);
    /* worker 只能为静态函数，而静态函数需要用到类中成员，所以传递 this 指针 */
    Pthread_create(&__threads[i], NULL, __worker, this);
    Pthread_detach(__threads[i]);
  }
}

template <typename T>
threadpool<T>::~threadpool() {
  __stop = true;
}

template <typename T>
bool threadpool<T>::append(T* request) {
  if (__jobs.size() > __max_requests) {
    return false;
  }
  __jobs_locker.lock();
  __jobs.push_back(request);
  __jobs_locker.unlock();
  __jobs_stat.post();
  return true;
}

template <typename T>
void* threadpool<T>::__worker(void* arg) {
  threadpool* pool = (threadpool*)arg;
  pool->__run();
  return pool;
}

template <typename T>
void threadpool<T>::__run() {
  while (!__stop) {
    __jobs_stat.wait();
    if (!__jobs.empty()) {
      T* request;
      __jobs_locker.lock();
      if (!__jobs.empty()) {
        request = __jobs.front();
        __jobs.pop_front();
      }
      __jobs_locker.unlock();
      request->process();
    }
  }
}

#endif  //!__THREADPOOL__H__