/// \file
/// \brief Proves the module can be imported, not merely compiled.
///
/// Built only when CE_BUILD_MODULE is ON. A module interface that compiles but
/// exports nothing usable would otherwise pass unnoticed: module_test reads the
/// file, and only this translation unit consumes it.
///
/// It includes NO standard header on purpose. GCC's -fmodules-ts rejects a
/// translation unit that includes a header the module's global module fragment
/// already absorbed, reporting a redefinition inside libstdc++ rather than
/// anything about this file (D-MODULE-1). Everything used here therefore comes
/// from the module.

import cloudevents;

int main() {
  // The literals are exported too, or a module consumer could name `event` and
  // never build one.
  using namespace ce::literals;

  auto parsed = ce::parse_timestamp("2026-09-20T12:34:56Z");
  if (!parsed) {
    return 1;
  }
  ce::event subject{"id-1"_id, "/module"_source, "com.example.module"_type,
                    {.subject = "through the module"_subject, .time = *parsed}};

  if (!subject.set(ce::ext::tracing{.traceparent = "00-a-b-01", .tracestate = {}})) {
    return 2;
  }
  // And the run-time factories, for text that is not a literal.
  if (!ce::id::make(subject.id().view())) {
    return 3;
  }

  // Member functions only: the free comparison operators for std::string come
  // from <string>, which this translation unit deliberately does not include.
  auto tracing = subject.get<ce::ext::tracing>();
  if (!tracing || tracing->traceparent.compare("00-a-b-01") != 0) {
    return 4;
  }
  if (ce::to_string(*subject.time()).compare("2026-09-20T12:34:56Z") != 0) {
    return 5;
  }
  if (!ce::base64_encode(ce::binary{}).empty()) {
    return 6;
  }
  if (!ce::valid_attribute_name("seq9") || ce::valid_attribute_name("Seq")) {
    return 7;
  }
  return 0;
}
