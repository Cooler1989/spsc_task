Recruitment Task: Implement a Single-Producer / Single-Consumer
(SPSC) Queue in C++

As part of the recruitment process, we’d like you to implement a Single-Producer / Single-
Consumer (SPSC) queue in C++ and provide a set of tests. Please submit your solution before

the interview, because we will use it as a basis for discussion (design decisions, correctness,
performance).
Goal
Implement a bounded SPSC queue that can be used safely by:
• exactly one producer thread (pushing items),
• exactly one consumer thread (popping items).
You should also provide unit tests (and optionally stress/performance tests) that demonstrate
correctness and robustness.
Requirements
1. Implement a SPSC queue.
2. The queue must be bounded (fixed capacity chosen at construction time).
3. You may choose API operations that queue provides.
4. The implementation must be safe for concurrent use by exactly one producer and
exactly one consumer thread.
5. The implementation should be original and rely exclusively on the C/C++ standard
library. No external or third-party dependencies (including Boost) may be used.
6. Target standard: C++17 or newer.
7. Must build and run on Linux (assume clang or gcc).
8. Use CMake as a build system.
9. Use GoogleTest as a test framework.
10. Include a short README describing:
a. assumptions / known limitations,
b. how to build,
c. how to run the tests.
11. Please submit GitHub or Godbolt (https://godbolt.org) link with:
a. Source code for the queue implementation.
b. Test code.
c. README.
Be prepared to talk about:
• API choices and semantics,
• Correctness,
• Performance and trade-offs,
• Clean code.