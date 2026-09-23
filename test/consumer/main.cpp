// The consumer acceptance program. It stays deliberately small: its job is to
// prove that an installed package is findable and its headers are self-contained,
// not to exercise behaviour. Behaviour is the main suite's job.

#include <cloudevents/detail/config.hpp>

int main() {
  // Reading a capability constant is enough to require that the header was found,
  // parsed, and placed its symbols in the expected namespace.
  return ce::v1::detail::has_exceptions ? 0 : 0;
}
