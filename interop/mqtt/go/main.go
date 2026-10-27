// Generates the MQTT 5.0 PUBLISH goldens the Go CloudEvents SDK produces, and checks
// that the PUBLISH messages this SDK produced, structured and binary (with the ce- prefix
// policy), decode there to the event they encode (SWR-MQTT-0014, SWR-MQTT-0015; CR-0006
// spike, docs/spikes/mqtt.md).
//
// protocol/mqtt_paho/v2 converts an event to and from a caller-supplied
// *paho.Publish, which is the topic, the payload and the properties (Content Type,
// User Properties) a broker would carry. Nothing here opens a socket.
//
// usage: go run . <output-directory> <directory-of-cpp-messages>
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

	mqtt "github.com/cloudevents/sdk-go/protocol/mqtt_paho/v2"
	cloudevents "github.com/cloudevents/sdk-go/v2"
	"github.com/cloudevents/sdk-go/v2/binding"
	"github.com/cloudevents/sdk-go/v2/event"
	"github.com/eclipse/paho.golang/paho"
)

const topic = "mytopic"

// publish is one MQTT 5.0 PUBLISH as bytes. User properties are UTF-8 strings in
// MQTT, so they are written as text, not as base64. A null content type is a
// PUBLISH without the Content Type property. MQTT 3.1.1 has no properties at all:
// a 3.1.1 PUBLISH is the topic and the payload only.
type publish struct {
	MQTT           string      `json:"mqtt"`
	Topic          string      `json:"topic"`
	Payload        string      `json:"payload_base64"`
	ContentType    *string     `json:"content_type"`
	UserProperties [][2]string `json:"user_properties"`
}

var modes = []string{"binary", "structured"}

// verified is the modes of the C++ messages this program reads back. The C++ binary
// messages are written under the prefixed_names policy, the only form Go reads.
var verified = []string{"structured", "binary"}

func must(err error) {
	if err != nil {
		panic(err)
	}
}

type named struct {
	name  string
	event event.Event
}

// corpus is the event set of the other interop programs, with the producer's name in
// every source so a message says who wrote it.
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
	add("extensions", extended)

	text := base("id-text", "com.example.text")
	must(text.SetData("text/plain", "plain text payload"))
	add("text_data", text)

	binaryEvent := base("id-binary", "com.example.binary")
	must(binaryEvent.SetData("application/octet-stream", []byte{0x00, 0x01, 0x02, 0xFF}))
	add("binary_data", binaryEvent)

	types := base("id-types", "com.example.types")
	types.SetExtension("astring", "text")
	types.SetExtension("aninteger", 42)
	types.SetExtension("aboolean", true)
	types.SetExtension("negative", -7)
	add("extension_types", types)

	unicode := base("id-unicode-é文", "com.example.unicode")
	unicode.SetSource("/interop/" + tag + "/événement")
	unicode.SetSubject("日本語 \U0001F600")
	must(unicode.SetData("application/json", map[string]any{"text": "é文 \U0001F600"}))
	add("unicode", unicode)

	scalar := base("id-scalar", "com.example.scalar")
	must(scalar.SetData("application/json", []any{1, 2, 3}))
	add("data_array", scalar)

	return out
}

// withSortedExtensions is the same compromise interop/kafka/go makes: the SDK
// writes structured extensions in Go map order, so they are rewritten sorted and
// the result is checked to be the document the SDK wrote.
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

func produce(e event.Event, mode string) publish {
	ctx := context.Background()
	if mode == "structured" {
		ctx = binding.WithForceStructured(ctx)
	} else {
		ctx = binding.WithForceBinary(ctx)
	}
	out := &paho.Publish{Topic: topic}
	must(mqtt.WritePubMessage(ctx, binding.ToMessage(&e), out))

	wire := publish{MQTT: "5.0", Topic: out.Topic, UserProperties: [][2]string{}}
	payload := out.Payload
	if mode == "structured" {
		payload = withSortedExtensions(e, payload)
	}
	wire.Payload = base64.StdEncoding.EncodeToString(payload)
	if out.Properties != nil {
		if out.Properties.ContentType != "" {
			ct := out.Properties.ContentType
			wire.ContentType = &ct
		}
		for _, property := range out.Properties.User {
			wire.UserProperties = append(wire.UserProperties, [2]string{property.Key, property.Value})
		}
	}
	// Extensions come out in Go map order; MQTT User Properties are an ordered
	// list, but CloudEvents gives their order no meaning, so sort for a stable golden.
	sort.SliceStable(wire.UserProperties, func(l, r int) bool {
		return wire.UserProperties[l][0] < wire.UserProperties[r][0]
	})
	return wire
}

func write(dir, name, mode string, wire publish) {
	data, err := json.MarshalIndent(wire, "", "  ")
	must(err)
	must(os.WriteFile(filepath.Join(dir, name+"."+mode+".json"), append(data, '\n'), 0o644))
}

// consume reads a PUBLISH as a Go subscriber would, from the bytes alone.
func consume(wire publish) (*event.Event, error) {
	payload, err := base64.StdEncoding.DecodeString(wire.Payload)
	must(err)
	incoming := &paho.Publish{Topic: wire.Topic, Payload: payload}
	if wire.ContentType != nil || len(wire.UserProperties) > 0 {
		incoming.Properties = &paho.PublishProperties{}
		if wire.ContentType != nil {
			incoming.Properties.ContentType = *wire.ContentType
		}
		for _, property := range wire.UserProperties {
			incoming.Properties.User.Add(property[0], property[1])
		}
	}
	return binding.ToEvent(context.Background(), mqtt.NewMessage(incoming))
}

// textOf is every attribute, every extension and the data, as text. The time is
// compared with its offset, because timestamp equality is textual across the SDKs.
func textOf(e event.Event) string {
	return fmt.Sprintf("%s|%s|%s|%s|%s|%s|%s|%s|%v|%s", e.SpecVersion(), e.ID(), e.Source(),
		e.Type(), e.Subject(), e.DataContentType(), e.DataSchema(),
		e.Time().Format(time.RFC3339Nano), e.Extensions(), e.Data())
}

// roundTrip has the Go SDK read back what it wrote, in both modes: the evidence
// that a golden is a PUBLISH the SDK itself accepts.
func roundTrip() int {
	failures := 0
	for _, entry := range corpus("go") {
		for _, mode := range modes {
			got, err := consume(produce(entry.event, mode))
			switch {
			case err != nil:
				fmt.Fprintf(os.Stderr, "FAIL %s.%s: %v\n", entry.name, mode, err)
				failures++
			case textOf(entry.event) != textOf(*got):
				fmt.Fprintf(os.Stderr, "FAIL %s.%s: %q != %q\n", entry.name, mode, textOf(entry.event), textOf(*got))
				failures++
			default:
				fmt.Printf("go read back %s.%s\n", entry.name, mode)
			}
		}
	}
	return failures
}

// probe records how the Go reader treats the forms the v1.0.2 text defines and
// the forms it defines itself. It exits non-zero if an answer changes, so the
// report cannot go stale unnoticed.
func probe() int {
	body := []byte(`{"specversion":"1.0","id":"1","source":"/s","type":"t"}`)
	cases := []struct {
		label  string
		wire   publish
		accept bool
	}{
		{"spec binary: unprefixed user properties, Content Type property",
			publish{Topic: topic, Payload: "e30=", ContentType: ptr("application/json"),
				UserProperties: [][2]string{{"specversion", "1.0"}, {"id", "1"}, {"source", "/s"}, {"type", "t"}}}, false},
		{"go binary: ce- prefixed user properties",
			publish{Topic: topic, Payload: "e30=", ContentType: ptr("application/json"),
				UserProperties: [][2]string{{"ce-specversion", "1.0"}, {"ce-id", "1"}, {"ce-source", "/s"}, {"ce-type", "t"}}}, true},
		{"spec binary with datacontenttype also as a user property (spec example 3.1.4)",
			publish{Topic: topic, Payload: "e30=", ContentType: ptr("application/json"),
				UserProperties: [][2]string{{"specversion", "1.0"}, {"id", "1"}, {"source", "/s"}, {"type", "t"}, {"datacontenttype", "application/json"}}}, false},
		{"spec structured, 5.0: Content Type application/cloudevents+json",
			publish{Topic: topic, Payload: base64.StdEncoding.EncodeToString(body),
				ContentType: ptr("application/cloudevents+json; charset=utf-8")}, true},
		{"spec structured, 3.1.1: payload only, no properties",
			publish{Topic: topic, Payload: base64.StdEncoding.EncodeToString(body)}, false},
		{"structured with the media type as a user property named Content-Type",
			publish{Topic: topic, Payload: base64.StdEncoding.EncodeToString(body),
				UserProperties: [][2]string{{"Content-Type", "application/cloudevents+json"}}}, false},
		{"go binary, ce-specversion in capitals",
			publish{Topic: topic, Payload: "e30=",
				UserProperties: [][2]string{{"CE-specversion", "1.0"}, {"CE-id", "1"}, {"CE-source", "/s"}, {"CE-type", "t"}}}, false},
	}
	failures := 0
	for _, c := range cases {
		decoded, err := consume(c.wire)
		accepted := err == nil && decoded.ID() == "1"
		fmt.Printf("go reads %s: accepted=%v\n", c.label, accepted)
		if accepted != c.accept {
			fmt.Fprintf(os.Stderr, "FAIL: the answer for %q changed; update docs/spikes/mqtt.md\n", c.label)
			failures++
		}
	}
	return failures
}

func ptr(text string) *string { return &text }

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

// sameEvent compares every attribute and every extension, the extensions by their
// text.
func sameEvent(want, got event.Event) error {
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
		gotten[name] = fmt.Sprint(value)
	}
	if !reflect.DeepEqual(wanted, gotten) {
		return fmt.Errorf("extensions: want %v, got %v", wanted, gotten)
	}
	return sameData(want, got)
}

// verify has the Go SDK read the PUBLISH messages the C++ SDK wrote and compares
// each with the event it was written from.
func verify(dir string) int {
	failures, checked := 0, 0
	for _, entry := range corpus("cpp") {
		for _, mode := range verified {
			path := filepath.Join(dir, entry.name+"."+mode+".json")
			raw, err := os.ReadFile(path)
			if err != nil {
				fmt.Fprintf(os.Stderr, "FAIL %s: %v\n", path, err)
				failures++
				continue
			}
			var wire publish
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
			if err := sameEvent(entry.event, *got); err != nil {
				fmt.Fprintf(os.Stderr, "FAIL %s.%s: %v\n", entry.name, mode, err)
				failures++
				continue
			}
			fmt.Printf("go accepted %s.%s\n", entry.name, mode)
			checked++
		}
	}
	fmt.Printf("go verified %d message(s) produced by the C++ SDK, %d failure(s)\n", checked, failures)
	if checked == 0 {
		return 1
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
	if failures := verify(cppDir) + roundTrip() + probe(); failures != 0 {
		os.Exit(1)
	}
}
