#include "task_queue.h"

static void SleepMs(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

class Test {
public:
    void compute(uint32_t n) {
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

    task_queue.add_task([&]{test.compute(n);});

    SleepMs(1000);

    uint64_t sum = test.Sum();

    task_queue.stop();

    return 0;
}
