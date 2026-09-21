package main

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"io"
	"net/http"
	"os"
	"sort"
	"strings"
	"time"

	cloudevents "github.com/cloudevents/sdk-go/v2"
	"github.com/cloudevents/sdk-go/v2/binding"
	"github.com/cloudevents/sdk-go/v2/event"
	cehttp "github.com/cloudevents/sdk-go/v2/protocol/http"
)

type observation struct {
	Headers map[string]string `json:"headers,omitempty"`
	Body    string            `json:"body_base64,omitempty"`
	Error   string            `json:"error,omitempty"`
	Event   map[string]any    `json:"event,omitempty"`
}

var report = map[string]observation{}

// send records what Go puts on the wire for an event.
func send(name string, build func(*event.Event)) {
	e := cloudevents.NewEvent()
	e.SetSpecVersion("1.0")
	e.SetID("1")
	e.SetSource("/probe")
	e.SetType("com.example.probe")
	build(&e)

	req, err := http.NewRequest("POST", "http://x/", nil)
	if err != nil {
		report[name] = observation{Error: err.Error()}
		return
	}
	if err := cehttp.WriteRequest(context.Background(), binding.ToMessage(&e), req); err != nil {
		report[name] = observation{Error: "write: " + err.Error()}
		return
	}
	obs := observation{Headers: map[string]string{}}
	for k, v := range req.Header {
		obs.Headers[strings.ToLower(k)] = v[0]
	}
	body := []byte{}
	if req.Body != nil {
		body, _ = io.ReadAll(req.Body)
	}
	obs.Body = base64.StdEncoding.EncodeToString(body)
	report[name] = obs
}

// recv records what Go makes of a message somebody else wrote.
func recv(name string, headers map[string]string, body string) {
	req, err := http.NewRequest("POST", "http://x/", strings.NewReader(body))
	if err != nil {
		report[name] = observation{Error: err.Error()}
		return
	}
	for k, v := range headers {
		req.Header.Set(k, v)
	}
	got, err := binding.ToEvent(context.Background(), cehttp.NewMessageFromHttpRequest(req))
	if err != nil {
		report[name] = observation{Error: "read: " + err.Error()}
		return
	}
	raw, err := json.Marshal(got)
	if err != nil {
		report[name] = observation{Error: err.Error()}
		return
	}
	var asMap map[string]any
	_ = json.Unmarshal(raw, &asMap)
	report[name] = observation{Event: asMap}
}

func main() {
	instant, _ := time.Parse(time.RFC3339, "2026-09-20T12:34:56.123456789+02:00")

	// --- send -------------------------------------------------------------
	send("send/subject_empty", func(e *event.Event) { e.SetSubject("") })
	send("send/extension_integer", func(e *event.Event) { e.SetExtension("num", 42) })
	send("send/extension_bool", func(e *event.Event) { e.SetExtension("flag", true) })
	send("send/extension_uppercase_name", func(e *event.Event) { e.SetExtension("MixedCase", "v") })
	send("send/time_with_offset", func(e *event.Event) { e.SetTime(instant) })
	send("send/no_datacontenttype", func(e *event.Event) {})
	send("send/binary_payload", func(e *event.Event) {
		_ = e.SetData("application/octet-stream", []byte{0x00, 0xFF})
	})
	send("send/text_payload", func(e *event.Event) { _ = e.SetData("text/plain", "hello") })
	send("send/dataschema", func(e *event.Event) { e.SetDataSchema("https://example.test/s") })

	// --- receive ----------------------------------------------------------
	base := map[string]string{
		"ce-specversion": "1.0", "ce-id": "1",
		"ce-source": "/s", "ce-type": "t",
	}
	with := func(extra map[string]string) map[string]string {
		out := map[string]string{}
		for k, v := range base {
			out[k] = v
		}
		for k, v := range extra {
			out[k] = v
		}
		return out
	}

	recv("recv/uppercase_header_name", with(map[string]string{"CE-SUBJECT": "s"}), "")
	recv("recv/unknown_ce_header", with(map[string]string{"ce-unknown": "v"}), "")
	recv("recv/invalid_extension_name", with(map[string]string{"ce-a_b": "v"}), "")
	recv("recv/uppercase_extension_name", with(map[string]string{"ce-MixedCase": "v"}), "")
	recv("recv/empty_subject", with(map[string]string{"ce-subject": ""}), "")
	recv("recv/empty_type", map[string]string{
		"ce-specversion": "1.0", "ce-id": "1", "ce-source": "/s", "ce-type": "",
	}, "")
	recv("recv/time_lowercase", with(map[string]string{"ce-time": "2026-09-20t12:34:56z"}), "")
	recv("recv/specversion_0_3", map[string]string{
		"ce-specversion": "0.3", "ce-id": "1", "ce-source": "/s", "ce-type": "t",
	}, "")
	recv("recv/body_no_content_type", with(nil), "some body")
	recv("recv/batch_content_type", map[string]string{
		"Content-Type": "application/cloudevents-batch+json",
	}, "[]")
	recv("recv/structured_with_charset", map[string]string{
		"Content-Type": "application/cloudevents+json; charset=utf-8",
	}, `{"specversion":"1.0","id":"1","source":"/s","type":"t"}`)
	recv("recv/no_ce_headers_plain_json", map[string]string{
		"Content-Type": "application/json",
	}, `{"hello":"world"}`)

	keys := make([]string, 0, len(report))
	for k := range report {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	ordered := make([]map[string]any, 0, len(keys))
	for _, k := range keys {
		raw, _ := json.Marshal(report[k])
		var m map[string]any
		_ = json.Unmarshal(raw, &m)
		m["case"] = k
		ordered = append(ordered, m)
	}
	out, _ := json.MarshalIndent(ordered, "", "  ")
	_, _ = os.Stdout.Write(out)
}
