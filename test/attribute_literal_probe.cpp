// An invalid attribute literal must not compile.
//
// SWR-CORE-0027 says the compile-time path rejects the translation unit rather
// than producing a value. No assertion inside a running program can witness that,
// because a program containing the offending line does not exist. So this file is
// EXCLUDE_FROM_ALL and registered as a test that passes when the build FAILS.
//
// "id!" breaks no rule on its own - id accepts any non-empty, well-formed UTF-8
// text. The empty literal is the one every policy refuses, so that is what this
// probes.

#include <cloudevents/core.hpp>

using namespace ce::literals;

// The empty string is refused by every attribute rule. Reaching the consteval
// constructor with it calls a function that has no definition, which is the
// error, and the function's name is the diagnostic.
const ce::id refused = ""_id;

int main() {}
