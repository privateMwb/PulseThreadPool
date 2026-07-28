// Chaining a task's output into the next stage.
//
// Demonstrates:
// - A three-stage pipeline where each stage feeds the next
// - get()ing one stage's future before enqueue()ing the next
// - Running several independent pipelines concurrently
// - The pool doing the waiting for you when a stage enqueues its own next stage

#include <support/framework.h>

#include <string>
#include <vector>

using namespace ThreadPoolPro;

namespace {
int parseStage(int raw) {
    return raw + 1;
}
int transformStage(int parsed) {
    return parsed * parsed;
}
std::string formatStage(int transformed) {
    return "result=" + std::to_string(transformed);
}
} // namespace

static void run_examples() {

    ThreadPool pool(4);

    // Simplest form: block between stages on the calling thread,
    // enqueueing the next stage only once the previous one is ready.
    setTitle("Sequential chaining");

    Detail::Future<int> parsed = pool.enqueue(parseStage, 4);
    Detail::Future<int> transformed = pool.enqueue(transformStage, parsed.get());
    Detail::Future<std::string> formatted = pool.enqueue(formatStage, transformed.get());

    std::cout << formatted.get() << "\n\n";

    // The same chain, but the whole pipeline runs as one task, so the
    // calling thread only ever waits on a single future.
    setTitle("Chaining inside one task");

    Detail::Future<std::string> pipeline = pool.enqueue([&pool] {
        Detail::Future<int> p = pool.enqueue(parseStage, 9);
        Detail::Future<int> t = pool.enqueue(transformStage, p.get());
        Detail::Future<std::string> f = pool.enqueue(formatStage, t.get());
        return f.get();
    });

    std::cout << pipeline.get() << "\n\n";

    // Several independent pipelines at once — but driven as phases from
    // the main thread, not as nested get()s inside worker tasks. Future::
    // get() only waits, it doesn't help drain the pool the way waitIdle()
    // does; if every worker were simultaneously blocked on its own
    // pipeline's nested get() (as many concurrent chains as threadCount()
    // would manage), none would be left to run the very tasks being
    // waited on. Waiting in phases from an external thread sidesteps that
    // entirely, and still lets all four pipelines' parse stages, then all
    // four transform stages, then all four format stages, run concurrently.
    setTitle("Several pipelines at once");

    std::vector<int> raws = {0, 1, 2, 3};

    std::vector<Detail::Future<int>> parsedFutures;
    for (int raw : raws) {
        parsedFutures.push_back(pool.enqueue(parseStage, raw));
    }
    std::vector<int> parsedValues;
    for (auto& f : parsedFutures) {
        parsedValues.push_back(f.get());
    }

    std::vector<Detail::Future<int>> transformedFutures;
    for (int p : parsedValues) {
        transformedFutures.push_back(pool.enqueue(transformStage, p));
    }
    std::vector<int> transformedValues;
    for (auto& f : transformedFutures) {
        transformedValues.push_back(f.get());
    }

    std::vector<Detail::Future<std::string>> formattedFutures;
    for (int t : transformedValues) {
        formattedFutures.push_back(pool.enqueue(formatStage, t));
    }
    for (auto& f : formattedFutures) {
        std::cout << f.get() << "\n";
    }
}

REGISTER_EXAMPLE_SUITE();
