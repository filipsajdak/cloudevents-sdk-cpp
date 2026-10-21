#include <optional>
#include <string>
#include <utility>

#include <boost/ut.hpp>

#include <cloudevents/core.hpp>
#include <cloudevents/message.hpp>
#include <cloudevents/v1/binding/http.hpp>
#include <cloudevents/v1/binding/kafka.hpp>
#include <cloudevents/v1/binding/nats.hpp>
#include <cloudevents/v2/binding/http.hpp>
#include <cloudevents/v2/binding/kafka.hpp>
#include <cloudevents/v2/binding/nats.hpp>
#include <cloudevents/v3/binding/http.hpp>
#include <cloudevents/v3/binding/kafka.hpp>
#include <cloudevents/v3/binding/nats.hpp>

// The moving entry points are an addition to ce::v4 only (CR-0005 ruling 13). A generation's
// namespace is reached by a using-directive and the name is then looked up unqualified with a
// dependent argument, so a name a generation does not declare makes the requirement false instead
// of a hard error. If a moving entry point were ever added to a frozen generation, the line that
// asserts its absence would fail. The codec argument is `void`: nothing is instantiated.

namespace {

namespace v1_http {
using namespace ce::v1::http;
template<class M>
constexpr bool single = requires(M&& given) { from_message_moving<void>(std::forward<M>(given)); };
template<class M>
constexpr bool batch =
    requires(M&& given) { from_batch_message_moving<void>(std::forward<M>(given)); };
}  // namespace v1_http

namespace v1_kafka {
using namespace ce::v1::kafka;
template<class M>
constexpr bool single = requires(M&& given) { from_message_moving<void>(std::forward<M>(given)); };
}  // namespace v1_kafka

namespace v1_nats {
using namespace ce::v1::nats;
template<class M>
constexpr bool single = requires(M&& given) { from_message_moving<void>(std::forward<M>(given)); };
template<class S>
constexpr bool payload = requires(S&& given) { from_payload_moving<void>(std::forward<S>(given)); };
}  // namespace v1_nats

namespace v2_http {
using namespace ce::v2::http;
template<class M>
constexpr bool single = requires(M&& given) { from_message_moving<void>(std::forward<M>(given)); };
template<class M>
constexpr bool batch =
    requires(M&& given) { from_batch_message_moving<void>(std::forward<M>(given)); };
}  // namespace v2_http

namespace v2_kafka {
using namespace ce::v2::kafka;
template<class M>
constexpr bool single = requires(M&& given) { from_message_moving<void>(std::forward<M>(given)); };
}  // namespace v2_kafka

namespace v2_nats {
using namespace ce::v2::nats;
template<class M>
constexpr bool single = requires(M&& given) { from_message_moving<void>(std::forward<M>(given)); };
template<class S>
constexpr bool payload = requires(S&& given) { from_payload_moving<void>(std::forward<S>(given)); };
}  // namespace v2_nats

namespace v3_http {
using namespace ce::v3::http;
template<class M>
constexpr bool single = requires(M&& given) { from_message_moving<void>(std::forward<M>(given)); };
template<class M>
constexpr bool batch =
    requires(M&& given) { from_batch_message_moving<void>(std::forward<M>(given)); };
}  // namespace v3_http

namespace v3_kafka {
using namespace ce::v3::kafka;
template<class M>
constexpr bool single = requires(M&& given) { from_message_moving<void>(std::forward<M>(given)); };
}  // namespace v3_kafka

namespace v3_nats {
using namespace ce::v3::nats;
template<class M>
constexpr bool single = requires(M&& given) { from_message_moving<void>(std::forward<M>(given)); };
template<class S>
constexpr bool payload = requires(S&& given) { from_payload_moving<void>(std::forward<S>(given)); };
}  // namespace v3_nats

namespace v3_binding {
using namespace ce::v3::binding;
template<class B>
constexpr bool reader =
    requires(B&& body) { read_body_moving<void>(std::forward<B>(body), std::nullopt); };
}  // namespace v3_binding

}  // namespace

// spec: SWR-BIND-0014
const boost::ut::suite<"moving-entry-points-are-absent-from-the-frozen-generations">
    moving_entry_points_are_absent = [] {
      using namespace boost::ut;

      "ce::v3 has none of them"_test = [] {
        static_assert(!v3_http::single<ce::message>);
        static_assert(!v3_http::batch<ce::message>);
        static_assert(!v3_kafka::single<ce::message>);
        static_assert(!v3_nats::single<ce::message>);
        static_assert(!v3_nats::payload<std::string>);
        static_assert(!v3_binding::reader<ce::binary>);
      };

      "ce::v2 and ce::v1 have none of them"_test = [] {
        static_assert(!v2_http::single<ce::message>);
        static_assert(!v2_http::batch<ce::message>);
        static_assert(!v2_kafka::single<ce::message>);
        static_assert(!v2_nats::single<ce::message>);
        static_assert(!v2_nats::payload<std::string>);
        static_assert(!v1_http::single<ce::message>);
        static_assert(!v1_http::batch<ce::message>);
        static_assert(!v1_kafka::single<ce::message>);
        static_assert(!v1_nats::single<ce::message>);
        static_assert(!v1_nats::payload<std::string>);
      };
    };

int main() {}
