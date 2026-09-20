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
  ce::event subject{.id = "id-1", .source = ce::uri_ref{"/module"}, .type = "com.example.module"};
  subject.subject = "through the module";

  auto parsed = ce::parse_timestamp("2026-09-20T12:34:56Z");
  if (!parsed) {
    return 1;
  }
  subject.time = *parsed;

  if (!subject.set(ce::ext::tracing{.traceparent = "00-a-b-01", .tracestate = {}})) {
    return 2;
  }
  if (!subject.validate()) {
    return 3;
  }

  // Member functions only: the free comparison operators for std::string come
  // from <string>, which this translation unit deliberately does not include.
  auto tracing = subject.get<ce::ext::tracing>();
  if (!tracing || tracing->traceparent.compare("00-a-b-01") != 0) {
    return 4;
  }
  if (ce::to_string(*subject.time).compare("2026-09-20T12:34:56Z") != 0) {
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
