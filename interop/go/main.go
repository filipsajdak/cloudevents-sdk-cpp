// Generates the golden JSON this SDK is checked against, using the Go
// CloudEvents SDK, and verifies that the documents this SDK produced decode
// there. Committed alongside its output so a golden can be regenerated and
// audited (SWR-SEC-0005).
package main

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"

	cloudevents "github.com/cloudevents/sdk-go/v2"
	"github.com/cloudevents/sdk-go/v2/binding"
	"github.com/cloudevents/sdk-go/v2/event"
	cehttp "github.com/cloudevents/sdk-go/v2/protocol/http"
)

// httpWire is one HTTP binary-mode message, as bytes rather than as a document.
// The JSON goldens beside it prove the event FORMAT agrees; these prove the
// HTTP binding's header mapping agrees, which nothing checked before.
type httpWire struct {
	Headers map[string]string `json:"headers"`
	Body    string            `json:"body_base64"`
}

// writeHTTPWire records exactly what the Go SDK puts on the wire in binary mode.
func writeHTTPWire(dir, name string, e event.Event) {
	must(os.MkdirAll(filepath.Join(dir, "http"), 0o755))
	req, err := http.NewRequest("POST", "http://interop/", nil)
	must(err)
	must(cehttp.WriteRequest(context.Background(), binding.ToMessage(&e), req))

	wire := httpWire{Headers: map[string]string{}}
	for key, values := range req.Header {
		wire.Headers[strings.ToLower(key)] = values[0]
	}
	body := []byte{}
	if req.Body != nil {
		body, err = io.ReadAll(req.Body)
		must(err)
	}
	wire.Body = base64.StdEncoding.EncodeToString(body)

	data, err := json.MarshalIndent(wire, "", "  ")
	must(err)
	must(os.WriteFile(filepath.Join(dir, "http", name+".json"), data, 0o644))
}

func must(err error) {
	if err != nil {
		panic(err)
	}
}

func write(dir, name string, e event.Event) {
	data, err := json.Marshal(e)
	must(err)
	must(os.WriteFile(filepath.Join(dir, name+".json"), data, 0o644))
	writeHTTPWire(dir, name, e)
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

	// --- edge cases: where SDKs are most likely to disagree -----------------

	// A timestamp with nanosecond precision and a non-UTC offset. Go marshals
	// time.Time as RFC3339Nano, which is more precision than most examples show.
	precise, err := time.Parse(time.RFC3339Nano, "2026-09-20T12:34:56.123456789+02:00")
	must(err)
	nanos := cloudevents.NewEvent()
	nanos.SetSpecVersion("1.0")
	nanos.SetID("id-nanos")
	nanos.SetSource("/interop/go")
	nanos.SetType("com.example.nanos")
	nanos.SetTime(precise)
	write(outDir, "time_nanoseconds", nanos)

	// Every attribute type JSON can carry natively, at once.
	types := cloudevents.NewEvent()
	types.SetSpecVersion("1.0")
	types.SetID("id-types")
	types.SetSource("/interop/go")
	types.SetType("com.example.types")
	types.SetExtension("astring", "text")
	types.SetExtension("aninteger", 42)
	types.SetExtension("aboolean", true)
	types.SetExtension("negative", -7)
	types.SetExtension("zero", 0)
	write(outDir, "extension_types", types)

	// Non-ASCII in the attributes and in the payload.
	unicode := cloudevents.NewEvent()
	unicode.SetSpecVersion("1.0")
	unicode.SetID("id-unicode-\u00e9\u6587")
	unicode.SetSource("/interop/go/\u00e9v\u00e9nement")
	unicode.SetType("com.example.unicode")
	unicode.SetSubject("\u65e5\u672c\u8a9e \U0001F600")
	must(unicode.SetData("application/json", map[string]any{"text": "\u00e9\u6587 \U0001F600"}))
	write(outDir, "unicode", unicode)

	// A percent in an attribute value. This is the case the HTTP binding spec
	// says to escape and the Go SDK does not, so it is where the two disagree.
	percent := cloudevents.NewEvent()
	percent.SetSpecVersion("1.0")
	percent.SetID("id-percent")
	percent.SetSource("/interop/go")
	percent.SetType("com.example.percent")
	percent.SetSubject("100% of 50%OFF")
	percent.SetExtension("pct", "a%20b")
	write(outDir, "percent_in_subject", percent)

	// A JSON payload that is not an object: the format permits any JSON value.
	scalar := cloudevents.NewEvent()
	scalar.SetSpecVersion("1.0")
	scalar.SetID("id-scalar")
	scalar.SetSource("/interop/go")
	scalar.SetType("com.example.scalar")
	must(scalar.SetData("application/json", []any{1, 2, 3}))
	write(outDir, "data_array", scalar)

	// Required attributes and nothing else, with an empty-ish source.
	relative := cloudevents.NewEvent()
	relative.SetSpecVersion("1.0")
	relative.SetID("id-relative")
	relative.SetSource("/")
	relative.SetType("t")
	write(outDir, "minimal_relative", relative)

	// --- a batch, which nothing had exercised across SDKs -------------------
	batch := []event.Event{minimal, full, extended}
	batchBytes, err := json.Marshal(batch)
	must(err)
	must(os.WriteFile(filepath.Join(outDir, "batch.json"), batchBytes, 0o644))
	fmt.Println("wrote batch.json")

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

		// A batch is an array of events, not an event.
		if entry.Name() == "batch.json" {
			var events []event.Event
			if err := json.Unmarshal(raw, &events); err != nil {
				fmt.Fprintf(os.Stderr, "FAIL %s: %v\n", entry.Name(), err)
				os.Exit(1)
			}
			for i, e := range events {
				if err := e.Validate(); err != nil {
					fmt.Fprintf(os.Stderr, "FAIL %s[%d]: invalid: %v\n", entry.Name(), i, err)
					os.Exit(1)
				}
			}
			fmt.Printf("go accepted %s (%d events)\n", entry.Name(), len(events))
			checked++
			continue
		}

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
