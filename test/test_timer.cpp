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

    uint64_t timer_id = 8;

    task_queue.add_timer(timer_func,interval_ms, timer_id);

    std::this_thread::sleep_for(std::chrono::milliseconds(20 * 1000));

    task_queue.remove_timer(timer_id);

    task_queue.stop();

    return 0;
}
