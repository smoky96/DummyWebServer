#ifndef __TIMER__H__
#define __TIMER__H__

#include <netinet/in.h>
#include <time.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

class Timer;

struct TimerClientData {
  sockaddr_in addr;
  int epollfd;
  int sockfd;
  std::string str_data;
  std::shared_ptr<Timer> timer;
};

class Timer {
 public:
  time_t expire_;
  void (*cb_func_)(TimerClientData*);
  TimerClientData* user_data_;

  Timer(int delay);
};

class TimerHeap {
 public:
  typedef std::shared_ptr<Timer> TimerPtr;

  TimerHeap(size_t capacity);
  TimerHeap(const std::vector<TimerPtr>& heap);

  void AddTimer(const TimerPtr& timer);
  void DelTimer(const TimerPtr& timer);
  const TimerPtr Top() const;
  void PopTimer();
  void Tick();

  size_t size() { return __heap_.size(); }
  size_t capacity() { return __heap_.capacity(); }

 private:
  std::vector<TimerPtr> __heap_;

  static bool __compare(const TimerPtr& lhs, const TimerPtr& rhs);
};

#endif  //!__TIMER__H__