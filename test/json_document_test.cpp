#include <boost/ut.hpp>

#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/format/json_codec.hpp>

#include <atomic>
#include <cstddef>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "mini_codec.hpp"
#include "no_nlohmann_probe.hpp"

namespace {

using namespace std::string_view_literals;

using nlohmann_codec = ce::codec::nlohmann_codec;
using mini_codec = ce::test::mini_codec;

struct renamed_mini_codec : mini_codec {
  static constexpr std::string_view identity = "io.cloudevents.cpp.test.renamed";
};
static_assert(ce::json::json_codec<renamed_mini_codec>);
static_assert(std::is_same_v<renamed_mini_codec::value, mini_codec::value>);

template <class Codec>
auto document_of(std::string_view text) -> ce::json_document {
  auto parsed = Codec::parse(text);
  boost::ut::expect(parsed.has_value()) << "parse " << text;
  return ce::json_document::make<Codec>(parsed ? std::move(*parsed) : Codec::make_null());
}

// spec: SWR-CORE-0031
const boost::ut::suite<"json-document-holds-any-codec"> holds_any_codec = [] {
  using namespace boost::ut;

  "a document hands back the DOM its codec built"_test = [] {
    const auto from_nlohmann = document_of<nlohmann_codec>(R"({"a":1,"b":["x"]})"sv);
    const auto* nlohmann_value = from_nlohmann.get<nlohmann_codec>();
    expect(nlohmann_value != nullptr);
    if (nlohmann_value != nullptr) {
      expect(nlohmann_codec::dump(*nlohmann_value) == R"({"a":1,"b":["x"]})"sv);
    }

    const auto from_mini = document_of<mini_codec>(R"({"a":1,"b":["x"]})"sv);
    const auto* mini_value = from_mini.get<mini_codec>();
    expect(mini_value != nullptr);
    if (mini_value != nullptr) {
      const auto* member = mini_codec::find(*mini_value, "a");
      expect(member != nullptr);
      if (member != nullptr) {
        const auto held = mini_codec::as_int(*member);
        expect(held.has_value() && *held == 1);
      }
    }
  };

  "dump produces text its codec parses back to the same value"_test = [] {
    constexpr auto text = R"({"a":1,"b":[true,null,"x"],"c":{"d":-2}})"sv;
    const auto from_nlohmann = document_of<nlohmann_codec>(text);
    const auto nlohmann_again = nlohmann_codec::parse(from_nlohmann.dump());
    expect(nlohmann_again.has_value() &&
           nlohmann_codec::equal(*nlohmann_again, *from_nlohmann.get<nlohmann_codec>()));

    const auto from_mini = document_of<mini_codec>(text);
    const auto mini_again = mini_codec::parse(from_mini.dump());
    expect(mini_again.has_value() &&
           mini_codec::equal(*mini_again, *from_mini.get<mini_codec>()));
  };

  "a document is never empty"_test = [] {
    static_assert(!std::is_default_constructible_v<ce::json_document>);
    static_assert(std::is_copy_constructible_v<ce::json_document>);
    static_assert(std::is_copy_assignable_v<ce::json_document>);

    auto source = document_of<nlohmann_codec>(R"({"a":1})"sv);
    const auto taken = std::move(source);
    // NOLINTNEXTLINE(bugprone-use-after-move,hicpp-invalid-access-moved)
    expect(source.dump() == taken.dump()) << "moving a document copies it";
    expect(source.get<nlohmann_codec>() != nullptr);
  };

  "the core header builds a document over a codec it has never seen"_test = [] {
    const auto observed = ce_no_nlohmann::probe();
    expect(!observed.nlohmann_macro_defined);
    expect(observed.json_document_round_tripped)
        << "json_document over mini_codec, in a translation unit without nlohmann";
  };
};

// spec: SWR-CORE-0032
const boost::ut::suite<"json-document-shared-across-threads"> shared_across_threads = [] {
  using namespace boost::ut;

  "threads copy, compare and read one document concurrently"_test = [] {
    constexpr std::size_t thread_count = 8;
    constexpr int iterations = 2000;
    const auto shared = document_of<nlohmann_codec>(R"({"a":1,"b":[1,2,3],"c":{"d":"e"}})"sv);
    const auto same_from_mini = document_of<mini_codec>(R"({"c":{"d":"e"},"b":[1,2,3],"a":1})"sv);
    const auto expected_text = shared.dump();

    std::atomic<int> failures{0};
    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    for (std::size_t worker = 0; worker < thread_count; ++worker) {
      workers.emplace_back([&shared, &same_from_mini, &expected_text, &failures] {
        auto reassigned = same_from_mini;
        for (int round = 0; round < iterations; ++round) {
          const auto copy = shared;
          reassigned = copy;
          const bool agrees = copy == shared && reassigned == shared &&
                              copy.get<nlohmann_codec>() == shared.get<nlohmann_codec>() &&
                              copy.dump() == expected_text && shared == same_from_mini;
          if (!agrees) {
            failures.fetch_add(1, std::memory_order_relaxed);
          }
        }
      });
    }
    for (auto& worker : workers) {
      worker.join();
    }

    expect(failures.load() == 0) << failures.load() << " reads disagreed";
    expect(shared.dump() == expected_text) << "the shared document is unchanged";
  };
};

// spec: SWR-CORE-0033
const boost::ut::suite<"json-document-compares-as-json"> compares_as_json = [] {
  using namespace boost::ut;

  "one codec: member order and whitespace do not count, values do"_test = [] {
    const auto document = document_of<nlohmann_codec>(R"({"a":1,"b":[1,2]})"sv);
    const auto reordered = document_of<nlohmann_codec>(R"( { "b" : [ 1, 2 ], "a" : 1 } )"sv);
    const auto different = document_of<nlohmann_codec>(R"({"a":2,"b":[1,2]})"sv);
    expect(document == reordered);
    expect(!(document == different));
    expect(document != different);

    const auto mini_document = document_of<mini_codec>(R"({"a":1,"b":[1,2]})"sv);
    const auto mini_reordered = document_of<mini_codec>(R"({"b":[1,2],"a":1})"sv);
    const auto mini_different = document_of<mini_codec>(R"({"a":1,"b":[2,1]})"sv);
    expect(mini_document == mini_reordered);
    expect(mini_document != mini_different);
  };

  "two codecs: the same JSON is equal from either side"_test = [] {
    const auto from_nlohmann = document_of<nlohmann_codec>(R"({"a":"x","b":[1,2]})"sv);
    const auto from_mini = document_of<mini_codec>(R"({"b":[1,2],"a":"x"})"sv);
    expect(from_nlohmann == from_mini);
    expect(from_mini == from_nlohmann);
  };

  "two codecs: different JSON is unequal from either side"_test = [] {
    const auto from_nlohmann = document_of<nlohmann_codec>(R"({"a":"x","b":[1,2]})"sv);
    const auto from_mini = document_of<mini_codec>(R"({"b":[1,2],"a":"y"})"sv);
    expect(from_nlohmann != from_mini);
    expect(from_mini != from_nlohmann);
  };

  "a copy equals its source"_test = [] {
    const auto source = document_of<mini_codec>(R"({"a":[{"b":null}]})"sv);
    const auto copy = source;
    expect(copy == source);
    expect(source == copy);
  };
};

// spec: SWR-CORE-0034
const boost::ut::suite<"json-document-yields-only-to-its-codec"> yields_only_to_its_codec = [] {
  using namespace boost::ut;

  "another codec gets no DOM"_test = [] {
    const auto from_nlohmann = document_of<nlohmann_codec>(R"({"a":1})"sv);
    const auto from_mini = document_of<mini_codec>(R"({"a":1})"sv);

    expect(from_nlohmann.get<mini_codec>() == nullptr);
    expect(from_mini.get<nlohmann_codec>() == nullptr);
    expect(from_nlohmann.get<nlohmann_codec>() != nullptr);
    expect(from_mini.get<mini_codec>() != nullptr);
  };

  "built_by names the codec that built the document"_test = [] {
    const auto from_nlohmann = document_of<nlohmann_codec>(R"([1])"sv);
    const auto from_mini = document_of<mini_codec>(R"([1])"sv);

    expect(from_nlohmann.built_by<nlohmann_codec>());
    expect(!from_nlohmann.built_by<mini_codec>());
    expect(from_mini.built_by<mini_codec>());
    expect(!from_mini.built_by<nlohmann_codec>());
  };

  "a codec of the same shape with another identity gets no DOM"_test = [] {
    const auto from_mini = document_of<mini_codec>(R"({"a":1})"sv);
    const auto from_renamed = document_of<renamed_mini_codec>(R"({"a":1})"sv);

    expect(from_mini.get<renamed_mini_codec>() == nullptr);
    expect(from_renamed.get<mini_codec>() == nullptr);
    expect(!from_mini.built_by<renamed_mini_codec>());
    expect(!from_renamed.built_by<mini_codec>());
    expect(from_renamed.get<renamed_mini_codec>() != nullptr);
    expect(from_mini == from_renamed) << "equal JSON still compares equal, through the text";
  };
};

}  // namespace

int main() {}
