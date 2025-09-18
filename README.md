# TaskQueue
The TaskQueue class implements a lightweight task queue with the following features:

1. Asynchronous Task Execution
    - Allows submitting arbitrary functions or callable objects asynchronously to the queue, executed by an internal worker thread.

2. Delayed Task Scheduling
    - Supports scheduling tasks to execute after a specified delay, enabling timer-like functionality.

3. Repeated Task Execution (Timers)
    - Supports periodic repeated execution of tasks, configurable with a specific repeat count or infinite repetition.

4. Synchronous Task Execution
    - Allows invoking tasks synchronously and waiting for their completion with a returned result.

5. Task Cancellation
    - Enables cancellation of submitted but not yet executed tasks by their task ID, particularly useful for timer removal.

6. Thread Safety
    - Employs mutexes and condition variables internally to ensure safe access to the queue in multithreaded environments.


## invoke
```cpp

#include "task_queue.h"

class Test {
public:
    uint64_t compute(uint32_t n) {
        uint64_t sum = 0;
        for (uint32_t i = 0; i < n; ++i){
            sum += i;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        return sum;
    }
};

int main(){
    lazy::TaskQueue task_queue("my task queue");

    task_queue.start();

    Test test;

    // invoke task
    uint64_t sum = task_queue.invoke<uint64_t>(std::bind(&Test::compute, &test, 10));

    task_queue.stop();

    return 0;
}
```

## post
```cpp
#include "task_queue.h"

static void SleepMs(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

class Test {
public:
    void compute(uint32_t n) {
        sum_ = 0;

        for (uint32_t i = 0; i < n; ++i){
            sum_ += i;

            SleepMs(10);
        }
    }
    uint64_t Sum() {return sum_;}
private:
    uint64_t sum_ = 0;
};


int main(){
    lazy::TaskQueue task_queue("my task queue");

    task_queue.start();

    Test test;

    int n = 10;

    // post task1
    uint64_t task_id = task_queue.post([&]{test.compute(n);});

    // post task2
    uint64_t task_id2 = task_queue.post([&]{test.compute(n);});

    // cancel task1
    task_queue.cancel(task_id);

    SleepMs(1000);

    uint64_t sum = test.Sum();

    task_queue.stop();

    return 0;
}
```

## timer
```cpp
#include "task_queue.h"

int val = 0;

void timer_func() {
    printf("%d\r\n", val);
    ++val;
}

int main(){
    lazy::TaskQueue task_queue("my task queue");

    task_queue.start();

    int interval_ms = 500;

    // add timer
    uint64_t timer_id = task_queue.add_timer(timer_func,interval_ms);

    std::this_thread::sleep_for(std::chrono::milliseconds(20 * 1000));

    // stop timer
    task_queue.remove_timer(timer_id);

    task_queue.stop();

    return 0;
}
```
