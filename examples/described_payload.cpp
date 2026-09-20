/// \file
/// \brief Carry your own struct as the event payload, with no mapping code.

#include <cloudevents/codec/nlohmann.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/describe.hpp>
#include <cloudevents/format/json_format.hpp>
#include <cloudevents/format/typed_payload.hpp>

#include <cstdint>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <vector>

using codec = ce::codec::nlohmann_codec;

namespace shop {

struct line_item {
  std::string sku;
  std::int32_t quantity;
};

struct order {
  std::string reference;
  std::int32_t total_cents;
  bool paid;
  std::optional<std::string> note;
  std::vector<std::string> tags;
  std::map<std::string, std::string> labels;
};

// One line per type. CE_DESCRIBE goes in the type's own namespace, because it
// defines a function found by ADL.
//
// CE_FIELD gives a member a wire name that differs from its identifier, which
// is how a C++ name and a JSON name stay independent.
CE_DESCRIBE(line_item, sku, quantity);
CE_DESCRIBE(order, CE_FIELD(reference, "ref"), CE_FIELD(total_cents, "total"), paid, note, tags,
            labels);

}  // namespace shop

int main() {
  // The describe seam reports what it cannot map at compile time, naming the
  // struct, rather than failing at run time on one unlucky field.
  static_assert(ce::described<shop::order>);
  static_assert(ce::members_supported<shop::order>());
  static_assert(ce::field_count<shop::order> == 6);

  const shop::order placed{
      .reference = "ORD-99",
      .total_cents = 4200,
      .paid = true,
      .note = "leave with the neighbour",
      .tags = {"priority", "fragile"},
      .labels = {{"warehouse", "oslo"}, {"lane", "3"}},
  };

  ce::event subject{
      .id = "A234-1234-1234",
      .source = ce::uri_ref{"https://example.test/orders"},
      .type = "com.example.order.placed",
  };

  // set_data writes the struct as the payload and sets datacontenttype, so the
  // event states what it carries.
  if (auto stored = ce::set_data<shop::order, codec>(subject, placed); !stored) {
    std::fprintf(stderr, "set_data: %s\n", stored.error().detail.c_str());
    return 1;
  }

  auto encoded = ce::json_format<codec>::encode(subject);
  if (!encoded) {
    std::fprintf(stderr, "encode: %s\n", encoded.error().detail.c_str());
    return 1;
  }
  std::printf("on the wire:\n%s\n\n", encoded->c_str());

  // A receiver names the type it expects and gets it, or a typed error.
  auto received = ce::json_format<codec>::decode(*encoded);
  if (!received) {
    std::fprintf(stderr, "decode: %s\n", received.error().detail.c_str());
    return 1;
  }

  auto payload = ce::data_as<shop::order, codec>(*received);
  if (!payload) {
    std::fprintf(stderr, "data_as: %s at %s\n", payload.error().detail.c_str(),
                 payload.error().where.c_str());
    return 1;
  }
  std::printf("ref=%s total=%d paid=%s tags=%zu note=%s\n", payload->reference.c_str(),
              payload->total_cents, payload->paid ? "yes" : "no", payload->tags.size(),
              payload->note.value_or("(none)").c_str());

  // event_of puts the payload type in the event's type, so a producer and a
  // consumer share a compile-time contract instead of a documented convention.
  auto view = ce::event_of<shop::line_item, codec>::with_data(
      subject, shop::line_item{.sku = "SKU-1", .quantity = 2});
  if (!view) {
    std::fprintf(stderr, "event_of: %s\n", view.error().detail.c_str());
    return 1;
  }
  auto item = view->data();
  if (!item) {
    std::fprintf(stderr, "event_of::data: %s\n", item.error().detail.c_str());
    return 1;
  }
  std::printf("line item: %s x%d\n", item->sku.c_str(), item->quantity);

  // Decoding is lenient about ABSENT members: they keep their default, so a
  // struct that grows a field still reads older documents. The consequence is
  // that reading an order as a line item SUCCEEDS with empty fields, because an
  // order carries neither sku nor quantity.
  //
  // The payload type is not self-describing. `type` is what says which payload
  // an event carries, and a consumer switches on that.
  auto as_item = ce::data_as<shop::line_item, codec>(*received);
  if (!as_item) {
    std::fprintf(stderr, "expected a lenient read, got: %s\n", as_item.error().detail.c_str());
    return 1;
  }
  std::printf("an order read as a line item: sku='%s' quantity=%d (both defaulted)\n",
              as_item->sku.c_str(), as_item->quantity);

  // Decoding is strict about a member that IS present with the wrong type, and
  // the failure names the member rather than the payload.
  ce::event mistyped{
      .id = "B1",
      .source = ce::uri_ref{"/s"},
      .type = "com.example.order.line",
  };
  mistyped.datacontenttype = "application/json";
  mistyped.data = ce::json_text{.raw = R"({"sku":"SKU-1","quantity":"two"})"};

  auto rejected = ce::data_as<shop::line_item, codec>(mistyped);
  if (rejected) {
    std::fprintf(stderr, "a string quantity should not decode as int32\n");
    return 1;
  }
  std::printf("a string where an integer was declared fails at '%s': %s\n",
              rejected.error().where.c_str(), rejected.error().detail.c_str());
  return 0;
}
