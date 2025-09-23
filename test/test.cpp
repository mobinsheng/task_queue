#include "task_queue.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <cassert>
#include <chrono>

using namespace lazy;

void test_basic_async_post() {
    std::cout << "[Test] Basic async post... ";
    TaskQueue queue("test_queue_1");
    queue.start();

    std::atomic<bool> called{false};
    queue.post([&] {
        called = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    queue.stop();

    assert(called.load());
    std::cout << "OK\n";
}

void test_delayed_task() {
    std::cout << "[Test] Delayed task... ";
    TaskQueue queue("test_queue_2");
    queue.start();

    std::atomic<bool> called{false};
    auto start = NowMs();

    queue.post_delayed([&] {
        called = true;
    }, 200);

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    queue.stop();

    assert(called.load());
    auto end = NowMs();
    assert(end - start >= 200);
    std::cout << "OK\n";
}

void test_sync_invoke() {
    std::cout << "[Test] Sync invoke... ";
    TaskQueue queue("test_queue_3");
    queue.start();

    int result = queue.invoke<int>([] {
        return 123;
    });

    queue.stop();

    assert(result == 123);
    std::cout << "OK\n";
}

void test_timer_repeat() {
    std::cout << "[Test] Timer repeat... ";
    TaskQueue queue("test_queue_4");
    queue.start();

    std::atomic<int> count{0};

    int64_t id = queue.add_timer([&] {
        count++;
    }, 100);

    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    queue.remove_timer(id);

    queue.stop();

    assert(count.load() >= 3);  // 至少触发三次
    std::cout << "OK\n";
}

void test_cancel_task() {
    std::cout << "[Test] Cancel task... ";
    TaskQueue queue("test_queue_5");
    queue.start();

    std::atomic<bool> called{false};

    int64_t id = queue.post_delayed([&] {
        called = true;
    }, 200);

    queue.cancel(id);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    queue.stop();

    assert(!called.load());
    std::cout << "OK\n";
}

void test_stop_behavior() {
    std::cout << "[Test] Stop behavior... ";
    TaskQueue queue("test_queue_6");
    queue.start();

    std::atomic<int> counter{0};
    queue.post_delayed([&] {
        counter++;
    }, 100);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    queue.stop();

    assert(counter.load() == 1);
    std::cout << "OK\n";
}

int main() {
    test_basic_async_post();
    test_delayed_task();
    test_sync_invoke();
    test_timer_repeat();
    test_cancel_task();
    test_stop_behavior();

    std::cout << "\nAll tests passed.\n";
    return 0;
}
