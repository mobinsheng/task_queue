# task_queue
Task Queue. List of Features: 
- invoke: Add the task into the task queue and wait for it to complete
- post: Put the task into the task queue and return immediately without waiting for it to complete
- add_task: like post
- timer: Put a task into the queue and execute it repeatedly


## invoke
```cpp

int add(int a, int b) {
	return a + b;
}

void test_task_queue() {
  // defined task queue
	lazy::TaskQueue task_queue("my task queue");

  // start 
	task_queue.start();

	int a = 1, b = 2;

  // invoke task
	auto sum = task_queue.invoke<int>([&] {return add(a, b); });
	assert(sum == a + b);

  // stop
	task_queue.stop();
}
```

## add_task
```cpp

class Test {
public:
	Test() {

	}

	~Test() {

	}

	int get_val() const {
		return val_;
	}

	void compute() {
		for (int i = 0; i < 1000000; ++i){
      val_ += i;
    }
	}
private:
	int64_t val_ = 0;
};


void test_task_queue() {
	lazy::TaskQueue task_queue("my task queue");
	task_queue.start();

	Test test;

  uint64_t task1_id = 0;

	task_queue.add_task([&] {test.compute(); }, task1_id);

  uint64_t task2_id = 1;

	task_queue.add_task([&] {test.compute(); }, task2_id);

  task_queue.cancel(task2_id);

	task_queue.stop();
}
```

## post
```cpp

class Test {
public:
	Test() {

	}

	~Test() {

	}

	int get_val() const {
		return val_;
	}

	void compute() {
		for (int i = 0; i < 1000000; ++i){
      val_ += i;
    }
	}
private:
	int64_t val_ = 0;
};


void test_task_queue() {
	lazy::TaskQueue task_queue("my task queue");
	task_queue.start();

	int a = 1, b = 2;

	auto sum = task_queue.invoke<int>([&] {return add(a, b); });
	assert(sum == a + b);

	Test test;


	task_queue.post([&] {test.compute(); });

	uint64_t task2_id = 0;

	task_queue.post([&] {test.compute(); }, task2_id);

	task_queue.cancel(task2_id);

	int delay_ms = 500;

	task_queue.post_delayed([&] {test.compute(); }, delay_ms);

	uint64_t task3_id = 1;

	int repeat_num = 3;

	task_queue.post_delayed_and_repeat([&] {test.compute(); }, delay_ms, task3_id, repeat_num);

	task_queue.stop();
}
```

## timer
```cpp

void timer_func() {
	printf("test\r\n");
}

void test_task_queue() {
	lazy::TaskQueue task_queue("my task queue");
	task_queue.start();

	uint64_t timer_id = 0;

	uint32_t interval_ms = 500;

	task_queue.add_timer([&] {timer_func(); }, interval_ms, timer_id);

	Sleep(20 * 1000);

	task_queue.cancel(timer_id);

	task_queue.stop();
}

```
