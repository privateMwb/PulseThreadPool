// Shutting down from inside the pool's own worker.
//
// Demonstrates:
// - shutdown() called from a task running on the pool it belongs to
// - Why this doesn't deadlock: the calling worker is detached, not joined
// - waitIdle() being the one call that's genuinely unsafe from a worker
// - The safe alternative when a task needs to know its own pool is done

#include <support/framework.h>

#include <atomic>
#include <thread>

using namespace ThreadPoolPro;

static void run_examples() {

    // A plain std::thread joining itself is undefined behavior — the
    // library documents that shutdown() specifically avoids this for
    // the worker that calls it, by detaching that one thread instead
    // of joining it.
    setTitle("shutdown() called from a worker is safe");

    {
        ThreadPool pool(2);
        std::atomic<bool> ran{false};

        pool.detach([&pool, &ran] {
            ran = true;
            pool.shutdown(); // called from inside the pool's own task
        });

        // shutdown() from the outside still has to wait for that
        // worker's detached thread to actually finish on its own.
        while (!pool.isStopped()) {
            std::this_thread::yield();
        }

        std::cout << "ran     : " << ran.load() << "\n";
        std::cout << "isStopped: " << pool.isStopped() << "\n\n";
    }

    // waitIdle() has no such protection — it's documented as unsafe to
    // call from a pool worker on its own pool, since that worker would
    // be blocking on the very condition only other workers (not it)
    // can help satisfy. The fix is to not wait from inside a task at
    // all: report completion through the Future/detach() mechanism
    // instead, and let the *external* caller be the one that waits.
    setTitle("waitIdle() from a worker: don't");

    {
        ThreadPool pool(2);

        Detail::Future<int> f = pool.enqueue([] { return 1 + 1; });

        // Correct: wait from the external caller...
        std::cout << "result: " << f.get() << "\n";

        // ...not: pool.enqueue([&pool] { pool.waitIdle(); return 0; });
        // — that task would be one of the very things waitIdle() is
        // waiting on, so it can never observe the pool going idle.
    }
}

REGISTER_EXAMPLE_SUITE();
