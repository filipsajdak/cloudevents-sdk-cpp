package interop;

import com.google.protobuf.Struct;
import com.google.protobuf.Value;
import io.cloudevents.CloudEvent;
import io.cloudevents.CloudEventData;
import io.cloudevents.core.builder.CloudEventBuilder;
import io.cloudevents.core.data.BytesCloudEventData;
import io.cloudevents.protobuf.ProtoCloudEventData;
import io.cloudevents.http.HttpMessageFactory;
import io.cloudevents.kafka.KafkaMessageFactory;
import io.cloudevents.protobuf.ProtobufFormat;
import java.io.ByteArrayOutputStream;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import org.apache.kafka.clients.producer.ProducerRecord;
import org.apache.kafka.common.header.Header;
import org.apache.kafka.common.header.internals.RecordHeaders;

/**
 * Protobuf format interoperability (CR-0009, SWR-PROTO-0016). Modes:
 *   gen    OUTDIR     write one NAME.pb per corpus event, as sdk-java serializes it, and the
 *                     structured message of its HTTP and Kafka bindings as NAME.http and NAME.kafka
 *   verify CPPDIR     decode every NAME.pb, NAME.http and NAME.kafka the C++ SDK wrote and compare
 *                     each with the corpus event
 *
 * A message file is the headers one per line ("name: value"), an empty line, then the body bytes.
 * The bindings convert to and from headers and a body in memory, so no server and no broker is
 * started (CR-0009, SWR-PROTO-0023).
 */
public final class ProtobufInterop {

  private static final ProtobufFormat FORMAT = new ProtobufFormat();

  private static CloudEventBuilder base(String id, String type) {
    return CloudEventBuilder.v1()
        .withId(id)
        .withSource(URI.create("/interop/protobuf"))
        .withType(type);
  }

  private static byte[] utf8(String s) {
    return s.getBytes(StandardCharsets.UTF_8);
  }

  private static Map<String, CloudEvent> corpus() {
    Map<String, CloudEvent> out = new TreeMap<>();
    OffsetDateTime instant = OffsetDateTime.parse("2026-09-20T12:34:56Z");

    out.put("minimal", base("id-minimal", "com.example.minimal").build());

    out.put("full",
        base("id-full", "com.example.full")
            .withSource(URI.create("https://example.test/interop/protobuf"))
            .withSubject("the-subject")
            .withTime(instant)
            .withDataSchema(URI.create("https://example.test/schema/1"))
            .withDataContentType("application/json")
            .withData(utf8("{\"count\":3,\"label\":\"interop\"}"))
            .build());

    out.put("extensions",
        base("id-extensions", "com.example.extensions")
            .withExtension("traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01")
            .withExtension("sampledrate", 30)
            .withExtension("partitionkey", "customer-42")
            .build());

    out.put("text_data",
        base("id-text", "com.example.text")
            .withDataContentType("text/plain")
            .withData(utf8("plain text payload"))
            .build());

    out.put("binary_data",
        base("id-binary", "com.example.binary")
            .withDataContentType("application/octet-stream")
            .withData(BytesCloudEventData.wrap(new byte[] {0x00, 0x01, 0x02, (byte) 0xFF}))
            .build());

    out.put("time_nanoseconds",
        base("id-nanos", "com.example.nanos")
            .withTime(OffsetDateTime.parse("2026-09-20T12:34:56.123456789+02:00"))
            .build());

    out.put("extension_types",
        base("id-types", "com.example.types")
            .withExtension("astring", "text")
            .withExtension("aninteger", 42)
            .withExtension("aboolean", true)
            .withExtension("negative", -7)
            .withExtension("zero", 0)
            .withExtension("maxint", Integer.MAX_VALUE)
            .withExtension("minint", Integer.MIN_VALUE)
            .withExtension("abinary", new byte[] {0x00, (byte) 0xFF, 0x10})
            .withExtension("auri", URI.create("https://example.test/ext?q=1"))
            .withExtension("atimestamp", instant)
            .build());

    out.put("unicode",
        base("id-unicode-é文", "com.example.unicode")
            .withSource(URI.create("/interop/protobuf/événement"))
            .withSubject("日本語 😀")
            .withDataContentType("application/json")
            .withData(utf8("{\"text\":\"é文 😀\"}"))
            .build());

    out.put("data_array",
        base("id-array", "com.example.array")
            .withDataContentType("application/json")
            .withData(utf8("[1,\"two\",3.5]"))
            .build());

    out.put("minimal_relative",
        base("id-relative", "com.example.relative")
            .withSource(URI.create("../relative/ref"))
            .build());

    Struct struct = Struct.newBuilder()
        .putFields("count", Value.newBuilder().setNumberValue(3).build())
        .putFields("label", Value.newBuilder().setStringValue("interop").build())
        .putFields("ok", Value.newBuilder().setBoolValue(true).build())
        .build();
    CloudEventData protoData = ProtoCloudEventData.wrap(struct);
    out.put("proto_data",
        base("id-proto", "com.example.proto")
            .withDataSchema(URI.create("type.googleapis.com/google.protobuf.Struct"))
            .withDataContentType("application/protobuf")
            .withData(protoData)
            .build());
    out.put("proto_data_noschema",
        base("id-proto-noschema", "com.example.proto")
            .withDataContentType("application/protobuf")
            .withData(protoData)
            .build());

    out.put("text_data_empty",
        base("id-empty-text", "com.example.text")
            .withDataContentType("text/plain")
            .withData(utf8(""))
            .build());
    return out;
  }

  private static String hex(byte[] b) {
    StringBuilder sb = new StringBuilder();
    for (byte x : b) {
      sb.append(String.format("%02x", x));
    }
    return sb.toString();
  }

  private static String dumpEvent(CloudEvent e) {
    StringBuilder sb = new StringBuilder();
    sb.append("specversion=").append(e.getSpecVersion().toString()).append('\n');
    sb.append("id=").append(e.getId()).append('\n');
    sb.append("source=").append(e.getSource()).append('\n');
    sb.append("type=").append(e.getType()).append('\n');
    Map<String, String> attrs = new TreeMap<>();
    if (e.getDataContentType() != null) {
      attrs.put("datacontenttype", "string:" + e.getDataContentType());
    }
    if (e.getDataSchema() != null) {
      attrs.put("dataschema", "uri:" + e.getDataSchema());
    }
    if (e.getSubject() != null) {
      attrs.put("subject", "string:" + e.getSubject());
    }
    if (e.getTime() != null) {
      attrs.put("time", "timestamp:" + fmtTime(e.getTime()));
    }
    for (String name : e.getExtensionNames()) {
      Object v = e.getExtension(name);
      if (v instanceof Boolean) {
        attrs.put(name, "boolean:" + v);
      } else if (v instanceof Integer) {
        attrs.put(name, "integer:" + v);
      } else if (v instanceof String) {
        attrs.put(name, "string:" + v);
      } else if (v instanceof byte[]) {
        attrs.put(name, "binary:" + hex((byte[]) v));
      } else if (v instanceof URI) {
        attrs.put(name, "uri:" + v);
      } else if (v instanceof OffsetDateTime) {
        attrs.put(name, "timestamp:" + fmtTime((OffsetDateTime) v));
      } else {
        attrs.put(name, "?" + v.getClass().getName());
      }
    }
    for (Map.Entry<String, String> a : attrs.entrySet()) {
      sb.append("attr.").append(a.getKey()).append('=').append(a.getValue()).append('\n');
    }
    return sb.toString();
  }

  private static byte[] dataOf(CloudEvent e) {
    return e.getData() == null ? new byte[0] : e.getData().toBytes();
  }

  /** RFC 3339 in UTC with trailing zeros of the fraction trimmed, like Go's RFC3339Nano. */
  private static String fmtTime(OffsetDateTime t) {
    OffsetDateTime u = t.withOffsetSameInstant(ZoneOffset.UTC);
    String s = DateTimeFormatter.ofPattern("yyyy-MM-dd'T'HH:mm:ss").format(u);
    int nano = u.getNano();
    if (nano != 0) {
      String frac = String.format("%09d", nano).replaceAll("0+$", "");
      s += "." + frac;
    }
    return s + "Z";
  }

  /** A message as a transport carries it: header fields in order, and the body. */
  private static final class Wire {
    final List<String[]> headers = new ArrayList<>();
    byte[] body = new byte[0];

    byte[] toBytes() {
      ByteArrayOutputStream out = new ByteArrayOutputStream();
      for (String[] header : headers) {
        byte[] line = (header[0] + ": " + header[1] + "\n").getBytes(StandardCharsets.UTF_8);
        out.write(line, 0, line.length);
      }
      out.write('\n');
      out.write(body, 0, body.length);
      return out.toByteArray();
    }

    static Wire parse(byte[] raw) {
      int end = -1;
      for (int i = 0; i + 1 < raw.length; i++) {
        if (raw[i] == '\n' && raw[i + 1] == '\n') {
          end = i;
          break;
        }
      }
      if (end < 0) {
        throw new IllegalArgumentException("no empty line after the headers");
      }
      Wire wire = new Wire();
      String head = new String(raw, 0, end, StandardCharsets.UTF_8);
      for (String line : head.split("\n")) {
        int colon = line.indexOf(": ");
        if (colon < 0) {
          throw new IllegalArgumentException("header line '" + line + "' has no ': '");
        }
        wire.headers.add(new String[] {line.substring(0, colon), line.substring(colon + 2)});
      }
      wire.body = java.util.Arrays.copyOfRange(raw, end + 2, raw.length);
      return wire;
    }
  }

  private static final String PROTOBUF_MEDIA_TYPE = "application/cloudevents+protobuf";

  private static Wire httpWrite(CloudEvent event) {
    Wire wire = new Wire();
    HttpMessageFactory.createWriter(
            (name, value) -> wire.headers.add(new String[] {name, value}), body -> wire.body = body)
        .writeStructured(event, FORMAT);
    wire.headers.sort((left, right) -> left[0].compareTo(right[0]));
    return wire;
  }

  private static CloudEvent httpRead(Wire wire) {
    return HttpMessageFactory.createReader(
            accept -> wire.headers.forEach(header -> accept.accept(header[0], header[1])),
            wire.body)
        .toEvent();
  }

  private static Wire kafkaWrite(CloudEvent event) {
    ProducerRecord<Void, byte[]> record =
        KafkaMessageFactory.createWriter("interop").writeStructured(event, FORMAT);
    if (record.key() != null) {
      throw new IllegalStateException("the Java Kafka binding keyed a structured record");
    }
    Wire wire = new Wire();
    for (Header header : record.headers()) {
      wire.headers.add(new String[] {header.key(), new String(header.value(), StandardCharsets.UTF_8)});
    }
    wire.headers.sort((left, right) -> left[0].compareTo(right[0]));
    wire.body = record.value();
    return wire;
  }

  private static CloudEvent kafkaRead(Wire wire) {
    RecordHeaders headers = new RecordHeaders();
    for (String[] header : wire.headers) {
      headers.add(header[0], header[1].getBytes(StandardCharsets.UTF_8));
    }
    return KafkaMessageFactory.createReader(headers, wire.body).toEvent();
  }

  private static void writeMessages(Path dir, String name, CloudEvent event, byte[] encoded)
      throws Exception {
    Wire http = httpWrite(event);
    Wire kafka = kafkaWrite(event);
    if (!java.util.Arrays.equals(http.body, encoded) || !java.util.Arrays.equals(kafka.body, encoded)) {
      throw new IllegalStateException("a binding changed the body of " + name);
    }
    Files.write(dir.resolve(name + ".http"), http.toBytes());
    Files.write(dir.resolve(name + ".kafka"), kafka.toBytes());
  }

  /** Reads NAME.http and NAME.kafka as the SDK's bindings would and compares the event. */
  private static int verifyMessages(Path dir, String name, CloudEvent want) throws Exception {
    int failures = 0;
    for (String transport : new String[] {"http", "kafka"}) {
      String file = name + "." + transport;
      Path path = dir.resolve(file);
      if (!Files.exists(path)) {
        System.err.println("FAIL " + file + ": " + path + " is missing");
        failures++;
        continue;
      }
      try {
        Wire wire = Wire.parse(Files.readAllBytes(path));
        if (wire.headers.size() != 1
            || !wire.headers.get(0)[0].equalsIgnoreCase("content-type")
            || !wire.headers.get(0)[1].equals(PROTOBUF_MEDIA_TYPE)) {
          System.err.println("FAIL " + file + ": want only the content type as a header");
          failures++;
          continue;
        }
        CloudEvent got = transport.equals("http") ? httpRead(wire) : kafkaRead(wire);
        if (!dumpEvent(want).equals(dumpEvent(got))) {
          System.err.println("FAIL " + file + ": want\n" + dumpEvent(want) + "got\n" + dumpEvent(got));
          failures++;
          continue;
        }
        if (!sameData(name, want, got)) {
          System.err.println("FAIL " + file + ": data " + hex(dataOf(want)) + " != " + hex(dataOf(got)));
          failures++;
          continue;
        }
        System.out.println("java accepted " + file);
      } catch (Exception ex) {
        System.err.println("FAIL " + file + ": java refused it: " + ex);
        failures++;
      }
    }
    return failures;
  }

  public static void main(String[] args) throws Exception {
    if (args[0].equals("gen")) {
      Path out = Paths.get(args[1]);
      Files.createDirectories(out);
      for (Map.Entry<String, CloudEvent> e : corpus().entrySet()) {
        byte[] b = FORMAT.serialize(e.getValue());
        Files.write(out.resolve(e.getKey() + ".pb"), b);
        System.out.println("wrote " + e.getKey() + ".pb " + b.length + " bytes");
        writeMessages(out, e.getKey(), e.getValue(), b);
      }
    } else if (args[0].equals("verify")) {
      System.exit(verify(Paths.get(args[1])));
    } else {
      System.err.println("usage: gen OUTDIR | verify CPPDIR");
      System.exit(2);
    }
  }

  /**
   * sdk-java reads a proto_data value as the whole serialized Any, type URL included, where the
   * Go SDK and this SDK read the value alone (D-INTEROP-6). The expected data is the value, so
   * the Any the C++ SDK wrote (type URL from the dataschema, then the value) is accepted.
   */
  private static boolean sameData(String name, CloudEvent want, CloudEvent got) {
    byte[] expected = dataOf(want);
    byte[] actual = dataOf(got);
    if (java.util.Arrays.equals(expected, actual)) {
      return true;
    }
    if (name.equals("proto_data") && want.getDataSchema() != null) {
      byte[] url = utf8(want.getDataSchema().toString());
      java.io.ByteArrayOutputStream any = new java.io.ByteArrayOutputStream();
      any.write(0x0A);
      any.write(url.length);
      any.write(url, 0, url.length);
      any.write(0x12);
      any.write(expected.length);
      any.write(expected, 0, expected.length);
      return java.util.Arrays.equals(any.toByteArray(), actual);
    }
    return false;
  }

  /** Decodes the bytes the C++ SDK wrote for each corpus event and fails on any difference. */
  private static int verify(Path dir) throws Exception {
    int failures = 0;
    int checked = 0;
    for (Map.Entry<String, CloudEvent> entry : corpus().entrySet()) {
      String name = entry.getKey();
      CloudEvent want = entry.getValue();
      Path file = dir.resolve(name + ".pb");
      if (!Files.exists(file)) {
        System.err.println("FAIL " + name + ": " + file + " is missing");
        failures++;
        continue;
      }
      CloudEvent got;
      try {
        got = FORMAT.deserialize(Files.readAllBytes(file));
      } catch (Exception ex) {
        System.err.println("FAIL " + name + ": java refused it: " + ex);
        failures++;
        continue;
      }
      if (!dumpEvent(want).equals(dumpEvent(got))) {
        System.err.println("FAIL " + name + ": want\n" + dumpEvent(want) + "got\n" + dumpEvent(got));
        failures++;
        continue;
      }
      if (!sameData(name, want, got)) {
        System.err.println("FAIL " + name + ": data " + hex(dataOf(want)) + " != " + hex(dataOf(got)));
        failures++;
        continue;
      }
      failures += verifyMessages(dir, name, want);
      System.out.println("java accepted " + name);
      checked++;
    }
    System.out.println("java verified " + checked + " event(s) produced by the C++ SDK, " + failures + " failure(s)");
    return checked == 0 ? 1 : failures;
  }
}
