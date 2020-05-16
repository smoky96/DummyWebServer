#ifndef __LOCKER__H__
#define __LOCKER__H__

#include <pthread.h>
#include <semaphore.h>

#include <exception>

#include "conmmon.h"

/* 封装信号类 */
class sem {
 private:
  sem_t __sem;

 public:
  sem(int pshared = 0, unsigned int value = 0) {
    if (sem_init(&__sem, pshared, value) != 0) {
      PRINT_ERRNO(sem_init);
      throw std::exception();
    }
  }

  ~sem() { sem_destroy(&__sem); }

  bool wait() { return sem_wait(&__sem) == 0; }

  bool post() { return sem_post(&__sem) == 0; }
};

class locker {
 private:
  pthread_mutex_t __mutex;

 public:
  locker() {
    if (pthread_mutex_init(&__mutex, NULL) != 0) {
      PRINT_ERRMSG(pthread_mutex_init, "something wrong");
      throw std::exception();
    }
  }

  ~locker() { pthread_mutex_destroy(&__mutex); }

  bool lock() { return pthread_mutex_lock(&__mutex) == 0; }

  bool unlock() { return pthread_mutex_unlock(&__mutex) == 0; }
};

class cond {
 private:
  pthread_mutex_t __mutex;
  pthread_cond_t __cond;

 public:
  cond() {
    if (pthread_mutex_init(&__mutex, NULL) != 0) {
      PRINT_ERRMSG(pthread_mutex_init, "something wrong");
      throw std::exception();
    }
    if (pthread_cond_init(&__cond, NULL) != 0) {
      /* 出现异常记得释放 mutex 资源  */
      pthread_mutex_destroy(&__mutex);
      throw std::exception();
    }
  }

  ~cond() {
    pthread_mutex_destroy(&__mutex);
    pthread_cond_destroy(&__cond);
  }

  bool wait() {
    int ret = 0;
    pthread_mutex_lock(&__mutex);
    ret = pthread_cond_wait(&__cond, &__mutex);
    pthread_mutex_unlock(&__mutex);
    return ret == 0;
  }

  bool signal() { return pthread_cond_signal(&__cond) == 0; }
};

#endif  //!__LOCKER__H__