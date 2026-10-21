#include <optional>
#include <string>
#include <utility>

#include <boost/ut.hpp>

#include <cloudevents/binding/http.hpp>
#include <cloudevents/binding/kafka.hpp>
#include <cloudevents/binding/nats.hpp>
#include <cloudevents/core.hpp>
#include <cloudevents/message.hpp>

#include "mini_codec.hpp"

// The moving entry points are in ce::v4 (CR-0005). The names are looked up the way
// test/v3/v3_moving_absent_test.cpp looks them up in the frozen generations, so that test
// is shown to be able to see a name that is there.

namespace {

using mini_codec = ce::test::mini_codec;

namespace v4_http {
using namespace ce::v4::http;
template<class M>
constexpr bool single =
    requires(M&& given) { from_message_moving<mini_codec>(std::forward<M>(given)); };
template<class M>
constexpr bool batch =
    requires(M&& given) { from_batch_message_moving<mini_codec>(std::forward<M>(given)); };
}  // namespace v4_http

namespace v4_kafka {
using namespace ce::v4::kafka;
template<class M>
constexpr bool single =
    requires(M&& given) { from_message_moving<mini_codec>(std::forward<M>(given)); };
}  // namespace v4_kafka

namespace v4_nats {
using namespace ce::v4::nats;
template<class M>
constexpr bool single =
    requires(M&& given) { from_message_moving<mini_codec>(std::forward<M>(given)); };
template<class S>
constexpr bool payload =
    requires(S&& given) { from_payload_moving<mini_codec>(std::forward<S>(given)); };
}  // namespace v4_nats

namespace v4_binding {
using namespace ce::v4::binding;
template<class B>
constexpr bool reader =
    requires(B&& body) { read_body_moving<mini_codec>(std::forward<B>(body), std::nullopt); };
}  // namespace v4_binding

}  // namespace

// spec: SWR-BIND-0014
const boost::ut::suite<"moving-entry-points-exist-in-v4"> moving_entry_points_exist = [] {
  using namespace boost::ut;

  "ce::v4 has every moving entry point"_test = [] {
    static_assert(v4_http::single<ce::message>);
    static_assert(v4_http::batch<ce::message>);
    static_assert(v4_kafka::single<ce::message>);
    static_assert(v4_nats::single<ce::message>);
    static_assert(v4_nats::payload<std::string>);
    static_assert(v4_binding::reader<ce::binary>);
  };
};

int main() {}
