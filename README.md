# task_queue
Task Queue. List of Features: 
- invoke: Add the task into the task queue and wait for it to complete
- post: Put the task into the task queue and return immediately without waiting for it to complete
- timer: Put a task into the queue and execute it repeatedly


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

    uint64_t task_id = task_queue.post([&]{test.compute(n);});

    uint64_t task_id2 = task_queue.post([&]{test.compute(n);});

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

    uint64_t timer_id = task_queue.add_timer(timer_func,interval_ms);

    std::this_thread::sleep_for(std::chrono::milliseconds(20 * 1000));

    task_queue.remove_timer(timer_id);

    task_queue.stop();

    return 0;
}
```
