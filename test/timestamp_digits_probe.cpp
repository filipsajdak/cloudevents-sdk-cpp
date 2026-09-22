// A literal fractional-digit count above nine must not compile.
//
// SWR-CORE-0030 says the count cannot exceed what a nanosecond instant
// expresses. `make` reports that at run time; a literal is refused when the
// translation unit is compiled, and no program containing the offending line
// exists to assert it from. So this file is EXCLUDE_FROM_ALL and registered as a
// test that passes when the build FAILS.

#include <cloudevents/core.hpp>

// Ten digits. The consteval constructor reaches a function that has no
// definition, which is the error, and the function's name is the diagnostic.
const ce::timestamp beyond{
    .utc = std::chrono::sys_time<std::chrono::nanoseconds>{},
    .fractional_digits = 10,
};

int main() {}
