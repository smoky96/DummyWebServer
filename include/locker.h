#ifndef __LOCKER__H__
#define __LOCKER__H__

#include <pthread.h>
#include <semaphore.h>

#include <exception>

#include "common.h"

/* 封装信号类 */
class Sem {
 private:
  sem_t __sem_;

 public:
  Sem(int pshared = 0, unsigned int value = 0) {
    if (sem_init(&__sem_, pshared, value) != 0) {
      LOGERR("sem_init error");
      throw std::exception();
    }
  }

  ~Sem() { sem_destroy(&__sem_); }

  bool Wait() { return sem_wait(&__sem_) == 0; }

  bool Post() { return sem_post(&__sem_) == 0; }
};

class Locker {
 private:
  pthread_mutex_t __mutex_;

 public:
  Locker() {
    if (pthread_mutex_init(&__mutex_, NULL) != 0) {
      LOGERR("pthread_mutex_init error");
      throw std::exception();
    }
  }

  ~Locker() { pthread_mutex_destroy(&__mutex_); }

  bool Lock() { return pthread_mutex_lock(&__mutex_) == 0; }

  bool Unlock() { return pthread_mutex_unlock(&__mutex_) == 0; }
};

class Cond {
 private:
  pthread_mutex_t __mutex_;
  pthread_cond_t __cond_;

 public:
  Cond() {
    if (pthread_mutex_init(&__mutex_, NULL) != 0) {
      LOGERR("pthread_mutex_init error");
      throw std::exception();
    }
    if (pthread_cond_init(&__cond_, NULL) != 0) {
      /* 出现异常记得释放 mutex 资源  */
      pthread_mutex_destroy(&__mutex_);
      throw std::exception();
    }
  }

  ~Cond() {
    pthread_mutex_destroy(&__mutex_);
    pthread_cond_destroy(&__cond_);
  }

  bool Wait() {
    int ret = 0;
    pthread_mutex_lock(&__mutex_);
    ret = pthread_cond_wait(&__cond_, &__mutex_);
    pthread_mutex_unlock(&__mutex_);
    return ret == 0;
  }

  bool Signal() { return pthread_cond_signal(&__cond_) == 0; }
};

#endif  //!__LOCKER__H__