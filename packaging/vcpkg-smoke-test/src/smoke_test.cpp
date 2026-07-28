// Minimal smoke test: confirms the vcpkg-installed package's headers
// are reachable and the library links, by constructing the core type.
//
// Same scope as the Conan test_package smoke test — proves the
// package is installable and linkable, not that its behavior is
// correct. Expand this to parse a small literal and check the result
// once filled in for a real project.
#include <ThreadPoolPro/ThreadPool.h>

#include <iostream>

int main() {
    rain::ThreadPool pool(4);
    std::cout << "ThreadPoolPro linked and constructed successfully.\n";
    return 0;
}