# Security policy

## Reporting a vulnerability

Report a vulnerability privately through GitHub: open the repository's **Security** tab and choose **Report a vulnerability**.
Do not open a public issue for it.

Say which version or commit you tested, and include an input that shows the problem if you have one.
A message, a JSON document or a header set that reproduces it is the most useful thing you can send.

## Supported versions

Only the latest release gets security fixes.
The SDK is pre-1.0, so a fix ships in the next release rather than as a backport.

## Scope

The SDK parses input from the network: JSON documents, header fields, base64 and RFC 3339 timestamps.
A crash, hang, out-of-bounds access or undefined behaviour on any such input is in scope, whichever codec and binding you use.

The SDK performs no network I/O and chooses no JSON library for you.
A flaw in your transport, or in the JSON library behind a codec, belongs to that project.
We still want to hear about it if the SDK's use of the library makes it reachable.

## What is already in place

Every change runs the whole suite under AddressSanitizer and UndefinedBehaviorSanitizer.
Seven libFuzzer targets run nightly over the decoders, and the JSON codecs cap nesting depth.
[docs/DECISIONS.md](docs/DECISIONS.md) records the security-relevant decisions under the `D-SEC-` entries.
