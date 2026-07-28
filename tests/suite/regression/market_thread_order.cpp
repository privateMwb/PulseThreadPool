// PulseThreadPool MarketThread construction-order regression test.
//
// Regression coverage:
// - thread_ must be declared (and thus constructed) after
//   job_/hasWork_/exiting_/id_, since its constructor starts the OS
//   thread immediately and that thread begins touching those members
//   right away via loop(). The original member order (thread_ first)
//   let the new thread observe them mid-construction. Not something a
//   plain assertion can catch directly — this stresses the exact path
//   (construct, then immediately destroy) many times over so a thread
//   sanitizer has a real chance to flag any regression.
//
// IMPORTANT: this test must never let a locally-constructed MarketThread
// run a job to completion. MarketThread::loop() always finishes by
// calling the process-wide ThreadMarket::instance().returnToIdle(this),
// regardless of whether the thread was ever leased from the market. If
// we called assign()/waitDone() here, each iteration would register a
// pointer to this stack-local object into the singleton's idle list;
// once the object goes out of scope, that pointer dangles, and some
// unrelated later test can crash when ThreadMarket::lease() hands it
// out. Constructing and immediately destroying still exercises the
// vulnerable startup window (the OS thread enters loop() and touches
// hasWork_/exiting_ right away), because the destructor sets exiting_
// before hasWork_ ever goes true, so loop() returns immediately without
// ever reaching returnToIdle().

#include <support/framework.h>

using namespace ThreadPoolPro::Detail;

static void construct_and_immediately_assign_is_race_free() {
    constexpr int iterations = 200;

    for (int i = 0; i < iterations; ++i) {
        MarketThread thread;
        // Deliberately no assign()/waitDone(): see comment above.
    }
}

// Executes all MarketThread construction-order regression test cases.
static void run_tests() {
    RUN(construct_and_immediately_assign_is_race_free);
}

REGISTER_TEST_SUITE();
