// A running task submitting more work into its own pool.
//
// Demonstrates:
// - detach() called from inside a task that's already running on a worker
// - enqueue() called from inside a task, and get() on its own pool's future
// - A small fan-out: one task spawning several children and waiting on them
// - waitIdle() from the outside still sees all of it settle

#include <support/framework.h>

#include <atomic>
#include <vector>

using namespace ThreadPoolPro;

static void run_examples() {

    ThreadPool pool(4);

    // A task can submit further work into the very pool it's running
    // on — submit() routes it straight into the calling worker's own
    // queue when called this way.
    setTitle("A task detaching a child task");

    std::atomic<int> childRan{false};

    pool.detach([&pool, &childRan] {
        std::cout << "parent task running\n";
        pool.detach([&childRan] {
            std::cout << "child task running\n";
            childRan = true;
        });
    });

    pool.waitIdle();
    std::cout << "childRan: " << childRan.load() << "\n\n";

    // A task can also enqueue() and get() on the result — the calling
    // worker will help drain the pool while it blocks, the same way
    // waitIdle() does from an external thread.
    setTitle("A task waiting on its own child's future");

    Detail::Future<int> outer = pool.enqueue([&pool] {
        Detail::Future<int> inner = pool.enqueue([] { return 21; });
        return inner.get() * 2;
    });

    std::cout << "outer result: " << outer.get() << "\n\n";

    // Fanning a batch of children out from one parent task.
    setTitle("Fan-out from a single task");

    Detail::Future<int> sumFuture = pool.enqueue([&pool] {
        std::vector<Detail::Future<int>> children;
        for (int i = 1; i <= 5; ++i) {
            children.push_back(pool.enqueue([](int i) { return i; }, i));
        }

        int sum = 0;
        for (auto& c : children) {
            sum += c.get();
        }
        return sum;
    });

    std::cout << "fan-out sum: " << sumFuture.get() << "\n";
}

REGISTER_EXAMPLE_SUITE();
