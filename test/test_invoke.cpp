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
