// PulseThreadPool WorkStealingQueue concurrent-steal test suite.
//
// Coverage:
// - Multiple concurrent thieves race with the owner's popBottom().
// - Every pushed task is delivered exactly once.
// - No task is lost.
// - No task is executed more than once.
// - Thieves do not terminate merely because the queue is temporarily empty.

#include <support/framework.h>

#include <atomic>
#include <thread>
#include <vector>

using namespace ThreadPoolPro::Detail;


// Verifies that multiple concurrent thieves racing the owner thread's
// popBottom() deliver every task exactly once.
//
// Important:
// steal() returning nullopt does NOT mean the entire operation is
// finished. Another thread may currently own the last task, or the
// owner may be racing with a thief. Therefore thieves keep retrying
// until the global completion count reaches taskCount.
static void concurrent_steal_delivers_each_task_once() {
    constexpr int taskCount = 2000;
    constexpr int thiefCount = 4;

    WorkStealingQueue queue;

    std::vector<std::atomic<int>> executions(taskCount);

    for (auto& count : executions)
        count.store(0, std::memory_order_relaxed);


    // Push every task before starting the thieves.
    //
    // Each task increments its own execution counter. If a task is
    // executed twice, its counter becomes 2. If it is lost, it remains 0.
    for (int i = 0; i < taskCount; ++i) {
        queue.pushBottom(
            Task(
                [i, &executions]() noexcept {
                    executions[i].fetch_add(
                        1,
                        std::memory_order_relaxed);
                }));
    }


    // Number of tasks that have actually been obtained and executed
    // by either the owner or a thief.
    std::atomic<int> completed{0};


    // Start thieves.
    //
    // A thief must NOT exit permanently after one failed steal().
    // A failed steal only means that no task was available at that
    // exact instant. Another thief may currently be competing for
    // the task, or the owner may be in the middle of popBottom().
    std::vector<std::thread> thieves;

    thieves.reserve(thiefCount);

    for (int t = 0; t < thiefCount; ++t) {
        thieves.emplace_back(
            [&queue, &completed]() {
                for (;;) {

                    // Once every task has been consumed, there is
                    // nothing left for this thief to do.
                    if (completed.load(
                            std::memory_order_acquire)
                        >= taskCount) {
                        return;
                    }


                    auto task =
                        queue.steal();


                    if (task) {
                        (*task)();

                        completed.fetch_add(
                            1,
                            std::memory_order_release);

                        continue;
                    }


                    // The queue can be temporarily empty while
                    // another thread is processing the final task.
                    // Yield instead of terminating.
                    std::this_thread::yield();
                }
            });
    }


    // Owner competes with thieves through popBottom().
    //
    // The owner also continues until the global completion count
    // reaches taskCount. This prevents the owner from stopping early
    // merely because a thief temporarily won the race for the current
    // last element.
    for (;;) {

        if (completed.load(
                std::memory_order_acquire)
            >= taskCount) {
            break;
        }


        auto task =
            queue.popBottom();


        if (task) {
            (*task)();

            completed.fetch_add(
                1,
                std::memory_order_release);

            continue;
        }


        // A thief may currently own the remaining task.
        std::this_thread::yield();
    }


    // Wait for all thieves to observe completion and terminate.
    for (auto& thief : thieves)
        thief.join();


    // Exactly taskCount tasks must have been consumed.
    CHK(
        completed.load(
            std::memory_order_acquire)
        == taskCount);


    // Every task must have executed exactly once.
    for (int i = 0; i < taskCount; ++i) {
        CHK(
            executions[i].load(
                std::memory_order_relaxed)
            == 1);
    }


    // The queue must now be empty.
    CHK(
        queue.size()
        == 0);
}


// Executes all concurrent-steal test cases.
static void run_tests() {
    RUN(
        concurrent_steal_delivers_each_task_once);
}


REGISTER_TEST_SUITE();