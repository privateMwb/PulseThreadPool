// PulseThreadPool WorkStealingQueue test suite.
//
// Coverage:
// - popBottom() on an empty queue returns nullopt
// - pushBottom()/popBottom() round-trips a task correctly
// - popBottom() takes tasks in LIFO order from the owner end
// - steal() on an empty queue returns nullopt
// - steal() takes from the top (oldest-pushed) end
// - A stolen task is invocable and only delivered once
// - size() tracks pushBottom(), popBottom(), and steal()

#include <support/framework.h>

using namespace ThreadPoolPro::Detail;

// Verifies popBottom() on an empty queue returns nullopt.
static void pop_on_empty_returns_nullopt() {
    WorkStealingQueue queue;
    CHK(!queue.popBottom().has_value());
}

// Verifies pushBottom()/popBottom() round-trips a task correctly.
static void push_then_pop_returns_same_task() {
    WorkStealingQueue queue;
    int value = 0;
    queue.pushBottom(Task([&value]() { value = 7; }));

    auto task = queue.popBottom();
    CHK(task.has_value());
    (*task)();
    CHK(value == 7);
}

// Verifies popBottom() takes tasks in LIFO order from the owner end.
static void pop_order_is_lifo() {
    WorkStealingQueue queue;
    std::vector<int> order;

    queue.pushBottom(Task([&order]() { order.push_back(1); }));
    queue.pushBottom(Task([&order]() { order.push_back(2); }));
    queue.pushBottom(Task([&order]() { order.push_back(3); }));

    (*queue.popBottom())();
    (*queue.popBottom())();
    (*queue.popBottom())();

    CHK((order == std::vector<int>{3, 2, 1}));
}

// Verifies steal() on an empty queue returns nullopt.
static void steal_on_empty_returns_nullopt() {
    WorkStealingQueue queue;
    CHK(!queue.steal().has_value());
}

// Verifies steal() takes from the top (oldest-pushed) end.
static void steal_takes_oldest_task() {
    WorkStealingQueue queue;
    std::vector<int> order;

    queue.pushBottom(Task([&order]() { order.push_back(1); }));
    queue.pushBottom(Task([&order]() { order.push_back(2); }));
    queue.pushBottom(Task([&order]() { order.push_back(3); }));

    (*queue.steal())();
    (*queue.steal())();

    CHK((order == std::vector<int>{1, 2}));
}

// Verifies a stolen task is invocable and only delivered once.
static void stolen_task_runs_exactly_once() {
    WorkStealingQueue queue;
    int calls = 0;
    queue.pushBottom(Task([&calls]() { ++calls; }));

    auto stolen = queue.steal();
    CHK(stolen.has_value());
    (*stolen)();
    CHK(calls == 1);
    CHK(queue.size() == 0);
}

// Verifies a freshly constructed queue reports size 0.
static void fresh_queue_is_empty() {
    WorkStealingQueue queue;
    CHK(queue.size() == 0);
}

// Verifies size() grows with pushBottom() and shrinks with popBottom().
static void size_tracks_push_and_pop() {
    WorkStealingQueue queue;
    queue.pushBottom(Task([]() {}));
    queue.pushBottom(Task([]() {}));
    CHK(queue.size() == 2);

    auto popped = queue.popBottom();
    CHK(queue.size() == 1);
    (*popped)();

    auto remaining = queue.popBottom();
    (*remaining)();
}

// Verifies size() shrinks with steal() too.
static void size_tracks_steal() {
    WorkStealingQueue queue;
    queue.pushBottom(Task([]() {}));
    queue.pushBottom(Task([]() {}));

    auto stolen = queue.steal();
    CHK(queue.size() == 1);
    (*stolen)();

    auto remaining = queue.popBottom();
    (*remaining)();
}

// Executes all WorkStealingQueue test cases.
static void run_tests() {
    RUN(pop_on_empty_returns_nullopt);
    RUN(push_then_pop_returns_same_task);
    RUN(pop_order_is_lifo);
    RUN(steal_on_empty_returns_nullopt);
    RUN(steal_takes_oldest_task);
    RUN(stolen_task_runs_exactly_once);
    RUN(fresh_queue_is_empty);
    RUN(size_tracks_push_and_pop);
    RUN(size_tracks_steal);
}

REGISTER_TEST_SUITE();
