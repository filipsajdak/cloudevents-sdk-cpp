package spike;

import com.google.protobuf.Struct;
import com.google.protobuf.Value;
import io.cloudevents.CloudEvent;
import io.cloudevents.CloudEventData;
import io.cloudevents.core.builder.CloudEventBuilder;
import io.cloudevents.core.data.BytesCloudEventData;
import io.cloudevents.protobuf.ProtoCloudEventData;
import io.cloudevents.protobuf.ProtobufFormat;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.nio.file.DirectoryStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.util.Map;
import java.util.TreeMap;

/**
 * Protobuf format spike (CR-0006). Modes:
 *   gen  OUTDIR          write one NAME.pb per corpus event
 *   dump INDIR OUTDIR    decode every NAME.pb with sdk-java, write NAME.txt
 */
public final class Generate {

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
    byte[] data = e.getData() == null ? new byte[0] : e.getData().toBytes();
    sb.append("data=").append(hex(data)).append('\n');
    return sb.toString();
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

  public static void main(String[] args) throws Exception {
    if (args[0].equals("gen")) {
      Path out = Paths.get(args[1]);
      Files.createDirectories(out);
      for (Map.Entry<String, CloudEvent> e : corpus().entrySet()) {
        byte[] b = FORMAT.serialize(e.getValue());
        Files.write(out.resolve(e.getKey() + ".pb"), b);
        System.out.println("wrote " + e.getKey() + ".pb " + b.length + " bytes");
      }
    } else if (args[0].equals("dump")) {
      Path in = Paths.get(args[1]);
      Path out = Paths.get(args[2]);
      Files.createDirectories(out);
      try (DirectoryStream<Path> files = Files.newDirectoryStream(in, "*.pb")) {
        for (Path f : files) {
          String name = f.getFileName().toString().replaceAll("\\.pb$", "");
          String text;
          try {
            text = dumpEvent(FORMAT.deserialize(Files.readAllBytes(f)));
          } catch (Exception ex) {
            text = "ERROR: " + ex.getClass().getSimpleName() + ": " + ex.getMessage() + "\n";
          }
          Files.write(out.resolve(name + ".txt"), utf8(text));
        }
      }
    }
  }
}
