// Structured Protobuf mode through the Go SDK's HTTP and Kafka bindings (CR-0009, SWR-PROTO-0023).
//
// The bindings convert an event to and from the in-memory http.Request and the sarama producer and
// consumer messages, which are the headers, the key and the body a transport would carry, so no
// server and no broker is involved. A message is written to disk as the headers one per line
// ("name: value"), an empty line, then the body bytes unchanged: a form that any reader can parse
// without a JSON library.
package main

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/IBM/sarama"
	ksarama "github.com/cloudevents/sdk-go/protocol/kafka_sarama/v2"
	"github.com/cloudevents/sdk-go/v2/binding"
	"github.com/cloudevents/sdk-go/v2/binding/utils"
	"github.com/cloudevents/sdk-go/v2/event"
	cehttp "github.com/cloudevents/sdk-go/v2/protocol/http"

	pbfmt "github.com/cloudevents/sdk-go/binding/format/protobuf/v2"
)

// wireMessage is a message as a transport carries it: header fields in order, and the body.
type wireMessage struct {
	headers [][2]string
	body    []byte
}

// headerBodySeparator ends the header lines of a message file.
var headerBodySeparator = []byte("\n\n")

func (w wireMessage) bytes() []byte {
	var out bytes.Buffer
	for _, header := range w.headers {
		fmt.Fprintf(&out, "%s: %s\n", header[0], header[1])
	}
	out.WriteByte('\n')
	out.Write(w.body)
	return out.Bytes()
}

func parseWire(raw []byte) (wireMessage, error) {
	end := bytes.Index(raw, headerBodySeparator)
	if end < 0 {
		return wireMessage{}, fmt.Errorf("no empty line after the headers")
	}
	var out wireMessage
	for _, line := range strings.Split(string(raw[:end]), "\n") {
		name, value, found := strings.Cut(line, ": ")
		if !found {
			return wireMessage{}, fmt.Errorf("header line %q has no ': '", line)
		}
		out.headers = append(out.headers, [2]string{name, value})
	}
	out.body = raw[end+len(headerBodySeparator):]
	return out, nil
}

// transport is one binding of the Go SDK: how it writes a structured message and reads one.
type transport struct {
	extension string
	write     func(body []byte) wireMessage
	read      func(wireMessage) (*event.Event, error)
}

var transports = []transport{
	{extension: "http", write: httpWrite, read: httpRead},
	{extension: "kafka", write: kafkaWrite, read: kafkaRead},
}

// structured wraps the encoded event as the SDK does for a format other than JSON: a structured
// message that names the format, which each binding writes as its content type.
func structured(body []byte) binding.Message {
	return utils.NewStructuredMessage(pbfmt.Protobuf, bytes.NewReader(body))
}

func httpWrite(body []byte) wireMessage {
	request, err := http.NewRequest(http.MethodPost, "http://example.test/events", nil)
	must(err)
	must(cehttp.WriteRequest(context.Background(), structured(body), request))
	written, err := io.ReadAll(request.Body)
	must(err)
	var headers [][2]string
	for name, values := range request.Header {
		for _, value := range values {
			headers = append(headers, [2]string{name, value})
		}
	}
	sortHeaders(headers)
	return wireMessage{headers: headers, body: written}
}

func httpRead(w wireMessage) (*event.Event, error) {
	request := &http.Request{
		Method:        http.MethodPost,
		Header:        http.Header{},
		Body:          io.NopCloser(bytes.NewReader(w.body)),
		ContentLength: int64(len(w.body)),
	}
	for _, header := range w.headers {
		request.Header.Add(header[0], header[1])
	}
	return binding.ToEvent(context.Background(), cehttp.NewMessageFromHttpRequest(request))
}

func kafkaWrite(body []byte) wireMessage {
	record := &sarama.ProducerMessage{}
	// The SDK's key mapping is a transformer, which makes it decode a structured message and write
	// it again; without the mapping it hands the encoded bytes through, which is the case here.
	ctx := ksarama.WithSkipKeyMapping(context.Background())
	must(ksarama.WriteProducerMessage(ctx, structured(body), record))
	if record.Key != nil {
		// The C++ side sets no key, and the record's key is the caller's: a key here would be
		// the SDK deciding for the caller.
		panic("the Go Kafka binding keyed a structured record")
	}
	value, err := record.Value.Encode()
	must(err)
	var headers [][2]string
	for _, header := range record.Headers {
		headers = append(headers, [2]string{string(header.Key), string(header.Value)})
	}
	sortHeaders(headers)
	return wireMessage{headers: headers, body: value}
}

func kafkaRead(w wireMessage) (*event.Event, error) {
	incoming := &sarama.ConsumerMessage{Value: w.body}
	for _, header := range w.headers {
		incoming.Headers = append(incoming.Headers,
			&sarama.RecordHeader{Key: []byte(header[0]), Value: []byte(header[1])})
	}
	return binding.ToEvent(context.Background(), ksarama.NewMessageFromConsumerMessage(incoming))
}

func sortHeaders(headers [][2]string) {
	sort.SliceStable(headers, func(left, right int) bool { return headers[left][0] < headers[right][0] })
}

// writeMessages writes <name>.<transport> for every transport, from the bytes the SDK's format
// encodes for the event, and checks that the body is those very bytes.
func writeMessages(dir, name string, encoded []byte) {
	for _, via := range transports {
		wire := via.write(encoded)
		if !bytes.Equal(wire.body, encoded) {
			panic(fmt.Sprintf("the %s binding changed the body of %s", via.extension, name))
		}
		must(os.WriteFile(filepath.Join(dir, name+"."+via.extension), wire.bytes(), 0o644))
	}
}

// protobufMediaType is the content type of the structured Protobuf format.
const protobufMediaType = "application/cloudevents+protobuf"

// verifyMessages reads <name>.<transport> as the SDK's binding would and compares the event.
func verifyMessages(dir, name string, want event.Event) (failures int) {
	for _, via := range transports {
		file := name + "." + via.extension
		raw, err := os.ReadFile(filepath.Join(dir, file))
		if err != nil {
			fmt.Fprintf(os.Stderr, "FAIL %s: %v\n", file, err)
			failures++
			continue
		}
		wire, err := parseWire(raw)
		if err != nil {
			fmt.Fprintf(os.Stderr, "FAIL %s: %v\n", file, err)
			failures++
			continue
		}
		if len(wire.headers) != 1 || !strings.EqualFold(wire.headers[0][0], "content-type") ||
			wire.headers[0][1] != protobufMediaType {
			fmt.Fprintf(os.Stderr, "FAIL %s: headers are %v, want only the content type\n", file, wire.headers)
			failures++
			continue
		}
		got, err := via.read(wire)
		if err != nil {
			fmt.Fprintf(os.Stderr, "FAIL %s: go refused it: %v\n", file, err)
			failures++
			continue
		}
		if err := sameEventAndData(name, want, got); err != nil {
			fmt.Fprintf(os.Stderr, "FAIL %s: %v\n", file, err)
			failures++
			continue
		}
		fmt.Printf("go accepted %s\n", file)
	}
	return failures
}
