/*
 *  Copyright mobinsheng. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef _LAZY_TASK_QUEUE_H_
#define _LAZY_TASK_QUEUE_H_
#include <assert.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <stdint.h>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace lazy {

static int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

static void SleepUs(uint64_t us) {
  std::this_thread::sleep_for(std::chrono::microseconds(us));
}

class QueuedTask {
public:
  QueuedTask() {}
  virtual ~QueuedTask() {}
  virtual void run() = 0;

  // call run
  void operator()() { run(); }

  // unique id
  int64_t task_id = -1;

  // is sync task
  bool is_sync = false;

  // is task execute finish
  std::atomic<bool> finished{false};

  // async task -- begin

  int64_t enqueue_time_ms = 0;

  uint32_t delay_ms = 0;

  uint64_t repeat_num = 0;
  
  uint64_t invoke_count = 0;

  // async task -- end
private:
  QueuedTask(const QueuedTask &) = delete;
};

template <class ReturnT, class Closure> class ClosureTask : public QueuedTask {
public:
  explicit ClosureTask(Closure &&closure)
      : closure_(std::forward<Closure>(closure)) {}

  virtual void run() override { result_ = closure_(); }

  ReturnT move_result() { return std::move(result_); }

private:
  Closure closure_;
  ReturnT result_;
};

template <class Closure> class ClosureTask<void, Closure> : public QueuedTask {
public:
  explicit ClosureTask(Closure &&closure)
      : closure_(std::forward<Closure>(closure)) {}

  virtual void run() override { closure_(); }

  void move_result() {}

private:
  Closure closure_;
};

template <class ReturnT, class Closure>
static std::unique_ptr<QueuedTask> NewClosure(Closure &&closure) {
  std::unique_ptr<QueuedTask> ptr(
      new ClosureTask<ReturnT, Closure>(std::forward<Closure>(closure)));
  return ptr;
}

template <class Closure>
static std::unique_ptr<QueuedTask> NewClosure(Closure &&closure) {
  std::unique_ptr<QueuedTask> ptr(
      new ClosureTask<void, Closure>(std::forward<Closure>(closure)));
  return ptr;
}

typedef std::shared_ptr<QueuedTask> SharedClosure;

template <class ReturnT, class Closure>
static SharedClosure MakeSharedClosure(Closure &&closure) {
  auto ptr = std::make_shared<ClosureTask<ReturnT, Closure>>(
      std::forward<Closure>(closure));
  return ptr;
}

template <class Closure>
static SharedClosure MakeSharedClosure(Closure &&closure) {
  auto ptr = std::make_shared<ClosureTask<void, Closure>>(
      std::forward<Closure>(closure));
  return ptr;
}

/*
** Task Queue
*/
class TaskQueue {
public:
  TaskQueue(const std::string &name) : name_(name) {}

  ~TaskQueue() { stop(); }

  void start() {
    if (thread_.joinable()) {
      return;
    }

    std::unique_lock<std::mutex> guard(mutex_);

    if (thread_.joinable()) {
      return;
    }

    stopped_ = false;

    thread_ = std::thread(std::bind(&TaskQueue::run, this));
  }

  void stop() {
    if (thread_.joinable()) {
      {
        // post_delayed_internal has lock
        post_delayed_internal([] {}, 0, 0, true);

        std::unique_lock<std::mutex> guard(mutex_);

        stopped_ = true;
      }

      cond_.notify_one();

      thread_.join();
    }
  }

  bool running() const { return !stopped_; }

  // 判断当前所在的线程是否和任务队列的线程为同一个
  bool is_current() const {
    return std::this_thread::get_id() == thread_.get_id();
  }

  const std::string &name() const { return name_; }

  // 添加异步任务，和add_task效果一样
  template <class Closure> int64_t post(Closure &&closure) {
    // 最后一个参数(repeat_num)等于0表示不是重复任务
    return post_delayed_internal(std::forward<Closure>(closure), 0, 0, false);
  }

  /* 添加带延迟的异步任务
   * closure: 可执行对象
   * delay_or_interval_ms: 延迟执行的时间
   */
  template <class Closure>
  int64_t post_delayed(Closure &&closure, uint32_t delay_or_interval_ms) {
    return post_delayed_internal(std::forward<Closure>(closure),
                                 delay_or_interval_ms, 0, false);
  }

  /* 添加带延迟的异步任务
   * closure: 可执行对象
   * delay_or_interval_ms: 延迟执行的时间
   * repeat_num:
   * 重复次数，默认是0表示不重复，等于-1表示无限重复/循环，大于0表示重复指定次数
   */
  template <class Closure>
  int64_t post_delayed_and_repeat(Closure &&closure,
                                  uint32_t delay_or_interval_ms,
                                  uint64_t repeat_num) {
    return post_delayed_internal(std::forward<Closure>(closure),
                                 delay_or_interval_ms, repeat_num, false);
  }

  // 取消一个异步任务
  // 对于周期性执行的任务（定时器），最好指定一个id，这样方便取消
  void cancel(int64_t task_id) {
    std::unique_lock<std::mutex> guard(mutex_);

    if (task_id == EXIT_TASK_ID) {
      return;
    }

    if (task_id <= SYNC_TASK_ID) {
      return;
    }

    auto it = delayed_task_map_.begin();

    while (it != delayed_task_map_.end()) {
      if (it->second->task_id != task_id) {
        ++it;
        continue;
      }

      it = delayed_task_map_.erase(it);
      break;
    }
  }

  // 添加定时器，需要明确指定一个id
  template <class Closure>
  int64_t add_timer(Closure &&closure, uint32_t interval_ms) {

    if (interval_ms == 0) {
      return INVALID_TASK_ID;
    }

    return post_delayed_internal(std::forward<Closure>(closure), interval_ms,
                                 REPEAT_FOREVER, false);
  }

  // 移除定时器
  void remove_timer(int64_t task_id) { return cancel(task_id); }

  // 执行同步任务
  template <class ReturnT, class Closure> ReturnT invoke(Closure &&closure) {

    if (stopped_) {
      throw std::runtime_error("task queue is stopped!");
    }

    auto task =
        MakeSharedClosure<ReturnT, Closure>(std::forward<Closure>(closure));
    task->finished = false;
    task->task_id = SYNC_TASK_ID;
    task->enqueue_time_ms = 0;
    task->delay_ms = 0;
    task->is_sync = true;
    task->repeat_num = 0;

    {
      std::unique_lock<std::mutex> guard(mutex_);
      task_list_.push_back(task);
    }

    cond_.notify_one();

    {
      // 等待任务执行结束
      std::unique_lock<std::mutex> guard(sync_mutex_);
      sync_cond_.wait(guard, [task] { return task->finished.load(); });
    }

    // 返回结果
    ClosureTask<ReturnT, Closure> *ptr =
        (ClosureTask<ReturnT, Closure> *)task.get();
    return ptr->move_result();
  }

private:
  // 添加异步任务的公共接口
  template <class Closure>
  int64_t post_delayed_internal(Closure &&closure,
                                uint32_t delay_or_interval_ms,
                                uint64_t repeat_num, bool is_exit_task) {
    int64_t task_id = INVALID_TASK_ID;
    if (stopped_) {
      return task_id;
    }

    if (is_exit_task) {
      task_id = EXIT_TASK_ID;
    } else {
      task_id = async_task_id_++;
      if (async_task_id_ < 0) {
        async_task_id_ = SYNC_TASK_ID + 1;
      }
    }
    auto task =
        MakeSharedClosure<void, Closure>(std::forward<Closure>(closure));
    task->finished = false;
    task->task_id = task_id;
    task->enqueue_time_ms = NowMs();
    task->delay_ms = delay_or_interval_ms;
    task->is_sync = false;
    task->repeat_num = repeat_num;
    task->invoke_count = 0;
    assert(task->repeat_num >= task->invoke_count);

    int64_t target_time_ms = task->enqueue_time_ms + task->delay_ms;

    {
      std::unique_lock<std::mutex> guard(mutex_);

      delayed_task_map_.insert(std::make_pair(target_time_ms, std::move(task)));
    }
    cond_.notify_one();

    return task_id;
  }

  // 任务队列线程函数
  void run() {
    while (!stopped_) {
      std::shared_ptr<QueuedTask> task;

      {
        std::unique_lock<std::mutex> guard(mutex_);

        int64_t now_ms = NowMs();

        // 把超时的任务从延迟队列中移动到任务队列
        auto it = delayed_task_map_.begin();
        while (it != delayed_task_map_.end()) {
          if (it->first <= now_ms || stopped_) {
            task_list_.push_back(std::move(it->second));
            it = delayed_task_map_.erase(it);
          } else {
            break;
          }
        }

        if (task_list_.empty()) {
          if (!delayed_task_map_.empty()) {

            // wait delayed task timeout
            int64_t next_task_ms = delayed_task_map_.begin()->first;

            int64_t wait_ms = next_task_ms - now_ms;

            if (wait_ms < 0) {
              wait_ms = 0;
            }

            cond_.wait_for(guard, std::chrono::milliseconds(wait_ms));
          } else {

            cond_.wait(guard, [&] {
              return (!task_list_.empty()) || (!delayed_task_map_.empty()) ||
                     stopped_;
            });
          }

          continue;
        }

        task = std::move(task_list_.front());
        task_list_.pop_front();
      }

      if (task->task_id == EXIT_TASK_ID) {
        return;
      }

      try {
        task->run();
      } catch (...) {
        std::cerr << "exception!" << std::endl;
      }

      // 对于同步任务，在这里进行唤醒操作
      if (task->is_sync) {
        std::unique_lock<std::mutex> guard(sync_mutex_);
        task->finished = true;
        sync_cond_.notify_all();
      }

      // 如果是重复任务
      if (!stopped_ && task->repeat_num != 0 && task->delay_ms > 0) {

        ++task->invoke_count;

        if (task->repeat_num == REPEAT_FOREVER ||
            task->invoke_count < task->repeat_num) {

          task->enqueue_time_ms = NowMs();

          uint64_t target_time_ms = task->enqueue_time_ms + task->delay_ms;

          std::unique_lock<std::mutex> guard(mutex_);

          delayed_task_map_.insert(
              std::make_pair(target_time_ms, std::move(task)));
        }
      }
    }
  }

  enum {
    INVALID_TASK_ID = -1,
    EXIT_TASK_ID = 0,
    SYNC_TASK_ID = 1,
    REPEAT_FOREVER = UINT64_MAX,
  };

  std::mutex mutex_;
  std::condition_variable cond_;

  std::thread thread_;

  std::deque<std::shared_ptr<QueuedTask>> task_list_;

  std::mutex sync_mutex_;
  std::condition_variable sync_cond_;

  std::multimap<uint64_t /*time ms*/, std::shared_ptr<QueuedTask>>
      delayed_task_map_;

  std::atomic<bool> stopped_{true};

  std::atomic<int64_t> async_task_id_{SYNC_TASK_ID + 1};

  std::string name_ = "";
};

} // namespace lazy

#endif // _LAZY_TASK_QUEUE_H_
