// RAII lifecycle example.
//
// Demonstrates:
// - owning a ThreadPool as a class member
// - submitting work through a small service wrapper
// - the pool draining automatically when the owning object is destroyed
// - explicitly shutting down early, before the owner itself is destroyed

#include <common/framework.h>

#include <atomic>
#include <chrono>
#include <thread>

using namespace ThreadPoolPro;

// A small service that owns its own worker pool for the duration of its lifetime.
class LogUploader {
public:
    explicit LogUploader(std::size_t threadCount) : pool_(threadCount) {}

    void upload(int id) {
        pool_.detach([id, this] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            uploaded_.fetch_add(1, std::memory_order_relaxed);
            (void)id;
        });
    }

    [[nodiscard]] int uploadedCount() const {
        return uploaded_.load(std::memory_order_relaxed);
    }

    void shutdownNow() {
        pool_.shutdown(ThreadPool::ShutdownMode::FinishTasks);
    }

private:
    ThreadPool pool_;
    std::atomic<int> uploaded_{0};
};

static void run_examples() {
    // The pool's lifetime is tied to the owning object's lifetime.
    setTitle("Owned By A Service");

    {
        LogUploader uploader{4};

        for (int i = 0; i < 20; ++i)
            uploader.upload(i);

        std::cout << "Submitted 20 uploads\n";
        // uploader goes out of scope here; its ThreadPool member is
        // destroyed, which drains all remaining uploads (FinishTasks
        // by default) before this block exits.
    }

    std::cout << "Service destroyed; all uploads finished\n\n";

    // Shutting down explicitly, before the owner itself is destroyed.
    setTitle("Explicit Early Shutdown");

    LogUploader earlyShutdown{4};

    for (int i = 0; i < 20; ++i)
        earlyShutdown.upload(i);

    earlyShutdown.shutdownNow();

    std::cout << "Uploaded : " << earlyShutdown.uploadedCount() << " / 20\n";
}

REGISTER_EXAMPLE_SUITE();
