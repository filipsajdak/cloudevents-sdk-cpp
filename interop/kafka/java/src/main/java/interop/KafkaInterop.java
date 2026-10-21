package interop;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import io.cloudevents.CloudEvent;
import io.cloudevents.core.builder.CloudEventBuilder;
import io.cloudevents.core.data.BytesCloudEventData;
import io.cloudevents.jackson.JsonFormat;
import io.cloudevents.kafka.KafkaMessageFactory;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.OffsetDateTime;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import org.apache.kafka.clients.consumer.ConsumerRecord;
import org.apache.kafka.clients.producer.ProducerRecord;
import org.apache.kafka.common.header.Header;

/**
 * Writes the Kafka record goldens the Java SDK produces and checks the records the C++ SDK
 * produced (SWR-SEC-0008). No broker is involved: the Kafka module converts an event to and from
 * an in-memory record, which is the key, the headers and the value a broker would carry.
 *
 * <p>usage: KafkaInterop output-directory directory-of-cpp-records
 */
public final class KafkaInterop {

  private static final ObjectMapper MAPPER = new ObjectMapper();
  private static final String TOPIC = "interop";
  private static final String[] MODES = {"binary", "structured"};
  private static final String PARTITION_KEY = "partitionkey";

  /** One event of the corpus, and the key its record is written with. */
  private record Entry(String name, CloudEvent event, String key) {}

  private static Entry entry(String name, CloudEvent event) {
    return new Entry(name, event, null);
  }

  /**
   * The event set the HTTP interop uses (interop/java), with the producer's name in every source.
   * The Java SDK never derives a record key from an event, so a key is the caller's to pass: the
   * "extensions" record is written with its partitionkey as the key, as an application that
   * partitions by that attribute would, and "extensions_unkeyed" is the same event written without
   * one, which is what the SDK does when it is not told.
   */
  private static List<Entry> corpus(String tag) {
    String source = "/interop/" + tag;
    // interop/java/unicode.json spells the non-ASCII source percent-encoded; the other producers
    // write the characters. Kept as it is so this record agrees with that golden.
    String unicodeSource = tag.equals("java")
        ? source + "/%C3%A9v%C3%A9nement"
        : source + "/événement";
    List<Entry> out = new ArrayList<>();

    out.add(entry("minimal",
        CloudEventBuilder.v1().withId("id-minimal").withSource(URI.create(source))
            .withType("com.example.minimal").build()));

    out.add(entry("full",
        CloudEventBuilder.v1()
            .withId("id-full")
            .withSource(URI.create("https://example.test/interop/" + tag))
            .withType("com.example.full")
            .withSubject("the-subject")
            .withTime(OffsetDateTime.parse("2026-09-20T12:34:56Z"))
            .withDataSchema(URI.create("https://example.test/schema/1"))
            .withDataContentType("application/json")
            .withData(("{\"count\":3,\"label\":\"" + tag + "\"}").getBytes(StandardCharsets.UTF_8))
            .build()));

    CloudEvent extended = CloudEventBuilder.v1()
        .withId("id-extensions")
        .withSource(URI.create(source))
        .withType("com.example.extensions")
        .withExtension("traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01")
        .withExtension("sampledrate", 30)
        .withExtension(PARTITION_KEY, "customer-42")
        .build();
    out.add(new Entry("extensions", extended, "customer-42"));
    out.add(entry("extensions_unkeyed", extended));

    out.add(entry("text_data",
        CloudEventBuilder.v1().withId("id-text").withSource(URI.create(source))
            .withType("com.example.text").withDataContentType("text/plain")
            .withData("plain text payload".getBytes(StandardCharsets.UTF_8)).build()));

    out.add(entry("binary_data",
        CloudEventBuilder.v1().withId("id-binary").withSource(URI.create(source))
            .withType("com.example.binary").withDataContentType("application/octet-stream")
            .withData(BytesCloudEventData.wrap(new byte[] {0x00, 0x01, 0x02, (byte) 0xFF}))
            .build()));

    out.add(entry("time_nanoseconds",
        CloudEventBuilder.v1().withId("id-nanos").withSource(URI.create(source))
            .withType("com.example.nanos")
            .withTime(OffsetDateTime.parse("2026-09-20T12:34:56.123456789+02:00")).build()));

    out.add(entry("extension_types",
        CloudEventBuilder.v1().withId("id-types").withSource(URI.create(source))
            .withType("com.example.types")
            .withExtension("astring", "text")
            .withExtension("aninteger", 42)
            .withExtension("aboolean", true)
            .withExtension("negative", -7)
            .withExtension("zero", 0)
            .build()));

    out.add(entry("unicode",
        CloudEventBuilder.v1()
            .withId("id-unicode-é文")
            .withSource(URI.create(unicodeSource))
            .withType("com.example.unicode")
            .withSubject("日本語 😀")
            .withDataContentType("application/json")
            .withData("{\"text\":\"é文 😀\"}".getBytes(StandardCharsets.UTF_8))
            .build()));

    out.add(entry("percent_in_subject",
        CloudEventBuilder.v1().withId("id-percent").withSource(URI.create(source))
            .withType("com.example.percent")
            .withSubject("100% of 50%OFF")
            .withExtension("pct", "a%20b")
            .build()));

    out.add(entry("data_array",
        CloudEventBuilder.v1().withId("id-scalar").withSource(URI.create(source))
            .withType("com.example.scalar").withDataContentType("application/json")
            .withData("[1,2,3]".getBytes(StandardCharsets.UTF_8)).build()));

    out.add(entry("minimal_relative",
        CloudEventBuilder.v1().withId("id-relative").withSource(URI.create("/"))
            .withType("t").build()));

    return out;
  }

  private static String encode(byte[] bytes) {
    return Base64.getEncoder().encodeToString(bytes);
  }

  /** Writes e as the Java SDK's Kafka binding does, in the given mode. */
  private static ProducerRecord<String, byte[]> produce(Entry entry, String mode) {
    var writer = KafkaMessageFactory.createWriter(TOPIC, null, null, entry.key());
    return mode.equals("structured")
        ? writer.writeStructured(entry.event(), JsonFormat.CONTENT_TYPE)
        : writer.writeBinary(entry.event());
  }

  private static void write(Path dir, String name, String mode, ProducerRecord<String, byte[]> r)
      throws Exception {
    ObjectNode document = MAPPER.createObjectNode();
    if (r.key() == null) {
      document.putNull("key");
    } else {
      document.put("key", encode(r.key().getBytes(StandardCharsets.UTF_8)));
    }
    // The SDK writes extension headers in the order of a hash set, so a regeneration could
    // reorder them. The order of record headers carries no meaning to CloudEvents.
    List<Header> headers = new ArrayList<>();
    r.headers().forEach(headers::add);
    headers.sort(Comparator.comparing(Header::key));
    ArrayNode list = document.putArray("headers");
    for (Header header : headers) {
      list.addArray().add(header.key()).add(encode(header.value()));
    }
    if (r.value() == null) {
      document.putNull("value");
    } else {
      document.put("value", encode(r.value()));
    }
    Files.writeString(dir.resolve(name + "." + mode + ".json"),
        MAPPER.writerWithDefaultPrettyPrinter().writeValueAsString(document) + "\n");
  }

  /** Reads a record as a Java consumer would, from the bytes alone. */
  private static CloudEvent consume(JsonNode wire) {
    String key = wire.get("key").isNull()
        ? null
        : new String(Base64.getDecoder().decode(wire.get("key").asText()), StandardCharsets.UTF_8);
    byte[] value = wire.get("value").isNull()
        ? null
        : Base64.getDecoder().decode(wire.get("value").asText());
    ConsumerRecord<String, byte[]> incoming = new ConsumerRecord<>(TOPIC, 0, 0L, key, value);
    for (JsonNode header : wire.get("headers")) {
      incoming.headers().add(header.get(0).asText(),
          Base64.getDecoder().decode(header.get(1).asText()));
    }
    return KafkaMessageFactory.createReader(incoming).toEvent();
  }

  private static boolean isJson(String contentType) {
    return contentType == null || contentType.startsWith("application/json")
        || contentType.endsWith("+json");
  }

  /** A JSON payload as a document, because the format may re-serialise it; anything else exactly. */
  private static void sameData(CloudEvent want, CloudEvent got) throws Exception {
    byte[] left = want.getData() == null ? new byte[0] : want.getData().toBytes();
    byte[] right = got.getData() == null ? new byte[0] : got.getData().toBytes();
    if (isJson(want.getDataContentType()) && left.length > 0 && right.length > 0) {
      if (!MAPPER.readTree(left).equals(MAPPER.readTree(right))) {
        throw new IllegalStateException(
            "data " + new String(left, StandardCharsets.UTF_8) + " != "
                + new String(right, StandardCharsets.UTF_8));
      }
      return;
    }
    if (!java.util.Arrays.equals(left, right)) {
      throw new IllegalStateException("data " + encode(left) + " != " + encode(right));
    }
  }

  private static Map<String, String> extensions(CloudEvent e) {
    Map<String, String> out = new TreeMap<>();
    for (String name : e.getExtensionNames()) {
      out.put(name, String.valueOf(e.getExtension(name)));
    }
    return out;
  }

  /** Every attribute and every extension; extensions by text, as binary mode carries strings. */
  private static void sameEvent(CloudEvent want, CloudEvent got) throws Exception {
    check("specversion", want.getSpecVersion(), got.getSpecVersion());
    check("id", want.getId(), got.getId());
    check("source", want.getSource(), got.getSource());
    check("type", want.getType(), got.getType());
    check("subject", want.getSubject(), got.getSubject());
    check("datacontenttype", want.getDataContentType(), got.getDataContentType());
    check("dataschema", want.getDataSchema(), got.getDataSchema());
    check("time", want.getTime(), got.getTime());
    check("extensions", extensions(want), extensions(got));
    sameData(want, got);
  }

  private static void check(String what, Object want, Object got) {
    if (!java.util.Objects.equals(want, got)) {
      throw new IllegalStateException(what + ": want " + want + ", got " + got);
    }
  }

  /** The record key this SDK's partitionkey mapper chooses. */
  private static String keyOf(CloudEvent e) {
    Object key = e.getExtension(PARTITION_KEY);
    return key == null ? null : key.toString();
  }

  private static int verify(Path dir) throws Exception {
    int failures = 0;
    int checked = 0;
    for (Entry entry : corpus("cpp")) {
      if (entry.name().equals("extensions_unkeyed")) {
        continue;
      }
      for (String mode : MODES) {
        String label = entry.name() + "." + mode;
        try {
          JsonNode wire = MAPPER.readTree(Files.readAllBytes(dir.resolve(label + ".json")));
          sameEvent(entry.event(), consume(wire));
          String key = wire.get("key").isNull()
              ? null
              : new String(Base64.getDecoder().decode(wire.get("key").asText()),
                  StandardCharsets.UTF_8);
          check("record key", keyOf(entry.event()), key);
          System.out.println("java accepted " + label);
          checked++;
        } catch (Exception failure) {
          System.err.println("FAIL " + label + ": " + failure);
          failures++;
        }
      }
    }
    System.out.println(
        "java verified " + checked + " record(s) produced by the C++ SDK, " + failures
            + " failure(s)");
    return checked == 0 ? 1 : failures;
  }

  /** Whether the Java consumer decodes a record carrying these headers and this value. */
  private static boolean reads(Map<String, String> headers, String value) {
    ConsumerRecord<String, byte[]> incoming =
        new ConsumerRecord<>(TOPIC, 0, 0L, null, value.getBytes(StandardCharsets.UTF_8));
    headers.forEach((name, text) ->
        incoming.headers().add(name, text.getBytes(StandardCharsets.UTF_8)));
    try {
      return "1".equals(KafkaMessageFactory.createReader(incoming).toEvent().getId());
    } catch (Exception refused) {
      return false;
    }
  }

  /**
   * Records how the Java consumer reads header names that are not lowercase, which is where it
   * agrees with this SDK and differs from Go (D-INTEROP-5). It fails if the answer changes, so the
   * decision cannot go stale unnoticed.
   */
  private static int probeHeaderCase() {
    String body = "{\"specversion\":\"1.0\",\"id\":\"1\",\"source\":\"/s\",\"type\":\"t\"}";
    boolean capitalPrefix = reads(
        Map.of("CE_specversion", "1.0", "CE_id", "1", "CE_source", "/s", "CE_type", "t"), "");
    boolean capitalContentType =
        reads(Map.of("Content-Type", "application/cloudevents+json"), body);
    System.out.println("java reads CE_ prefix in capitals, binary mode: accepted=" + capitalPrefix);
    System.out.println(
        "java reads Content-Type in capitals, structured mode: accepted=" + capitalContentType);
    if (capitalPrefix || capitalContentType) {
      System.err.println("FAIL: java now reads capitalised header names; update D-INTEROP-5");
      return 1;
    }
    return 0;
  }

  public static void main(String[] args) throws Exception {
    Path outDir = Paths.get(args[0]);
    Path cppDir = Paths.get(args[1]);
    Files.createDirectories(outDir);
    for (Entry entry : corpus("java")) {
      for (String mode : MODES) {
        write(outDir, entry.name(), mode, produce(entry, mode));
      }
    }
    if (verify(cppDir) + probeHeaderCase() != 0) {
      System.exit(1);
    }
  }

  private KafkaInterop() {}
}
