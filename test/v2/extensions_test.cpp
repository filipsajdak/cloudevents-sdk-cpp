#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <boost/ut.hpp>

#include <cloudevents/v2/binding/http.hpp>
#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/v2/core.hpp>
#include <cloudevents/describe.hpp>
#include <cloudevents/extensions.hpp>
#include <cloudevents/v2/format/json_format.hpp>
#include <cloudevents/v2/format/typed_payload.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/result.hpp>

#include "mini_codec.hpp"

// The five structs are core and need no codec, but type recovery is only
// observable after a wire form has thrown the type away. So the recovery suites
// are function templates instantiated for BOTH in-tree codecs, as elsewhere.
//
// `expect(bool{a == b})` is not decoration: ut streams both operands on failure,
// and ce::v2::uri_ref, ce::v2::event and the extension structs have no operator<<.

namespace {

using namespace std::string_view_literals;

using nlohmann_codec = ce::v2::codec::nlohmann_codec;
using mini_codec = ce::test::mini_codec;

using namespace ce::v2::literals;

[[nodiscard]] auto minimal(ce::v2::event::options rest = {}) -> ce::v2::event {
  return ce::v2::event{"id-1"_id, "/spec/test"_source, "com.example.thing"_type, std::move(rest)};
}

struct payload {
  std::string label;
  std::int32_t count;
};

CE_DESCRIBE(payload, label, count);

// --- SWR-EXT-0001 -----------------------------------------------------------

const boost::ut::suite<"extensions-described-structs"> described_structs = [] {
  using namespace boost::ut;

  "all five extensions are described"_test = [] {
    static_assert(ce::v2::described<ce::v2::ext::tracing>);
    static_assert(ce::v2::described<ce::v2::ext::partitioning>);
    static_assert(ce::v2::described<ce::v2::ext::sampled_rate>);
    static_assert(ce::v2::described<ce::v2::ext::sequence>);
    static_assert(ce::v2::described<ce::v2::ext::dataref>);
    expect(true);
  };

  "the macro backend describes them"_test = [] {
    // Pinned so that enabling reflection cannot silently take them over and
    // change the wire names (ADR-0003).
    static_assert(ce::v2::backend_of<ce::v2::ext::tracing> == ce::v2::describe_backend::macro);
    static_assert(ce::v2::backend_of<ce::v2::ext::dataref> == ce::v2::describe_backend::macro);
    expect(true);
  };

  "the wire names are the ones the CloudEvents spec defines"_test = [] {
    constexpr auto tracing = ce::v2::field_names<ce::v2::ext::tracing>();
    static_assert(ce::v2::field_count<ce::v2::ext::tracing> == 2);
    expect(tracing[0] == "traceparent"sv);
    expect(tracing[1] == "tracestate"sv);

    constexpr auto partitioning = ce::v2::field_names<ce::v2::ext::partitioning>();
    static_assert(ce::v2::field_count<ce::v2::ext::partitioning> == 1);
    expect(partitioning[0] == "partitionkey"sv);

    constexpr auto rate = ce::v2::field_names<ce::v2::ext::sampled_rate>();
    static_assert(ce::v2::field_count<ce::v2::ext::sampled_rate> == 1);
    expect(rate[0] == "sampledrate"sv);

    // The struct member is `value`, because a member named `sequence` inside
    // `struct sequence` would hide the injected class name. The WIRE name is
    // what the spec fixes, and it is the thing asserted here.
    constexpr auto sequence = ce::v2::field_names<ce::v2::ext::sequence>();
    static_assert(ce::v2::field_count<ce::v2::ext::sequence> == 1);
    expect(sequence[0] == "sequence"sv);

    constexpr auto dataref = ce::v2::field_names<ce::v2::ext::dataref>();
    static_assert(ce::v2::field_count<ce::v2::ext::dataref> == 1);
    expect(dataref[0] == "dataref"sv);
  };

  "every wire name is a legal extension attribute name"_test = [] {
    // An extension whose name validate() rejects could be written and never
    // encoded, so the five names are checked against the same rule.
    for (const auto names :
         {ce::v2::field_names<ce::v2::ext::tracing>()[0], ce::v2::field_names<ce::v2::ext::tracing>()[1]}) {
      expect(ce::v2::valid_attribute_name(names)) << names;
      expect(!ce::v2::reserved_name(names)) << names;
    }
    expect(ce::v2::valid_attribute_name(ce::v2::field_names<ce::v2::ext::partitioning>()[0]));
    expect(ce::v2::valid_attribute_name(ce::v2::field_names<ce::v2::ext::sampled_rate>()[0]));
    expect(ce::v2::valid_attribute_name(ce::v2::field_names<ce::v2::ext::sequence>()[0]));
    expect(ce::v2::valid_attribute_name(ce::v2::field_names<ce::v2::ext::dataref>()[0]));
  };

  "the declared types are the CloudEvents attribute types"_test = [] {
    static_assert(std::is_same_v<decltype(ce::v2::ext::tracing::traceparent), std::string>);
    static_assert(
        std::is_same_v<decltype(ce::v2::ext::tracing::tracestate), std::optional<std::string>>);
    static_assert(std::is_same_v<decltype(ce::v2::ext::partitioning::partitionkey), std::string>);
    // Integer, not a string: the spec says sampledrate is an Integer, and the
    // declared type is what get<> restores it to.
    static_assert(std::is_same_v<decltype(ce::v2::ext::sampled_rate::sampledrate), std::int32_t>);
    static_assert(std::is_same_v<decltype(ce::v2::ext::sequence::value), std::string>);
    // URI-reference, not a string, for the same reason.
    static_assert(std::is_same_v<decltype(ce::v2::ext::dataref::value), ce::v2::uri_ref>);
    expect(true);
  };

  "sampledrate carries the one constraint the type system cannot"_test = [] {
    expect(ce::v2::ext::sampled_rate{.sampledrate = 1}.validate().has_value());
    expect(ce::v2::ext::sampled_rate{.sampledrate = 30}.validate().has_value());
    for (const std::int32_t bad : {0, -1, -30}) {
      auto checked = ce::v2::ext::sampled_rate{.sampledrate = bad}.validate();
      expect(!checked.has_value()) << bad;
      if (!checked) {
        expect(checked.error().code == ce::v2::errc::invalid_attribute_value) << bad;
        expect(checked.error().where == "sampledrate") << bad;
      }
    }
  };
};

// --- SWR-EXT-0002 -----------------------------------------------------------

const boost::ut::suite<"extensions-get-set-roundtrip"> get_set_roundtrip = [] {
  using namespace boost::ut;

  "each extension reads back what it wrote"_test = [] {
    ce::v2::event subject = minimal();

    const ce::v2::ext::tracing tracing{.traceparent = "00-0af7-00f0-01", .tracestate = "vendor=1"};
    expect(subject.set(tracing).has_value());
    auto read_tracing = subject.get<ce::v2::ext::tracing>();
    expect(read_tracing.has_value());
    if (read_tracing) {
      expect(bool{*read_tracing == tracing});
    }

    const ce::v2::ext::partitioning partitioning{.partitionkey = "customer-42"};
    expect(subject.set(partitioning).has_value());
    auto read_partitioning = subject.get<ce::v2::ext::partitioning>();
    expect(read_partitioning.has_value());
    if (read_partitioning) {
      expect(bool{*read_partitioning == partitioning});
    }

    const ce::v2::ext::sampled_rate rate{.sampledrate = 30};
    expect(subject.set(rate).has_value());
    auto read_rate = subject.get<ce::v2::ext::sampled_rate>();
    expect(read_rate.has_value());
    if (read_rate) {
      expect(bool{*read_rate == rate});
    }

    const ce::v2::ext::sequence sequence{.value = "0000000042"};
    expect(subject.set(sequence).has_value());
    auto read_sequence = subject.get<ce::v2::ext::sequence>();
    expect(read_sequence.has_value());
    if (read_sequence) {
      expect(bool{*read_sequence == sequence});
    }

    const ce::v2::ext::dataref dataref{.value = ce::v2::uri_ref{"https://example.test/blob/1"}};
    expect(subject.set(dataref).has_value());
    auto read_dataref = subject.get<ce::v2::ext::dataref>();
    expect(read_dataref.has_value());
    if (read_dataref) {
      expect(bool{*read_dataref == dataref});
    }

    // Five extensions, six attributes, and each one still reads back after the
    // others were written: set() must not disturb its neighbours.
    expect(subject.extensions().size() == 6_ul);
    expect(subject.get<ce::v2::ext::tracing>().has_value());
  };

  "set stores each field under its declared attribute type"_test = [] {
    // This is what makes type recovery possible at all: an Integer written as a
    // string would already have lost the type before any wire form saw it.
    ce::v2::event subject = minimal();
    expect(subject.set(ce::v2::ext::sampled_rate{.sampledrate = 30}).has_value());
    expect(subject.set(ce::v2::ext::dataref{.value = ce::v2::uri_ref{"/blob"}}).has_value());
    expect(subject.set(ce::v2::ext::partitioning{.partitionkey = "k"}).has_value());

    const auto* rate = subject.extension("sampledrate");
    expect(rate != nullptr);
    if (rate != nullptr) {
      expect(std::holds_alternative<std::int32_t>(*rate));
      expect(!std::holds_alternative<std::string>(*rate));
    }
    const auto* reference = subject.extension("dataref");
    expect(reference != nullptr);
    if (reference != nullptr) {
      expect(std::holds_alternative<ce::v2::uri_ref>(*reference));
      expect(!std::holds_alternative<std::string>(*reference));
    }
    const auto* key = subject.extension("partitionkey");
    expect(key != nullptr);
    if (key != nullptr) {
      expect(std::holds_alternative<std::string>(*key));
    }
  };

  "an absent optional field is nullopt, not an error"_test = [] {
    ce::v2::event subject = minimal();
    expect(subject.set(ce::v2::ext::tracing{.traceparent = "00-a-b-01", .tracestate = {}}).has_value());
    expect(subject.extension("tracestate") == nullptr);

    auto read = subject.get<ce::v2::ext::tracing>();
    expect(read.has_value());
    if (read) {
      expect(!read->tracestate.has_value());
      expect(read->traceparent == "00-a-b-01");
    }
  };

  "a nullopt optional removes an attribute already present"_test = [] {
    // Otherwise set() would be a merge, and the event would keep a tracestate
    // the caller had just cleared.
    ce::v2::event subject = minimal();
    expect(
        subject.set(ce::v2::ext::tracing{.traceparent = "00-a-b-01", .tracestate = "v=1"}).has_value());
    expect(subject.extension("tracestate") != nullptr);

    expect(subject.set(ce::v2::ext::tracing{.traceparent = "00-a-b-01", .tracestate = {}}).has_value());
    expect(subject.extension("tracestate") == nullptr);
  };

  "an absent required field is an error naming the attribute"_test = [] {
    ce::v2::event subject = minimal();
    auto read = subject.get<ce::v2::ext::tracing>();
    expect(!read.has_value());
    if (!read) {
      expect(read.error().code == ce::v2::errc::missing_required_attribute);
      expect(read.error().where == "traceparent");
    }

    // Present but incomplete is the same failure, and it still names the field
    // that is missing rather than the first one.
    subject.set_extension("tracestate"_ext, ce::v2::attribute_value{std::string{"v=1"}});
    auto partial = subject.get<ce::v2::ext::tracing>();
    expect(!partial.has_value());
    if (!partial) {
      expect(partial.error().code == ce::v2::errc::missing_required_attribute);
      expect(partial.error().where == "traceparent");
    }
  };

  "get reports the first failure and does not report a later one"_test = [] {
    ce::v2::event subject = minimal();
    auto read = subject.get<ce::v2::ext::partitioning>();
    expect(!read.has_value());
    if (!read) {
      expect(read.error().where == "partitionkey");
    }
  };
};

// --- SWR-EXT-0003 -----------------------------------------------------------

template<class C>
void check_json_type_recovery(std::string_view label) {
  using namespace boost::ut;
  using format = ce::v2::json_format<C>;

  ce::v2::event subject = minimal();
  const ce::v2::ext::tracing tracing{.traceparent = "00-0af7651916cd43dd-b7ad6b7169203331-01",
                                 .tracestate = "congo=t61rcWkgMzE"};
  expect(subject.set(tracing).has_value()) << label;
  expect(subject.set(ce::v2::ext::sampled_rate{.sampledrate = 30}).has_value()) << label;
  expect(subject.set(ce::v2::ext::dataref{.value = ce::v2::uri_ref{"https://example.test/b"}}).has_value())
      << label;

  auto encoded = format::encode(subject);
  expect(encoded.has_value()) << label;
  if (!encoded) {
    return;
  }
  auto decoded = format::decode(*encoded);
  expect(decoded.has_value()) << label;
  if (!decoded) {
    return;
  }

  // The acceptance criterion: the tracing extension survives with its types.
  auto read_tracing = decoded->template get<ce::v2::ext::tracing>();
  expect(read_tracing.has_value()) << label;
  if (read_tracing) {
    expect(bool{*read_tracing == tracing}) << label;
  }

  // sampledrate is an Integer, and JSON does carry that, so it arrives as one.
  const auto* stored_rate = decoded->extension("sampledrate");
  expect(stored_rate != nullptr) << label;
  if (stored_rate != nullptr) {
    expect(std::holds_alternative<std::int32_t>(*stored_rate)) << label;
  }
  auto read_rate = decoded->template get<ce::v2::ext::sampled_rate>();
  expect(read_rate.has_value()) << label;
  if (read_rate) {
    expect(read_rate->sampledrate == 30) << label;
  }

  // A URI-reference has no JSON spelling of its own, so it arrives as a string
  // and the declared field type is what restores it.
  const auto* stored_ref = decoded->extension("dataref");
  expect(stored_ref != nullptr) << label;
  if (stored_ref != nullptr) {
    expect(std::holds_alternative<std::string>(*stored_ref)) << label;
  }
  auto read_ref = decoded->template get<ce::v2::ext::dataref>();
  expect(read_ref.has_value()) << label;
  if (read_ref) {
    expect(read_ref->value.view() == "https://example.test/b") << label;
  }
}

template<class C>
void check_http_type_recovery(std::string_view label) {
  using namespace boost::ut;

  ce::v2::event subject = minimal();
  const ce::v2::ext::tracing tracing{.traceparent = "00-0af7651916cd43dd-b7ad6b7169203331-01",
                                 .tracestate = "congo=t61rcWkgMzE"};
  expect(subject.set(tracing).has_value()) << label;
  expect(subject.set(ce::v2::ext::sampled_rate{.sampledrate = 30}).has_value()) << label;

  auto message = ce::v2::http::to_message<C>(subject, ce::v2::content_mode::binary_mode);
  expect(message.has_value()) << label;
  if (!message) {
    return;
  }
  auto decoded = ce::v2::http::from_message<C>(*message);
  expect(decoded.has_value()) << label;
  if (!decoded) {
    return;
  }

  // The binary binding carries every attribute as a header, so EVERY extension
  // arrives as a string. That is exactly the loss the typed layer undoes.
  const auto* stored_rate = decoded->extension("sampledrate");
  expect(stored_rate != nullptr) << label;
  if (stored_rate != nullptr) {
    expect(std::holds_alternative<std::string>(*stored_rate)) << label;
    expect(!std::holds_alternative<std::int32_t>(*stored_rate)) << label;
  }

  auto read_tracing = decoded->template get<ce::v2::ext::tracing>();
  expect(read_tracing.has_value()) << label;
  if (read_tracing) {
    expect(bool{*read_tracing == tracing}) << label;
  }
  auto read_rate = decoded->template get<ce::v2::ext::sampled_rate>();
  expect(read_rate.has_value()) << label;
  if (read_rate) {
    expect(read_rate->sampledrate == 30) << label;
  }
}

template<class C>
void check_recovery_failures(std::string_view label) {
  using namespace boost::ut;

  // Every declared type that is not a string has a value it cannot accept.
  const auto reject = [&label](std::string_view stored, auto probe) {
    auto name = ce::v2::extension_name::make(probe.name);
    expect(name.has_value()) << label;
    if (!name) {
      return;
    }
    const ce::v2::event subject =
        minimal({.extensions = {{std::move(*name), ce::v2::attribute_value{std::string{stored}}}}});
    auto read = probe.read(subject);
    expect(!read.has_value()) << label << ": should reject " << stored;
    if (!read) {
      expect(read.error().code == ce::v2::errc::type_mismatch) << label << ": " << stored;
      expect(read.error().where == probe.name) << label << ": " << stored;
    }
  };

  struct rate_probe {
    std::string_view name = "sampledrate";
    static auto read(const ce::v2::event& e) { return e.get<ce::v2::ext::sampled_rate>(); }
  };

  for (const auto bad :
       {"not-a-number"sv, ""sv, "30x"sv, "3.5"sv, "0x1e"sv, " 30"sv, "99999999999999999999"sv}) {
    reject(bad, rate_probe{});
  }

  // A value that does fit is still accepted, so the rejection is about the text.
  const ce::v2::event subject =
      minimal({.extensions = {{"sampledrate"_ext, ce::v2::attribute_value{std::string{"30"}}}}});
  auto read = subject.get<ce::v2::ext::sampled_rate>();
  expect(read.has_value()) << label;
  if (read) {
    expect(read->sampledrate == 30) << label;
  }

  // An attribute holding an unrelated type is a mismatch rather than a silent
  // conversion: a Boolean is not an Integer even though both are scalars.
  const ce::v2::event odd = minimal({.extensions = {{"sampledrate"_ext, ce::v2::attribute_value{true}}}});
  auto mismatched = odd.get<ce::v2::ext::sampled_rate>();
  expect(!mismatched.has_value()) << label;
  if (!mismatched) {
    expect(mismatched.error().code == ce::v2::errc::type_mismatch) << label;
  }
}

const boost::ut::suite<"extensions-type-recovery"> type_recovery = [] {
  using namespace boost::ut;

  "json nlohmann_codec"_test = [] { check_json_type_recovery<nlohmann_codec>("nlohmann_codec"); };
  "json mini_codec"_test = [] { check_json_type_recovery<mini_codec>("mini_codec"); };
  "http nlohmann_codec"_test = [] { check_http_type_recovery<nlohmann_codec>("nlohmann_codec"); };
  "http mini_codec"_test = [] { check_http_type_recovery<mini_codec>("mini_codec"); };
  "failures nlohmann_codec"_test = [] {
    check_recovery_failures<nlohmann_codec>("nlohmann_codec");
  };
  "failures mini_codec"_test = [] { check_recovery_failures<mini_codec>("mini_codec"); };
};

// --- SWR-EXT-0004 -----------------------------------------------------------

template<class C>
void check_event_of(std::string_view label) {
  using namespace boost::ut;
  using view = ce::v2::event_of<payload, C>;

  static_assert(std::is_same_v<typename view::payload_type, payload>);
  static_assert(std::is_same_v<typename view::codec_type, C>);

  const payload sent{.label = "crate", .count = 12};

  // with_data returns the view itself: writing a described payload cannot fail,
  // so there is no result to unwrap.
  auto built = view::with_data(minimal(), sent);

  auto read = built.data();
  expect(read.has_value()) << label;
  if (read) {
    expect(read->label == sent.label) << label;
    expect(read->count == sent.count) << label;
  }

  // A view, not a container: the event is handed back whole, with the context
  // attributes the caller supplied still on it.
  expect(built.underlying().id() == "id-1") << label;
  expect(built.underlying().type() == "com.example.thing") << label;

  // Writing through the view updates the event it holds.
  const payload replaced{.label = "pallet", .count = 3};
  built.set_data(replaced);
  auto again = built.data();
  expect(again.has_value()) << label;
  if (again) {
    expect(again->label == "pallet") << label;
    expect(again->count == 3) << label;
  }

  // A view over an event with no payload reports it rather than inventing one.
  const view empty{minimal()};
  auto missing = empty.data();
  expect(!missing.has_value()) << label;
  if (!missing) {
    expect(missing.error().code == ce::v2::errc::missing_required_attribute) << label;
  }
}

const boost::ut::suite<"event-of-typed-view"> event_of_view = [] {
  using namespace boost::ut;

  "nlohmann_codec"_test = [] { check_event_of<nlohmann_codec>("nlohmann_codec"); };
  "mini_codec"_test = [] { check_event_of<mini_codec>("mini_codec"); };
};

}  // namespace

int main() {}
