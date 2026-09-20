// Generates the golden JSON this SDK is checked against, using the Go
// CloudEvents SDK, and verifies that the documents this SDK produced decode
// there. Committed alongside its output so a golden can be regenerated and
// audited (SWR-SEC-0005).
package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"time"

	cloudevents "github.com/cloudevents/sdk-go/v2"
	"github.com/cloudevents/sdk-go/v2/event"
)

func must(err error) {
	if err != nil {
		panic(err)
	}
}

func write(dir, name string, e event.Event) {
	data, err := json.Marshal(e)
	must(err)
	must(os.WriteFile(filepath.Join(dir, name+".json"), data, 0o644))
}

func main() {
	outDir := os.Args[1]
	ourDir := os.Args[2]
	must(os.MkdirAll(outDir, 0o755))

	instant, err := time.Parse(time.RFC3339, "2026-09-20T12:34:56Z")
	must(err)

	// minimal: the four required attributes and nothing else
	minimal := cloudevents.NewEvent()
	minimal.SetSpecVersion("1.0")
	minimal.SetID("id-minimal")
	minimal.SetSource("/interop/go")
	minimal.SetType("com.example.minimal")
	write(outDir, "minimal", minimal)

	// full: every optional context attribute present
	full := cloudevents.NewEvent()
	full.SetSpecVersion("1.0")
	full.SetID("id-full")
	full.SetSource("https://example.test/interop/go")
	full.SetType("com.example.full")
	full.SetSubject("the-subject")
	full.SetTime(instant)
	full.SetDataSchema("https://example.test/schema/1")
	must(full.SetData("application/json", map[string]any{"count": 3, "label": "go"}))
	write(outDir, "full", full)

	// extensions: the three attribute types JSON can carry natively
	extended := cloudevents.NewEvent()
	extended.SetSpecVersion("1.0")
	extended.SetID("id-extensions")
	extended.SetSource("/interop/go")
	extended.SetType("com.example.extensions")
	extended.SetExtension("traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01")
	extended.SetExtension("sampledrate", 30)
	extended.SetExtension("partitionkey", "customer-42")
	write(outDir, "extensions", extended)

	// text data under a non-JSON content type
	text := cloudevents.NewEvent()
	text.SetSpecVersion("1.0")
	text.SetID("id-text")
	text.SetSource("/interop/go")
	text.SetType("com.example.text")
	must(text.SetData("text/plain", "plain text payload"))
	write(outDir, "text_data", text)

	// binary data, which the JSON format carries as data_base64
	binary := cloudevents.NewEvent()
	binary.SetSpecVersion("1.0")
	binary.SetID("id-binary")
	binary.SetSource("/interop/go")
	binary.SetType("com.example.binary")
	must(binary.SetData("application/octet-stream", []byte{0x00, 0x01, 0x02, 0xFF}))
	write(outDir, "binary_data", binary)

	// --- the other direction: what this SDK produced must decode here -------
	entries, err := os.ReadDir(ourDir)
	if err != nil {
		fmt.Fprintf(os.Stderr, "no C++ documents to verify at %s: %v\n", ourDir, err)
		os.Exit(1)
	}
	checked := 0
	for _, entry := range entries {
		if filepath.Ext(entry.Name()) != ".json" {
			continue
		}
		raw, err := os.ReadFile(filepath.Join(ourDir, entry.Name()))
		must(err)
		var decoded event.Event
		if err := json.Unmarshal(raw, &decoded); err != nil {
			fmt.Fprintf(os.Stderr, "FAIL %s: %v\n", entry.Name(), err)
			os.Exit(1)
		}
		if err := decoded.Validate(); err != nil {
			fmt.Fprintf(os.Stderr, "FAIL %s: invalid: %v\n", entry.Name(), err)
			os.Exit(1)
		}
		fmt.Printf("go accepted %s (id=%s type=%s)\n", entry.Name(), decoded.ID(), decoded.Type())
		checked++
	}
	if checked == 0 {
		fmt.Fprintln(os.Stderr, "no C++ documents were verified")
		os.Exit(1)
	}
	fmt.Printf("go verified %d document(s) produced by the C++ SDK\n", checked)
}
