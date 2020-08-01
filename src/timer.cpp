#include "timer.h"

Timer::Timer(int delay) : expire_(time(NULL) + delay) {}

TimerHeap::TimerHeap(size_t capacity) { __heap_.reserve(capacity); }

TimerHeap::TimerHeap(const std::vector<TimerPtr>& heap) : __heap_(heap) {}

void TimerHeap::AddTimer(const TimerPtr& timer) {
  __heap_.push_back(timer);
  std::push_heap(__heap_.begin(), __heap_.end(), __compare);
}

void TimerHeap::DelTimer(const TimerPtr& timer) {
  if (timer == nullptr) return;
  // lazy delete
  timer->cb_func_ = nullptr;
}

const TimerHeap::TimerPtr TimerHeap::Top() const {
  if (__heap_.empty()) return nullptr;
  return __heap_[0];
}

void TimerHeap::PopTimer() {
  if (__heap_.empty()) return;
  std::pop_heap(__heap_.begin(), __heap_.end(), __compare);
  __heap_.pop_back();
}

void TimerHeap::Tick() {
  auto it = __heap_.begin();
  time_t cur = time(NULL);
  while (!__heap_.empty()) {
    if (it == __heap_.end() || (*it)->expire_ > cur) break;

    if ((*it)->cb_func_ != nullptr) {
      (*it)->cb_func_((*it)->user_data_);
    }
    PopTimer();
    it = __heap_.begin();
  }
}

bool TimerHeap::__compare(const TimerPtr& lhs, const TimerPtr& rhs) {
  return lhs->expire_ > rhs->expire_;
}
