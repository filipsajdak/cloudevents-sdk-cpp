---
uid: SWR-JSON-0040
title: A document above the retention limit keeps its payload as text
type: software
status: approved
priority: high
rationale: >
  A parsed DOM can occupy many times the bytes of its text, and a retained one lives as long as the event.
  Without a bound, a small hostile document could pin a large expansion in memory.
  The limit is a decode option, 16 KiB by default, and 0 keeps every payload as text.
  In the CI perf job for PR #58 a retained document cost 1.4 to 2.3 times the retained bytes of the same payload kept as text for a full event of about 400 bytes, and 1.9 to 3.3 times for a 52,889-byte event (nlohmann: 76,865 to 253,448 bytes).
  At 16 KiB a typical event keeps the document and its fast path, while a large payload stays text unless the caller raises the limit.
  A batch is measured per event by average size: it keeps documents when its text is at most the limit times its number of events.
  A limit on the whole batch text sent a batch of typical events to text, which cost decode_batch_100 up to 42 percent more allocated bytes than main in the CI perf job for PR #59.
  Retained memory then stays proportional to the input, with a worst case around 3 times its text: one large event among tiny ones is retained because the average is small.
  The absolute per-event bound applies to single-event decode.
  The product of the limit and the event count is checked, so a huge event count cannot overflow it into a small bound.
verification_method: test
security_classification: security-relevant
derived_from: [SYS-JSON-0001]
satisfied_by: [code:include/cloudevents/format/json_format.hpp]
verified_by: [test:test/json_format_test.cpp::decode-retains-document-up-to-limit]
owner: filip.sajdak
version: 2
---
When the json_format decoder reads a single event whose text is longer than the retention limit, it shall store its JSON payload as json_text instead of json_document.
When the json_format decoder reads a batch whose text is longer than the retention limit multiplied by the number of events in the batch, it shall store the JSON payload of every event in that batch as json_text instead of json_document.
When the retention limit is 0, the json_format decoder shall store every JSON payload as json_text.
