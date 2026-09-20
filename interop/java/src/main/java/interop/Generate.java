package interop;

import com.fasterxml.jackson.databind.ObjectMapper;
import io.cloudevents.CloudEvent;
import io.cloudevents.core.builder.CloudEventBuilder;
import io.cloudevents.core.data.BytesCloudEventData;
import io.cloudevents.jackson.JsonFormat;
import java.io.File;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.OffsetDateTime;

/** Writes the Java goldens and checks the documents the C++ SDK produced. */
public final class Generate {

  private static final JsonFormat FORMAT = new JsonFormat();

  private static void write(Path dir, String name, CloudEvent event) throws Exception {
    byte[] encoded = FORMAT.serialize(event);
    Files.write(dir.resolve(name + ".json"), encoded);
    System.out.println("wrote " + name + ".json");
  }

  public static void main(String[] args) throws Exception {
    Path outDir = Paths.get(args[0]);
    Path ourDir = Paths.get(args[1]);
    Files.createDirectories(outDir);

    OffsetDateTime instant = OffsetDateTime.parse("2026-09-20T12:34:56Z");

    write(outDir, "minimal",
        CloudEventBuilder.v1()
            .withId("id-minimal")
            .withSource(URI.create("/interop/java"))
            .withType("com.example.minimal")
            .build());

    write(outDir, "full",
        CloudEventBuilder.v1()
            .withId("id-full")
            .withSource(URI.create("https://example.test/interop/java"))
            .withType("com.example.full")
            .withSubject("the-subject")
            .withTime(instant)
            .withDataSchema(URI.create("https://example.test/schema/1"))
            .withDataContentType("application/json")
            .withData("{\"count\":3,\"label\":\"java\"}".getBytes(StandardCharsets.UTF_8))
            .build());

    write(outDir, "extensions",
        CloudEventBuilder.v1()
            .withId("id-extensions")
            .withSource(URI.create("/interop/java"))
            .withType("com.example.extensions")
            .withExtension("traceparent",
                "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01")
            .withExtension("sampledrate", 30)
            .withExtension("partitionkey", "customer-42")
            .build());

    write(outDir, "text_data",
        CloudEventBuilder.v1()
            .withId("id-text")
            .withSource(URI.create("/interop/java"))
            .withType("com.example.text")
            .withDataContentType("text/plain")
            .withData("plain text payload".getBytes(StandardCharsets.UTF_8))
            .build());

    write(outDir, "binary_data",
        CloudEventBuilder.v1()
            .withId("id-binary")
            .withSource(URI.create("/interop/java"))
            .withType("com.example.binary")
            .withDataContentType("application/octet-stream")
            .withData(BytesCloudEventData.wrap(new byte[] {0x00, 0x01, 0x02, (byte) 0xFF}))
            .build());

    // --- the other direction: what the C++ SDK produced must decode here ----
    File[] entries = ourDir.toFile().listFiles((d, name) -> name.endsWith(".json"));
    if (entries == null || entries.length == 0) {
      System.err.println("no C++ documents to verify at " + ourDir);
      System.exit(1);
    }
    int checked = 0;
    for (File entry : entries) {
      byte[] raw = Files.readAllBytes(entry.toPath());
      try {
        CloudEvent decoded = FORMAT.deserialize(raw);
        System.out.println(
            "java accepted " + entry.getName() + " (id=" + decoded.getId()
                + " type=" + decoded.getType() + ")");
        checked++;
      } catch (Exception failure) {
        System.err.println("FAIL " + entry.getName() + ": " + failure);
        System.exit(1);
      }
    }
    System.out.println("java verified " + checked + " document(s) produced by the C++ SDK");
  }

  private Generate() {}
}
