// Protobuf format interoperability (CR-0009, SWR-PROTO-0016): writes the
// application/cloudevents+protobuf bytes the Go SDK produces, and checks that the bytes
// this SDK produced decode there to the event they encode.
//
// No broker or network is involved; the bytes are the whole interface.
//
//	go run . gen    <outdir>   one <name>.pb per corpus event (map entries sorted)
//	go run . verify <cppdir>   decode every <name>.pb the C++ SDK wrote, compare to the corpus
package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/url"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"

	cloudevents "github.com/cloudevents/sdk-go/v2"
	"github.com/cloudevents/sdk-go/v2/event"
	"github.com/cloudevents/sdk-go/v2/types"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/types/known/structpb"

	pbfmt "github.com/cloudevents/sdk-go/binding/format/protobuf/v2"
)

func must(err error) {
	if err != nil {
		panic(err)
	}
}

func base(id, typ string) event.Event {
	e := cloudevents.NewEvent()
	e.SetSpecVersion("1.0")
	e.SetID(id)
	e.SetSource("/interop/protobuf")
	e.SetType(typ)
	return e
}

func corpus() map[string]event.Event {
	out := map[string]event.Event{}

	out["minimal"] = base("id-minimal", "com.example.minimal")

	full := base("id-full", "com.example.full")
	full.SetSource("https://example.test/interop/protobuf")
	full.SetSubject("the-subject")
	instant, err := time.Parse(time.RFC3339, "2026-09-20T12:34:56Z")
	must(err)
	full.SetTime(instant)
	full.SetDataSchema("https://example.test/schema/1")
	must(full.SetData("application/json", map[string]any{"count": 3, "label": "interop"}))
	out["full"] = full

	ext := base("id-extensions", "com.example.extensions")
	ext.SetExtension("traceparent", "00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01")
	ext.SetExtension("sampledrate", 30)
	ext.SetExtension("partitionkey", "customer-42")
	out["extensions"] = ext

	text := base("id-text", "com.example.text")
	must(text.SetData("text/plain", "plain text payload"))
	out["text_data"] = text

	bin := base("id-binary", "com.example.binary")
	must(bin.SetData("application/octet-stream", []byte{0x00, 0x01, 0x02, 0xFF}))
	out["binary_data"] = bin

	precise, err := time.Parse(time.RFC3339Nano, "2026-09-20T12:34:56.123456789+02:00")
	must(err)
	nanos := base("id-nanos", "com.example.nanos")
	nanos.SetTime(precise)
	out["time_nanoseconds"] = nanos

	types_ := base("id-types", "com.example.types")
	types_.SetExtension("astring", "text")
	types_.SetExtension("aninteger", 42)
	types_.SetExtension("aboolean", true)
	types_.SetExtension("negative", -7)
	types_.SetExtension("zero", 0)
	types_.SetExtension("maxint", 2147483647)
	types_.SetExtension("minint", -2147483648)
	types_.SetExtension("abinary", []byte{0x00, 0xFF, 0x10})
	u, err := url.Parse("https://example.test/ext?q=1")
	must(err)
	types_.SetExtension("auri", types.URI{URL: *u})
	types_.SetExtension("atimestamp", types.Timestamp{Time: instant})
	out["extension_types"] = types_

	uni := base("id-unicode-é文", "com.example.unicode")
	uni.SetSource("/interop/protobuf/événement")
	uni.SetSubject("日本語 \U0001F600")
	must(uni.SetData("application/json", map[string]any{"text": "é文 \U0001F600"}))
	out["unicode"] = uni

	arr := base("id-array", "com.example.array")
	must(arr.SetData("application/json", []any{1, "two", 3.5}))
	out["data_array"] = arr

	rel := base("id-relative", "com.example.relative")
	rel.SetSource("../relative/ref")
	out["minimal_relative"] = rel

	// proto_data: a google.protobuf.Struct, as the spec example (6.2) does.
	st, err := structpb.NewStruct(map[string]any{"label": "interop", "count": 3, "ok": true})
	must(err)
	raw, err := proto.MarshalOptions{Deterministic: true}.Marshal(st)
	must(err)
	pd := base("id-proto", "com.example.proto")
	pd.SetDataSchema("type.googleapis.com/google.protobuf.Struct")
	must(pd.SetData("application/protobuf", raw))
	out["proto_data"] = pd

	pn := base("id-proto-noschema", "com.example.proto")
	must(pn.SetData("application/protobuf", raw))
	out["proto_data_noschema"] = pn

	empty := base("id-empty-text", "com.example.text")
	must(empty.SetData("text/plain", ""))
	out["text_data_empty"] = empty

	return out
}

func names() []string {
	out := make([]string, 0)
	for name := range corpus() {
		out = append(out, name)
	}
	sort.Strings(out)
	return out
}

func gen(dir string) {
	must(os.MkdirAll(dir, 0o755))
	for _, name := range names() {
		e := corpus()[name]
		pbe, err := pbfmt.ToProto(&e)
		must(err)
		// Deterministic orders map entries by key; the SDK's own Marshal does not.
		b, err := proto.MarshalOptions{Deterministic: true}.Marshal(pbe)
		must(err)
		must(os.WriteFile(filepath.Join(dir, name+".pb"), b, 0o644))
		fmt.Println("wrote", name+".pb", len(b), "bytes")
	}
}

// dumpEvent renders the attributes of an event in a form both SDKs can print identically.
// The payload is compared on its own, by sameData.
func dumpEvent(e *event.Event) string {
	var sb strings.Builder
	fmt.Fprintf(&sb, "specversion=%s\nid=%s\nsource=%s\ntype=%s\n", e.SpecVersion(), e.ID(), e.Source(), e.Type())
	attrs := map[string]string{}
	if e.DataContentType() != "" {
		attrs["datacontenttype"] = "string:" + e.DataContentType()
	}
	if e.DataSchema() != "" {
		attrs["dataschema"] = "uri:" + e.DataSchema()
	}
	if e.Subject() != "" {
		attrs["subject"] = "string:" + e.Subject()
	}
	if !e.Time().IsZero() {
		attrs["time"] = "timestamp:" + e.Time().UTC().Format(time.RFC3339Nano)
	}
	for name, v := range e.Extensions() {
		switch t := v.(type) {
		case bool:
			attrs[name] = fmt.Sprintf("boolean:%v", t)
		case int32:
			attrs[name] = fmt.Sprintf("integer:%d", t)
		case string:
			attrs[name] = "string:" + t
		case []byte:
			attrs[name] = fmt.Sprintf("binary:%x", t)
		case types.URI:
			attrs[name] = "uri:" + t.String()
		case types.URIRef:
			attrs[name] = "uriref:" + t.String()
		case types.Timestamp:
			attrs[name] = "timestamp:" + t.UTC().Format(time.RFC3339Nano)
		default:
			attrs[name] = fmt.Sprintf("?%T", v)
		}
	}
	keys := make([]string, 0, len(attrs))
	for n := range attrs {
		keys = append(keys, n)
	}
	sort.Strings(keys)
	for _, n := range keys {
		fmt.Fprintf(&sb, "attr.%s=%s\n", n, attrs[n])
	}
	return sb.String()
}

// sameData compares the payload. The C++ SDK writes a JSON payload to text_data, as the
// specification says text must be stored. The Go SDK reads a text_data of application/json
// back as a JSON string, so the document comes back quoted (D-INTEROP-6). That read is accepted
// alongside the correct one, so the check says what the SDK does and fails on anything else.
func sameData(want, got *event.Event) error {
	if bytes.Equal(want.Data(), got.Data()) {
		return nil
	}
	quoted, err := json.Marshal(string(want.Data()))
	must(err)
	if strings.HasPrefix(want.DataContentType(), "application/json") && bytes.Equal(quoted, got.Data()) {
		return nil
	}
	return fmt.Errorf("data %q != %q", want.Data(), got.Data())
}

// verify decodes the bytes the C++ SDK wrote for each corpus event, with the Go SDK, and fails
// on the first difference in any attribute or in the payload.
func verify(dir string) int {
	failures, checked := 0, 0
	for _, name := range names() {
		want := corpus()[name]
		raw, err := os.ReadFile(filepath.Join(dir, name+".pb"))
		if err != nil {
			fmt.Fprintf(os.Stderr, "FAIL %s: %v\n", name, err)
			failures++
			continue
		}
		var got event.Event
		if err := pbfmt.Protobuf.Unmarshal(raw, &got); err != nil {
			fmt.Fprintf(os.Stderr, "FAIL %s: go refused it: %v\n", name, err)
			failures++
			continue
		}
		// Validate refuses a dataschema that is a bare protobuf type URL (type.googleapis.com/...),
		// which is the value the Go SDK itself writes for proto_data, so that one event skips it.
		if name != "proto_data" {
			if err := got.Validate(); err != nil {
				fmt.Fprintf(os.Stderr, "FAIL %s: invalid: %v\n", name, err)
				failures++
				continue
			}
		}
		if dumpEvent(&want) != dumpEvent(&got) {
			fmt.Fprintf(os.Stderr, "FAIL %s: want\n%sgot\n%s", name, dumpEvent(&want), dumpEvent(&got))
			failures++
			continue
		}
		if err := sameData(&want, &got); err != nil {
			fmt.Fprintf(os.Stderr, "FAIL %s: %v\n", name, err)
			failures++
			continue
		}
		fmt.Printf("go accepted %s\n", name)
		checked++
	}
	fmt.Printf("go verified %d event(s) produced by the C++ SDK, %d failure(s)\n", checked, failures)
	if checked == 0 {
		return 1
	}
	return failures
}

func main() {
	if len(os.Args) == 3 && os.Args[1] == "gen" {
		gen(os.Args[2])
		return
	}
	if len(os.Args) == 3 && os.Args[1] == "verify" {
		os.Exit(verify(os.Args[2]))
	}
	fmt.Fprintln(os.Stderr, "usage: go run . gen <outdir> | go run . verify <cppdir>")
	os.Exit(2)
}
