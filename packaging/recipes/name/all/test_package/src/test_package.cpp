// Minimal smoke test: confirms the installed package's headers are
// reachable and the library links, by constructing the core type.
//
// This deliberately doesn't exercise parsing or other behavior — that
// would need matching the library's actual API surface. Consider
// expanding this to parse a small literal and check the result once
// this is filled in for a real project.
#include <ThreadPoolPro/LRUCache.h>

#include <iostream>

int main() {
    rain::LRUCache<std::string, int> cache(3);
    std::cout << "ThreadPoolPro linked and constructed successfully.\n";
    return 0;
}