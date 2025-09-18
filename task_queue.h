/*
 *  Created by mobinsheng.
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

static void SleepMs(uint64_t ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

class QueuedTask {
public:
  QueuedTask() {}
  virtual ~QueuedTask() {}
  virtual void run() = 0;

  // 重载函数调用运算符,直接调用run
  void operator()() { run(); }

  // 唯一标识
  int64_t task_id = -1;

  // is sync task
  // 是否为同步任务
  bool is_sync = false;

  // is task finish
  // 任务是否执行结束
  std::atomic<bool> finished{false};

  // 异步任务 -- begin

  // 投递到任务队列的时刻
  int64_t enqueue_time_ms = 0;

  // 延迟执行的时间
  uint32_t delay_ms = 0;

  // 下面两个用于指定重复执行
  // 循环/重复次数，等于0表示不重复（只执行一次）,等于-1表示无限循环（定时器），大于0表示循环/重复指定次数
  uint64_t repeat_num = 0;
  // 当前已经执行的次数
  uint64_t invoke_count = 0;

  // 异步任务 -- end
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
  SharedClosure ptr(
      new ClosureTask<ReturnT, Closure>(std::forward<Closure>(closure)));
  return ptr;
}

template <class Closure>
static SharedClosure MakeSharedClosure(Closure &&closure) {
  SharedClosure ptr(
      new ClosureTask<void, Closure>(std::forward<Closure>(closure)));
  return ptr;
}

/*
** 任务队列
*/
class TaskQueue {
public:
  TaskQueue(const std::string &name) : name_(name) {}

  ~TaskQueue() { stop(); }

  void start() {
    std::unique_lock<std::mutex> guard(thread_mutex_);
    if (!stopped_) {
      return;
    }

    stopped_ = false;

    thread_ = std::thread([this] { run(); });
  }

  void stop(bool wait_all_task_finish = false) {
    {
      std::unique_lock<std::mutex> guard(mutex_);
      // 如果不等待所有的任务执行完毕
      if (!wait_all_task_finish) {
        task_list_.clear();
        delayed_task_map_.clear();
      }
    }

    std::unique_lock<std::mutex> guard(thread_mutex_);

    if (thread_.joinable()) {

      stopped_ = true;

      post_delayed_internal([] {}, 0, 0, true);

      thread_.join();

      // 重置为空的对象
      thread_ = std::thread();
    }
  }

  // 判断当前所在的线程是否和任务队列的线程为同一个
  bool is_current() {
    std::unique_lock<std::mutex> guard(thread_mutex_);
    if (!thread_.joinable()) {
      return false;
    }
    return std::this_thread::get_id() == thread_.get_id();
  }

  const std::string &name() const { return name_; }

  // 添加异步任务
  template <class Closure> int64_t post(Closure &&closure) {
    // 最后一个参数(repeat_num)等于0表示不是重复任务
    return post_delayed_internal(std::forward<Closure>(closure), 0, 0);
  }

  /* 添加带延迟的异步任务
   * closure: 可执行对象
   * delay_or_interval_ms: 延迟执行的时间
   * task_id: 任务ID，通过cancel接口可以取消
   */
  template <class Closure>
  int64_t post_delayed(Closure &&closure, uint32_t delay_or_interval_ms) {
    return post_delayed_internal(std::forward<Closure>(closure),
                                 delay_or_interval_ms, 0);
  }

  /* 添加带延迟的异步任务
   * closure: 可执行对象
   * delay_or_interval_ms: 延迟执行的时间
   * task_id: 任务ID，通过cancel接口可以取消
   * repeat_num:
   * 重复次数，默认是0表示不重复，等于UINT64_MAX表示无限重复/循环，大于0表示重复指定次数
   */
  template <class Closure>
  int64_t post_delayed_and_repeat(Closure &&closure,
                                  uint32_t delay_or_interval_ms,
                                  uint64_t repeat_num) {
    return post_delayed_internal(std::forward<Closure>(closure),
                                 delay_or_interval_ms, repeat_num);
  }

  // 取消一个异步任务
  // 对于周期性执行的任务（定时器），最好指定一个id，这样方便取消
  void cancel(int64_t task_id) {
    std::unique_lock<std::mutex> guard(mutex_);

    if (task_id == INVALID_TASK_ID) {
      return;
    }

    auto it = delayed_task_map_.begin();

    while (it != delayed_task_map_.end()) {
      if (it->second && it->second->task_id == task_id) {
        // assert(it->second->repeat_num != 0);
        it = delayed_task_map_.erase(it);
      } else {
        ++it;
      }
    }
  }

  // 添加定时器，需要明确指定一个id
  template <class Closure>
  int64_t add_timer(Closure &&closure, uint32_t interval_ms) {

    if (interval_ms == 0) {
      return INVALID_TASK_ID;
    }

    return post_delayed_internal(std::forward<Closure>(closure), interval_ms,
                                 REPEAT_FOREVER);
  }

  // 移除定时器
  void remove_timer(int64_t task_id) { return cancel(task_id); }

  // 执行同步任务
  template <class ReturnT, class Closure> ReturnT invoke(Closure &&closure) {
    std::shared_ptr<QueuedTask> task =
        MakeSharedClosure<ReturnT, Closure>(std::forward<Closure>(closure));
    task->finished = false;
    task->task_id = SYNC_TASK_ID;
    task->enqueue_time_ms = 0;
    task->delay_ms = 0;
    task->is_sync = true;
    task->repeat_num = 0;

    {
      std::unique_lock<std::mutex> guard(mutex_);

      if (stopped_) {
        // 如果已经停止
        throw std::runtime_error("TaskQueue has been stopped.");
      }

      task_list_.push_back(task);
      cond_.notify_one();
    }
    {
      // 等待任务执行结束
      std::unique_lock<std::mutex> guard(sync_mutex_);
      if (!task->finished.load()) {
        sync_cond_.wait(guard, [&] { return task->finished.load(); });
      }
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
                                uint64_t repeat_num = REPEAT_FOREVER,
                                bool exit = false) {
    std::unique_lock<std::mutex> guard(mutex_);

    if (stopped_) {
      return INVALID_TASK_ID;
    }

    if (task_id_ < 0) {
      task_id_ = 1;
    }

    int64_t task_id = 0;

    if (exit) {
      // if exit
      task_id = EXIT_TASK_ID;
    } else {
      task_id = task_id_++;
    }

    std::shared_ptr<QueuedTask> task =
        MakeSharedClosure<void, Closure>(std::forward<Closure>(closure));
    task->finished = false;
    task->task_id = task_id;
    task->enqueue_time_ms = NowMs();
    task->delay_ms = delay_or_interval_ms;
    task->is_sync = false;
    task->repeat_num = repeat_num;
    task->invoke_count = 0;

    int64_t target_time_ms = task->enqueue_time_ms + task->delay_ms;

    delayed_task_map_.insert(std::make_pair(target_time_ms, std::move(task)));
    cond_.notify_one();

    return task_id;
  }

  // 任务队列线程函数
  void run() {
    while (!stopped_) {
      std::shared_ptr<QueuedTask> task;

      int64_t wait_ms = 0;

      {
        std::unique_lock<std::mutex> guard(mutex_);

        int64_t now_ms = NowMs();

        // 把超时的任务从延迟队列中移动到任务队列
        for (auto it = delayed_task_map_.begin();
             it != delayed_task_map_.end();) {
          if (it->first <= now_ms) {
            task_list_.push_back(std::move(it->second));
            it = delayed_task_map_.erase(it);
          } else {
            break;
          }
        }

        if (task_list_.empty()) {
          if (delayed_task_map_.empty()) {

            // 两个队列都为空，等待新的任务到来，或者等待被打断wait
            cond_.wait(guard, [&] {
              return (!task_list_.empty()) || (!delayed_task_map_.empty());
            });

          } else {
            // wait delayed task timeout
            // 延迟队列不为空（还有任务没有超时），那么需要sleep一下
            auto next_task_ms = delayed_task_map_.begin()->first;

            wait_ms = next_task_ms - now_ms;

            if (wait_ms < 0) {
              wait_ms = 0;
            }

            // 等待指定的时长，或者被新的任务到来打断
            cond_.wait_for(guard, std::chrono::milliseconds(wait_ms));
          }

          continue;
        }

        task = std::move(task_list_.front());

        task_list_.pop_front();
      }

      // 非法任务，跳过
      if (task == nullptr) {
        continue;
      }

      // 如果是退出任务，那么退出
      if (task->task_id == EXIT_TASK_ID) {
        stopped_ = true;
        break;
      }

      try {
        task->run();
      } catch (const std::exception &e) {
        std::cerr << "Task exception: " << e.what() << std::endl;
      } catch (...) {
        std::cerr << "Unknown exception occurred in task" << std::endl;
      }

      // 对于同步任务，在这里进行唤醒操作
      if (task->is_sync) {
        std::unique_lock<std::mutex> guard(sync_mutex_);
        task->finished.store(true);
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

          cond_.notify_one();
        }
      }
    }
  }

  const static int64_t INVALID_TASK_ID = -1;

  const static int64_t SYNC_TASK_ID = 0;

  const static int64_t EXIT_TASK_ID = INT64_MAX - 1;

  const static uint64_t REPEAT_FOREVER = UINT64_MAX;

  std::atomic<bool> stopped_{true};

  std::mutex mutex_;
  std::condition_variable cond_;

  std::thread thread_;
  std::mutex thread_mutex_;

  std::atomic<int64_t> task_id_{1};

  std::deque<std::shared_ptr<QueuedTask>> task_list_;

  std::mutex sync_mutex_;
  std::condition_variable sync_cond_;

  std::multimap<uint64_t /*time ms*/, std::shared_ptr<QueuedTask>>
      delayed_task_map_;

  const std::string name_;
};

// 以同步的方式把任务/函数放到任务队列中执行，并等待执行结束
/* 用法如下：

 TaskQueue task_queue;

 int add(int a, int b) {
    RunOnTaskQueue(int, add, a, b);

    return (a+b);
 }

 */

template <typename Func, typename... Args>
auto RunOnTaskQueue(TaskQueue &task_queue, Func &&func, Args &&...args)
    -> decltype(func(std::forward<Args>(args)...)) {
  using ReturnT = decltype(func(std::forward<Args>(args)...));

  if (!task_queue.is_current()) {
    return task_queue.invoke<ReturnT>(
        [=]() { return func(std::forward<Args>(args)...); });
  }

  // 当前线程就是任务队列线程，直接调用
  return func(std::forward<Args>(args)...);
}

} // namespace lazy

#endif // _LAZY_TASK_QUEUE_H_
