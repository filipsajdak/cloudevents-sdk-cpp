# Architecture Decision Records

Decisions with architectural consequences, one file each, `NNNN-kebab-title.md`.
Smaller judgement calls and the measurements behind them live in
`docs/DECISIONS.md`. The approved work specification is `docs/SPEC.md`.

## Format

`## Status`, `## Context`, `## Decision`, `## Consequences` (Positive / Negative /
Neutral), `## References`. Start from `template.md`.

## Index

| ADR | Title | Status |
|---|---|---|
| [0001](./0001-error-model-returned-not-thrown.md) | Recoverable failures are returned, never thrown | Accepted |
| [0002](./0002-result-subset-enforced-by-the-polyfill.md) | The `result<T>` subset is enforced by building against the polyfill | Accepted |
| [0003](./0003-describe-seam-opt-in-reflection.md) | One describe seam, with the C++26 backend opt-in | Accepted |
| [0004](./0004-json-codec-abstracts-a-dom.md) | The codec concept abstracts a JSON DOM, not the event | Accepted |
| [0005](./0005-transport-neutral-message.md) | Bindings map to a transport-neutral message, never to a transport | Accepted |
| [0006](./0006-one-configuration-header.md) | Every feature gate lives in one configuration header | Accepted |
| [0007](./0007-traits-parameterised-binding-core.md) | A binding is a traits type over a shared core | Accepted |
| [0008](./0008-invalid-events-are-not-representable.md) | An invalid event is not representable, and `validate()` goes away | Accepted |
| [0009](./0009-two-api-generations-side-by-side.md) | Two API generations side by side | Accepted |
| [0010](./0010-events-keep-their-json-document.md) | An event keeps its JSON document, in a third generation | Proposed |
