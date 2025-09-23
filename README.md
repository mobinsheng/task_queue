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
```

## post
```cpp

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

```

## timer
```cpp

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
```
