#include <boost/ut.hpp>

#include <cloudevents/binding/http.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/describe.hpp>
#include <cloudevents/extensions.hpp>
#include <cloudevents/format/base64.hpp>
#include <cloudevents/format/describe_json.hpp>
#include <cloudevents/format/json_codec.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/format/typed_payload.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef CE_MODULE_FILE
#error "CE_MODULE_FILE must be defined by the build"
#endif
#ifndef CE_INCLUDE_DIR
#error "CE_INCLUDE_DIR must be defined by the build"
#endif
#ifndef CE_MODULE_IN_DEFAULT_BUILD
#error "CE_MODULE_IN_DEFAULT_BUILD must be defined by the build"
#endif

// The module wrapper cannot be imported here: module support across the
// toolchains SPEC section 8 names is uneven, which is the reason it is kept out
// of the default build in the first place.
//
// So the two properties that CAN be checked are checked properly.
//
// That every exported name exists is proven by the compiler: the list below
// expands to real using-declarations, and a name that is gone fails the build.
// That the module exports exactly that list, and includes every public header,
// is proven by reading the file. One list drives both, so the two cannot drift.

#define CE_MODULE_EXPORTS(X)                                                                     \
  X(errc) X(error) X(fail) X(result) X(static_error) X(to_string_view) X(widen)                   \
  X(datacontenttype) X(dataschema) X(extension_name) X(id) X(source) X(spec_version) X(subject)  \
  X(type)                                                                                        \
  X(attribute_value) X(binary) X(content_mode) X(data_t) X(event) X(raw_headers)                     \
  X(is_json_content_type) X(json_document) X(json_text) X(lint_warning) X(message)          \
  X(parse_timestamp)                                                                             \
  X(reserved_name) X(timestamp) X(to_string) X(to_bytes) X(to_text) X(uri) X(uri_ref)\
  X(valid_attribute_name)           \
  X(backend_of) X(describe_backend) X(described) X(field_count) X(field_names)                   \
  X(for_each_field) X(members_supported) X(name) X(reflect) X(skip)                              \
  X(base64_decode) X(base64_encode) X(data_as) X(decode_as) X(decode_batch_as) X(decoded)       \
  X(encode_as) X(event_of) X(from_json_value) X(from_value_as) X(json_format) X(set_data)        \
  X(to_json_value)

namespace {

using namespace std::string_view_literals;

// The compiler's half of the check: every exported name must exist in ce::.
namespace exported_names_exist {
#define CE_USING(n_) using ce::n_;
CE_MODULE_EXPORTS(CE_USING)
#undef CE_USING
}  // namespace exported_names_exist

#define CE_NAME_STRING(n_) #n_##sv,
constexpr std::array exported_names{CE_MODULE_EXPORTS(CE_NAME_STRING)};
#undef CE_NAME_STRING

/// Names exported from a nested namespace inside the module. Checked for
/// presence in the file, and their existence is proven by the includes above.
constexpr std::array nested_exports{
    "ce::v4::json::json_codec"sv, "ce::v4::json::kind"sv, "ce::v4::json::decode_options"sv,
    "ce::v4::http::from_message"sv, "ce::v4::http::to_message"sv,
    "ce::v4::ext::tracing"sv,       "ce::v4::ext::dataref"sv,
    "ce::v4::binding::binding_traits"sv,
    "ce::v4::binding::write_attributes"sv,
    "ce::v4::binding::read_attributes"sv,
    "ce::v4::kafka::to_record"sv,
    "ce::v4::kafka::from_message"sv,
    "ce::v4::kafka::partitionkey_mapper"sv,
    "ce::v4::nats::to_payload"sv,
    "ce::v4::nats::from_payload"sv,
    "ce::v4::nats::to_message"sv,
    "ce::v4::nats::from_message"sv,
    "ce::v4::literals::operator\"\"_dataschema"sv,
    "ce::v4::literals::operator\"\"_ext"sv,
    "ce::v4::literals::operator\"\"_id"sv,
    "ce::v4::literals::operator\"\"_mediatype"sv,
    "ce::v4::literals::operator\"\"_source"sv,
    "ce::v4::literals::operator\"\"_subject"sv,
    "ce::v4::literals::operator\"\"_type"sv,
};

[[nodiscard]] auto read_module() -> std::string {
  std::ifstream in{CE_MODULE_FILE, std::ios::binary};
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

/// \brief Every public header, meaning one a consumer may include directly.
///
/// `detail/` is private and `codec/nlohmann.hpp` is deliberately excluded: the
/// module must not drag a third-party dependency in.
[[nodiscard]] auto public_headers() -> std::vector<std::string> {
  std::vector<std::string> found;
  const std::filesystem::path root{CE_INCLUDE_DIR};
  for (const auto& entry : std::filesystem::recursive_directory_iterator{root}) {
    if (!entry.is_regular_file() || entry.path().extension() != ".hpp") {
      continue;
    }
    const std::string relative = std::filesystem::relative(entry.path(), root).generic_string();
    // Any detail/ segment, not only the top-level one: binding/detail/ is just
    // as private, and a header there is not something a module consumer names.
    // cloudevents/v1/, cloudevents/v2/ and cloudevents/v3/ are the frozen
    // generations, which the module does not export (ADR-0009, ADR-0010,
    // ADR-0012): a using-declaration of a v1, v2 or v3 name into ce::v4 exports
    // nothing.
    if (relative.find("/detail/") != std::string::npos ||
        relative.find("cloudevents/codec/") != std::string::npos ||
        relative.starts_with("cloudevents/v1/") || relative.starts_with("cloudevents/v2/") ||
        relative.starts_with("cloudevents/v3/") ||
        // The v3 conversions are opt-in and reachable only through their own
        // header, which the module does not include (SWR-BUILD-0013).
        relative == "cloudevents/v3_conversion.hpp") {
      continue;
    }
    found.push_back(relative);
  }
  std::sort(found.begin(), found.end());
  return found;
}

// spec: SWR-BUILD-0010
const boost::ut::suite<"module-wrapper-optional"> module_wrapper = [] {
  using namespace boost::ut;

  "the module interface is shipped"_test = [] {
    expect(std::filesystem::exists(CE_MODULE_FILE))
        << "cloudevents.cppm is missing from " << CE_MODULE_FILE;
    const std::string source = read_module();
    expect(!source.empty());
    expect(source.find("export module cloudevents;") != std::string::npos)
        << "the file does not declare the module";
    // A global module fragment is what lets it include the headers at all.
    expect(source.find("module;") != std::string::npos);
    expect(source.find("module;") < source.find("export module cloudevents;"));
  };

  "it is excluded from the default build"_test = [] {
    // The point of the requirement: a consumer on a toolchain with no module
    // support must still configure and build.
    expect(CE_MODULE_IN_DEFAULT_BUILD == 0)
        << "the module target is in the default build, which SWR-BUILD-0010 forbids";
  };

  "it includes every public header"_test = [] {
    // A new public header that the module does not re-export would be invisible
    // to a module consumer, and nothing else would notice.
    const std::string source = read_module();
    for (const auto& header : public_headers()) {
      if (header == "cloudevents/cloudevents.cppm") {
        continue;
      }
      expect(source.find("#include <" + header + ">") != std::string::npos)
          << "the module does not include " << header;
    }
    expect(!public_headers().empty());
  };

  "it does not drag in the nlohmann codec"_test = [] {
    // ce::core depends on no third-party library except CTRE, and a module that
    // re-exported the nlohmann codec would make every module consumer need it.
    const std::string source = read_module();
    expect(source.find("codec/nlohmann.hpp") == std::string::npos);
    expect(source.find("nlohmann") == std::string::npos);
  };

  "it exports exactly the names this test names"_test = [] {
    // The other half of the check. The using-declarations above already proved
    // these names exist; this proves the module actually exports them.
    const std::string source = read_module();
    for (const auto exported : exported_names) {
      const std::string declaration = "using ce::v4::" + std::string{exported} + ";";
      expect(source.find(declaration) != std::string::npos)
          << "the module does not export ce::" << exported;
    }

    for (const auto nested : nested_exports) {
      const std::string declaration = "using " + std::string{nested} + ";";
      expect(source.find(declaration) != std::string::npos)
          << "the module does not export " << nested;
    }

    // And nothing beyond them: a stray export would be a name with no
    // compile-time check behind it.
    std::set<std::string> named;
    for (const auto exported : exported_names) {
      named.insert(std::string{exported});
    }
    std::size_t pos = 0;
    std::size_t counted = 0;
    const std::string marker = "using ce::v4::";
    while ((pos = source.find(marker, pos)) != std::string::npos) {
      const std::size_t start = pos + marker.size();
      const std::size_t end = source.find(';', start);
      if (end == std::string::npos) {
        break;
      }
      const std::string symbol = source.substr(start, end - start);
      // Nested exports are spelled ce::v4::json::x and handled above.
      if (symbol.find("::") == std::string::npos) {
        expect(named.contains(symbol)) << "the module exports ce::" << symbol
                                       << ", which this test does not name";
        ++counted;
      }
      pos = end;
    }
    expect(counted == exported_names.size())
        << "the module exports " << counted << " top-level names, the test names "
        << exported_names.size();
  };
};

// spec: SWR-BUILD-0013
const boost::ut::suite<"module-exports-v4-only"> module_exports_v4_only = [] {
  using namespace boost::ut;

  // A frozen generation is reached by including its headers, never through the
  // module (ADR-0009, ADR-0010, ADR-0012). Exporting one would put a second event
  // model behind every import, and the v3 conversions would pull in the v3 one.
  "it exports no frozen generation"_test = [] {
    const std::string source = read_module();
    for (const auto frozen : {"using ce::v1::"sv, "using ce::v2::"sv, "using ce::v3::"sv}) {
      expect(source.find(frozen) == std::string::npos)
          << "the module exports a name through " << frozen;
    }
    for (const auto copies : {"cloudevents/v1/"sv, "cloudevents/v2/"sv, "cloudevents/v3/"sv}) {
      expect(source.find(copies) == std::string::npos)
          << "the module includes a frozen header from " << copies;
    }
  };

  "it exports neither conversion between v3 and v4"_test = [] {
    const std::string source = read_module();
    expect(source.find("from_v3") == std::string::npos);
    expect(source.find("to_v3") == std::string::npos);
    expect(source.find("v3_conversion") == std::string::npos);
  };
};

}  // namespace

#undef CE_MODULE_EXPORTS

int main() {}
