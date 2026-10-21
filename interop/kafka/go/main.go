// Generates the Kafka record goldens the Go CloudEvents SDK produces, and checks
// that the records this SDK produced decode there to the event they encode
// (SWR-SEC-0008).
//
// No broker is involved: the sarama binding converts an event to and from the
// in-memory sarama.ProducerMessage and sarama.ConsumerMessage, which are the key,
// the headers and the value a broker would carry.
//
// usage: go run . <output-directory> <directory-of-cpp-records>
package main

import (
	"bytes"
	"context"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"reflect"
	"sort"
	"strings"
	"time"

	"github.com/IBM/sarama"
	ksarama "github.com/cloudevents/sdk-go/protocol/kafka_sarama/v2"
	cloudevents "github.com/cloudevents/sdk-go/v2"
	"github.com/cloudevents/sdk-go/v2/binding"
	"github.com/cloudevents/sdk-go/v2/event"
)

// record is one Kafka record as bytes, in the shape every producer in this
// directory writes. A null key or value is JSON null, which is not the empty
// string: a record whose value is null is a tombstone to a Kafka broker.
type record struct {
	Key     *string     `json:"key"`
	Headers [][2]string `json:"headers"`
	Value   *string     `json:"value"`
}

// The modes a record is written in, and the file-name suffix of each.
var modes = []string{"binary", "structured"}

// goExtensions are the extensions the Go consumer adds in binary mode to say
// where the record came from. They are not part of the event on the wire, so a
// comparison allows exactly these names and no others.
var goExtensions = map[string]bool{
	"kafkaoffset":    true,
	"kafkapartition": true,
	"kafkatopic":     true,
}

const partitionKey = "partitionkey"

func must(err error) {
	if err != nil {
		panic(err)
	}
}

func encode(bytes []byte) string { return base64.StdEncoding.EncodeToString(bytes) }

func decode(text string) []byte {
	out, err := base64.StdEncoding.DecodeString(text)
	must(err)
	return out
}

type named struct {
	name  string
	event event.Event
}

// corpus is the event set the HTTP interop uses (interop/go/main.go), with the
// producer's name in every source so a record says who wrote it.
func corpus(tag string) []named {
	source := "/interop/" + tag
	base := func(id, kind string) event.Event {
		e := cloudevents.NewEvent()
		e.SetSpecVersion("1.0")
		e.SetID(id)
		e.SetSource(source)
		e.SetType(kind)
		return e
	}
	var out []named
	add := func(name string, e event.Event) { out = append(out, named{name, e}) }

	instant, err := time.Parse(time.RFC3339, "2026-09-20T12:34:56Z")
	must(err)
	precise, err := time.Parse(time.RFC3339Nano, "2026-09-20T12:34:56.123456789+02:00")
	must(err)

	add("minimal", base("id-minimal", "com.example.minimal"))

	full := base("id-full", "com.example.full")
	full.SetSource("https://example.test/interop/" + tag)
	full.SetSubject("the-subject")
	full.SetTime(instant)
	full.SetDataSchema("https://example.test/schema/1")
	must(full.SetData("application/json", map[string]any{"count": 3, "label": tag}))
	add("full", full)

	extended := base("id-extensions", "com.example.extensions")
	extended.SetExtension("traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01")
	extended.SetExtension("sampledrate", 30)
	extended.SetExtension(partitionKey, "customer-42")
	add("extensions", extended)

	text := base("id-text", "com.example.text")
	must(text.SetData("text/plain", "plain text payload"))
	add("text_data", text)

	binaryEvent := base("id-binary", "com.example.binary")
	must(binaryEvent.SetData("application/octet-stream", []byte{0x00, 0x01, 0x02, 0xFF}))
	add("binary_data", binaryEvent)

	nanos := base("id-nanos", "com.example.nanos")
	nanos.SetTime(precise)
	add("time_nanoseconds", nanos)

	types := base("id-types", "com.example.types")
	types.SetExtension("astring", "text")
	types.SetExtension("aninteger", 42)
	types.SetExtension("aboolean", true)
	types.SetExtension("negative", -7)
	types.SetExtension("zero", 0)
	add("extension_types", types)

	unicode := base("id-unicode-é文", "com.example.unicode")
	unicode.SetSource("/interop/" + tag + "/événement")
	unicode.SetSubject("日本語 \U0001F600")
	must(unicode.SetData("application/json", map[string]any{"text": "é文 \U0001F600"}))
	add("unicode", unicode)

	percent := base("id-percent", "com.example.percent")
	percent.SetSubject("100% of 50%OFF")
	percent.SetExtension("pct", "a%20b")
	add("percent_in_subject", percent)

	scalar := base("id-scalar", "com.example.scalar")
	must(scalar.SetData("application/json", []any{1, 2, 3}))
	add("data_array", scalar)

	relative := base("id-relative", "t")
	relative.SetSource("/")
	add("minimal_relative", relative)

	return out
}

// withSortedExtensions returns the structured body with its extensions last and
// sorted by name. The SDK walks its extension map in Go's randomised order, so
// without this a regeneration reorders them and a golden changes for nothing
// (the same compromise interop/go makes). It panics if the sorted document is
// not the document the SDK wrote, so the golden stays what the SDK produced.
// The SDK writes extensions before data, so an event with both is refused.
func withSortedExtensions(e event.Event, written []byte) []byte {
	extensions := e.Extensions()
	if len(extensions) > 0 && e.Data() != nil {
		panic("an event with extensions and data would put them in the wrong place")
	}
	bare := e.Clone()
	names := make([]string, 0, len(extensions))
	for name := range extensions {
		bare.SetExtension(name, nil)
		names = append(names, name)
	}
	sort.Strings(names)

	head, err := json.Marshal(bare)
	must(err)
	out := bytes.NewBuffer(head[:len(head)-1])
	for _, name := range names {
		key, err := json.Marshal(name)
		must(err)
		value, err := json.Marshal(extensions[name])
		must(err)
		out.WriteByte(',')
		out.Write(key)
		out.WriteByte(':')
		out.Write(value)
	}
	out.WriteByte('}')

	var left, right any
	must(json.Unmarshal(written, &left))
	must(json.Unmarshal(out.Bytes(), &right))
	if !reflect.DeepEqual(left, right) {
		panic(fmt.Sprintf("sorted body %s differs from the SDK's %s", out.Bytes(), written))
	}
	return out.Bytes()
}

// produce writes e as the Go SDK's Kafka binding does, in the given mode.
func produce(e event.Event, mode string) record {
	ctx := context.Background()
	if mode == "structured" {
		ctx = binding.WithForceStructured(ctx)
	} else {
		ctx = binding.WithForceBinary(ctx)
	}
	out := &sarama.ProducerMessage{}
	must(ksarama.WriteProducerMessage(ctx, binding.ToMessage(&e), out))

	wire := record{Headers: [][2]string{}}
	if out.Key != nil {
		key, err := out.Key.Encode()
		must(err)
		text := encode(key)
		wire.Key = &text
	}
	if out.Value != nil {
		value, err := out.Value.Encode()
		must(err)
		if mode == "structured" {
			value = withSortedExtensions(e, value)
		}
		text := encode(value)
		wire.Value = &text
	}
	for _, header := range out.Headers {
		wire.Headers = append(wire.Headers, [2]string{string(header.Key), encode(header.Value)})
	}
	// The SDK writes the extension headers in the order of a Go map, which is
	// random, so a regeneration would reorder them and a golden would change for
	// nothing. The order of record headers carries no meaning to CloudEvents.
	sort.SliceStable(wire.Headers, func(left, right int) bool {
		return wire.Headers[left][0] < wire.Headers[right][0]
	})
	return wire
}

func write(dir, name, mode string, wire record) {
	data, err := json.MarshalIndent(wire, "", "  ")
	must(err)
	must(os.WriteFile(filepath.Join(dir, name+"."+mode+".json"), append(data, '\n'), 0o644))
}

// consume reads a record as a Go consumer would, from the bytes alone.
func consume(wire record) (*event.Event, error) {
	incoming := &sarama.ConsumerMessage{}
	if wire.Key != nil {
		incoming.Key = decode(*wire.Key)
	}
	if wire.Value != nil {
		incoming.Value = decode(*wire.Value)
	}
	for _, header := range wire.Headers {
		incoming.Headers = append(incoming.Headers,
			&sarama.RecordHeader{Key: []byte(header[0]), Value: decode(header[1])})
	}
	return binding.ToEvent(context.Background(), ksarama.NewMessageFromConsumerMessage(incoming))
}

func isJSON(contentType string) bool {
	return contentType == "" || strings.HasPrefix(contentType, "application/json") ||
		strings.HasSuffix(contentType, "+json")
}

// sameData compares the payload: a JSON payload as a document, because the JSON
// format may re-serialise it, anything else byte for byte (D-INTEROP-1).
func sameData(want, got event.Event) error {
	if isJSON(want.DataContentType()) && len(want.Data()) > 0 && len(got.Data()) > 0 {
		var left, right any
		if err := json.Unmarshal(want.Data(), &left); err != nil {
			return err
		}
		if err := json.Unmarshal(got.Data(), &right); err != nil {
			return fmt.Errorf("data is not JSON: %q", got.Data())
		}
		if !reflect.DeepEqual(left, right) {
			return fmt.Errorf("data %s != %s", want.Data(), got.Data())
		}
		return nil
	}
	if !bytes.Equal(want.Data(), got.Data()) {
		return fmt.Errorf("data %q != %q", want.Data(), got.Data())
	}
	return nil
}

// timeText is the instant with its offset, because timestamp equality is
// textual across the SDKs (D-INTEROP-3); an absent time is the empty string.
func timeText(instant time.Time) string {
	if instant.IsZero() {
		return ""
	}
	return instant.Format(time.RFC3339Nano)
}

// sameEvent compares every attribute and every extension. Extensions are
// compared by their text, because binary mode carries them as header strings.
func sameEvent(want, got event.Event, mode string) error {
	pairs := [][3]string{
		{"specversion", want.SpecVersion(), got.SpecVersion()},
		{"id", want.ID(), got.ID()},
		{"source", want.Source(), got.Source()},
		{"type", want.Type(), got.Type()},
		{"subject", want.Subject(), got.Subject()},
		{"datacontenttype", want.DataContentType(), got.DataContentType()},
		{"dataschema", want.DataSchema(), got.DataSchema()},
		{"time", timeText(want.Time()), timeText(got.Time())},
	}
	for _, pair := range pairs {
		if pair[1] != pair[2] {
			return fmt.Errorf("%s: want %q, got %q", pair[0], pair[1], pair[2])
		}
	}
	wanted := map[string]string{}
	for name, value := range want.Extensions() {
		wanted[name] = fmt.Sprint(value)
	}
	gotten := map[string]string{}
	for name, value := range got.Extensions() {
		if mode == "binary" && goExtensions[name] {
			continue
		}
		gotten[name] = fmt.Sprint(value)
	}
	if !reflect.DeepEqual(wanted, gotten) {
		return fmt.Errorf("extensions: want %v, got %v", wanted, gotten)
	}
	return sameData(want, got)
}

// keyOf is the record key this SDK's partitionkey mapper chooses.
func keyOf(e event.Event) *string {
	if value, ok := e.Extensions()[partitionKey]; ok {
		text := encode([]byte(fmt.Sprint(value)))
		return &text
	}
	return nil
}

func sameKey(want, got *string) bool {
	if want == nil || got == nil {
		return want == got
	}
	return *want == *got
}

func verify(dir string) int {
	failures, checked := 0, 0
	extras := map[string]bool{}
	for _, entry := range corpus("cpp") {
		for _, mode := range modes {
			path := filepath.Join(dir, entry.name+"."+mode+".json")
			raw, err := os.ReadFile(path)
			if err != nil {
				fmt.Fprintf(os.Stderr, "FAIL %s: %v\n", path, err)
				failures++
				continue
			}
			var wire record
			must(json.Unmarshal(raw, &wire))
			got, err := consume(wire)
			if err != nil {
				fmt.Fprintf(os.Stderr, "FAIL %s.%s: go refused it: %v\n", entry.name, mode, err)
				failures++
				continue
			}
			if err := got.Validate(); err != nil {
				fmt.Fprintf(os.Stderr, "FAIL %s.%s: invalid: %v\n", entry.name, mode, err)
				failures++
				continue
			}
			if err := sameEvent(entry.event, *got, mode); err != nil {
				fmt.Fprintf(os.Stderr, "FAIL %s.%s: %v\n", entry.name, mode, err)
				failures++
				continue
			}
			if !sameKey(keyOf(entry.event), wire.Key) {
				fmt.Fprintf(os.Stderr, "FAIL %s.%s: record key differs from the partitionkey\n",
					entry.name, mode)
				failures++
				continue
			}
			for name := range got.Extensions() {
				if _, declared := entry.event.Extensions()[name]; !declared {
					extras[name] = true
				}
			}
			fmt.Printf("go accepted %s.%s\n", entry.name, mode)
			checked++
		}
	}
	names := make([]string, 0, len(extras))
	for name := range extras {
		names = append(names, name)
	}
	sort.Strings(names)
	fmt.Printf("go added these extensions on read: %v\n", names)
	fmt.Printf("go verified %d record(s) produced by the C++ SDK, %d failure(s)\n", checked, failures)
	if checked == 0 {
		return 1
	}
	return failures
}

// probeHeaderCase records how the Go consumer reads header names that are not
// lowercase, which is where it differs from this SDK and from Java (D-INTEROP-5).
// It fails if the answer changes, so the decision cannot go stale unnoticed.
func probeHeaderCase() int {
	body := []byte(`{"specversion":"1.0","id":"1","source":"/s","type":"t"}`)
	probes := []struct {
		label   string
		headers map[string]string
		value   []byte
	}{
		{"CE_ prefix in capitals, binary mode",
			map[string]string{"CE_specversion": "1.0", "CE_id": "1", "CE_source": "/s", "CE_type": "t"}, nil},
		{"Content-Type in capitals, structured mode",
			map[string]string{"Content-Type": "application/cloudevents+json"}, body},
	}
	failures := 0
	for _, probe := range probes {
		incoming := &sarama.ConsumerMessage{Value: probe.value}
		for name, value := range probe.headers {
			incoming.Headers = append(incoming.Headers,
				&sarama.RecordHeader{Key: []byte(name), Value: []byte(value)})
		}
		decoded, err := binding.ToEvent(context.Background(), ksarama.NewMessageFromConsumerMessage(incoming))
		accepted := err == nil && decoded.ID() == "1"
		fmt.Printf("go reads %s: accepted=%v\n", probe.label, accepted)
		if !accepted {
			fmt.Fprintf(os.Stderr, "FAIL: go no longer reads %s; update D-INTEROP-5\n", probe.label)
			failures++
		}
	}
	return failures
}

func main() {
	outDir, cppDir := os.Args[1], os.Args[2]
	must(os.MkdirAll(outDir, 0o755))
	for _, entry := range corpus("go") {
		for _, mode := range modes {
			write(outDir, entry.name, mode, produce(entry.event, mode))
		}
	}
	if failures := verify(cppDir) + probeHeaderCase(); failures != 0 {
		os.Exit(1)
	}
}
