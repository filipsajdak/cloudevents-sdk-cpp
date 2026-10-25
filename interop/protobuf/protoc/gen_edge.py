"""Hand-built wire cases, encoded by protoc from the v1.0.2 cloudevents.proto (a third producer).

usage: python3 -I gen_edge.py <directory holding cloudevents.proto at v1.0.2> <output directory>

The output directory is test/fixtures/interop/protobuf/protoc. interop/run.sh does not run this,
because it needs protoc and the .proto from the specification repository at the tag v1.0.2.
"""
import os
import subprocess
import sys

spec = sys.argv[1]  # directory holding the v1.0.2 cloudevents.proto
out = sys.argv[2]   # test/fixtures/interop/protobuf/protoc
root = out
os.makedirs(out, exist_ok=True)

HEAD = 'id: "%s" source: "/interop/protobuf" spec_version: "%s" type: "com.example.edge"'

cases = {
    "text_data_wire": HEAD % ("id-text-wire", "1.0")
    + ' attributes { key: "datacontenttype" value { ce_string: "text/plain" } } text_data: "plain text payload"',
    "attr_unset": HEAD % ("id-unset", "1.0") + ' attributes { key: "empty" value { } }',
    "duplicate_keys": HEAD % ("id-dup", "1.0")
    + ' attributes { key: "subject" value { ce_string: "first" } }'
    + ' attributes { key: "subject" value { ce_string: "second" } }',
    "spec_version_03": HEAD % ("id-v03", "0.3"),
    "missing_id": 'source: "/interop/protobuf" spec_version: "1.0" type: "com.example.edge"',
    "time_as_string": HEAD % ("id-timestr", "1.0")
    + ' attributes { key: "time" value { ce_string: "2026-09-20T12:34:56Z" } }',
    "subject_as_integer": HEAD % ("id-subjint", "1.0")
    + ' attributes { key: "subject" value { ce_integer: 7 } }',
    "uri_ref_extension": HEAD % ("id-uriref", "1.0")
    + ' attributes { key: "relref" value { ce_uri_ref: "../a/b?c#d" } }',
    "timestamp_negative_nanos": HEAD % ("id-tsneg", "1.0")
    + ' attributes { key: "time" value { ce_timestamp { seconds: 1 nanos: -5 } } }',
    "timestamp_year_9999": HEAD % ("id-ts9999", "1.0")
    + ' attributes { key: "atimestamp" value { ce_timestamp { seconds: 253402300799 nanos: 999999999 } } }',
    "timestamp_year_0001": HEAD % ("id-ts0001", "1.0")
    + ' attributes { key: "atimestamp" value { ce_timestamp { seconds: -62135596800 } } }',
    "proto_data_unregistered_type": HEAD % ("id-anyunk", "1.0")
    + ' proto_data { type_url: "type.example.test/not.Registered" value: "\\001\\002\\003" }',
}

for name, text in cases.items():
    data = subprocess.run(
        ["protoc", "-I", spec, "--encode=io.cloudevents.v1.CloudEvent", os.path.join(spec, "cloudevents.proto")],
        input=text.encode(), capture_output=True, check=True,
    ).stdout
    open(os.path.join(out, name + ".pb"), "wb").write(data)

# a field number the schema does not define (99, varint 1): every decoder must skip it
base = subprocess.run(
    ["protoc", "-I", spec, "--encode=io.cloudevents.v1.CloudEvent", os.path.join(spec, "cloudevents.proto")],
    input=(HEAD % ("id-unknown", "1.0")).encode(), capture_output=True, check=True,
).stdout
open(os.path.join(out, "unknown_field.pb"), "wb").write(base + bytes([0x98, 0x06, 0x01]))

batch = (
    'events { id: "id-b1" source: "/interop/protobuf" spec_version: "1.0" type: "com.example.batch" text_data: "one" } '
    'events { id: "id-b2" source: "/interop/protobuf" spec_version: "1.0" type: "com.example.batch" binary_data: "\\000\\001" }'
)
data = subprocess.run(
    ["protoc", "-I", spec, "--encode=io.cloudevents.v1.CloudEventBatch", os.path.join(spec, "cloudevents.proto")],
    input=batch.encode(), capture_output=True, check=True,
).stdout
open(os.path.join(out, "batch_two.pbbatch"), "wb").write(data)
print("wrote", len(cases) + 1, "single-event cases and one batch")
