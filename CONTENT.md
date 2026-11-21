## What this is

The forebay ecosystem's C library, `forebay-library`. `library.json` declares it in the weivel standard's
format, `forebay-library` 0.2.0, with three services, `routing`, `accounts` and `anthropic`, each one function of kind
`definition`: its exported symbol (`forebay_routing_definition_of`, `forebay_accounts_definition_of`,
`forebay_anthropic_definition_of`)
answers a static C struct that begins with its `size`, then the provider's `context`, then one function pointer
per operation and one release per kind of answer, living until the library is unloaded. The structs are declared
in `include/forebay/routing.h`, `include/forebay/accounts.h` and `include/forebay/anthropic.h`, which document every operation, what it takes
and answers, and every refusal; this file says what they decide. No JSON crosses the boundary.

An operation takes the `context` and a size-prefixed argument struct, fills the caller's empty answer or, when it
refuses, the caller's `spisor_refusal`, and answers a `spisor_status`, both from `spisor/library`'s
`spisor/status.h`. A size-prefixed struct is read only up to its `size` and only grows at its end; an array
element has no size and never grows. Text handed in is NUL-terminated UTF-8 borrowed for the call, and what an
answer points at is the provider's until the release for its kind. Data whose shape belongs to someone else (a
user's settings record, an account, a quota reading) crosses as a `spisor_value`. The operations read nothing of
the process they run in: time, randomness and every path arrive as arguments. Names are the operations' own,
in snake case.

## The routing service

`routing` holds the decisions of basekit's model map and router. The profile members it reads are
`forebay_routing_profile`, the providers' models a catalog of `forebay_catalog_model`, and the model map a slot
per tier and the default, each served by `forebay_assignment`s (`provider`, `model`, `name`, `derived`).

| operation | decides |
| --- | --- |
| `tiers` | the tiers: `tier_regex` group 1 over the tier source's ranking, ordered by `tier_order`, unknown last |
| `model_map` | the map stored in the user's settings, its entries kept, healed within their provider, or derived, and whether it healed |
| `resolve` | the chain and its notices for a requested model: exact id, tier keyword, an offered model, then the default |
| `outcome` | whether a response was rate limited (on 429) and its reset: the retry hint in milliseconds when given, else the `retry-after` header in seconds |
| `step` | the next thing a proxy plugin does for one request, the log lines to write, and the walk to hand back with the report of what happened |
| `pass_through` | the target (the request path appended to the upstream's origin and path, trailing slashes dropped) and the request headers without the hop-by-hop set, `host` and `content-length`, and without an `authorization` equal to the placeholder, then `host` (with the port only when the upstream names one) and `content-length` set; a relative, non-http(s) or userinfo-carrying upstream, and a path not starting with `/`, are refused |
| `reply_headers` | the reply headers without the hop-by-hop set, and for a rewritten body also without `content-encoding` and `content-length` |
| `arrival` | refuse (with a 403 `proxyError` reply) when an `origin` header is present, whatever its value, or `host`, ignoring case, is neither `127.0.0.1:<port>` nor `localhost:<port>`; else health (200, `text/plain`, `ok`) for the path `/health` without its query; else route |
| `limits` | the largest request body, the 413 `proxyError` reply for a larger one, the wait for providers' models at a start or a request finding no model, the least time between that request's asks, and the interval of the periodic refresh |
| `port` | the port and the loopback address for an integer 1 to 65535; else a refusal that the setting is no port, or, given a listen failure, that the port cannot be listened on |
| `failure` | the reply: for an unreachable upstream a 502 `proxyError` naming the upstream's origin (scheme, host and port) and the detail, an unusable upstream refused as `pass_through` refuses it; for a failed request a 500 `proxyError` whose message is the detail, with a `warn` line `a request failed: <detail>` |
| `environment` | the environment an application starts with: the base URL, and with providers the model pairs named by the application's model variables and the placeholder token unless the application has a login of its own |

A refusal answers REFUSED with the refusal saying what to repair. Windows has no `regcomp`, so one matcher
serves every platform: `^ $`, literals, `.`, classes, `\d \w \s`, `* + ?` on one atom and at most one
capture group. A pattern using anything else is refused naming the construct. `notices` lists
`model_switched` when the default tier serves a model nothing offers (unless `native_model_pattern`
says it is the application's own), then `healed` when the chain's primary is derived; both are
listed when both apply, in that order. A model `unserved_model_pattern` matches counts as not offered, since the
profile never serves it.
The application is described to the library as data, and the library names no application: its `placeholder`
(variable and value), `own_login` (the variables that mean a login of its own and the state file that records
one) and the model variables (`model` for the default, and `tier_model` and `tier_model_name` templates in which every `{{TIER}}`
stands for the upper-cased tier, and a template without one is refused) arrive in the call.
It has a login of its own when one of those variables is non-blank, the placeholder variable holds anything
but the placeholder value, or the state file records an account.
`step` walks a request's chain without ever calling into the plugin: the plugin performs each step,
reports what happened, and hands back the walk it was given, an opaque `forebay_walk` the library owns, present
exactly when a report is expected. A walk never changes once answered, so it may be handed back more than once
and from any thread, and `release_walk` frees it only after every step it was handed to has returned. A step is pass through (no provider runs, or the request decoded as a pass-through),
refresh models (the chain is empty and the call was not `refreshed`), call provider (`provider`, `model`),
end stream (an IR error `event` to write after a stream failed past its first byte), reply or done.
A reply is `proxyError` 502 once every entry failed, or `apiError` 503 when nothing serves the model after a
refresh, each with a `message` the wire format encodes; provider failure (`status`, and the `headers`
`reply_headers` answers for a rewritten body) when a provider fails without rate limiting, sent with the
failure's own body; or rate limited (`headers`, its reset) in place of the 502 once any entry was rate
limited, carrying the last limited entry's headers and the latest reset any of them named. A report is
`response`, `streamed`, stream failed (`after_bytes`, `message`), `thrown` (`message`) or `failure`
(`status`, `headers`, `retry_after_milliseconds`, `now`, read as `outcome` reads them); a stream failing
before its first byte, a thrown call or a rate-limited failure falls to the next entry. An entry whose
provider is not in `providers` is skipped and logged, never called.
The step argument gives the login command and settings file the 503 names. The hop-by-hop set is one policy for both directions: `connection`, `keep-alive`, `proxy-connection`,
`transfer-encoding`, `upgrade`, `te`, `trailer`, every name starting `proxy-` and every name the `connection`
header lists, all compared ignoring case.
`test/vectors/routing/` holds the vectors, each from a basekit test or a rule of `ModelMap.java` or
`Router.java`; `routing_step_test` drives `step` call by call. A vector's `files` are laid out in a fresh
directory that becomes its `userHome`, or with `filesUnder` the value of the environment variable it names.
The vectors are JSON test data, turned into the definition's structs by a test-only reader that no shipped
binary links; a vector naming `"status": 1` is a call the definition refuses as MALFORMED.

## The accounts service

`accounts` holds the account selection, health reporting, backoff and store of basekit's `AccountManager` as
operations a provider plugin calls once per attempt; the attempt loop, the network and every
token stay in the provider. Every store function takes `store`, the path of the provider's `accounts.json`
(its `ctx.paths.state`). The file is `{ version: 1, accounts, activeIndex, activeIndexByLane }`, one pool per
provider. Time and randomness are arguments (`now`, `random` in [0, 1)).

| operation | answer |
| --- | --- |
| `list` | the accounts; a missing or unreadable store lists as empty |
| `upsert` | whether the account was added, updated or unchanged, matched by `id`, members the store does not know kept, compared with key order ignored and array order kept |
| `remove` | whether an account was removed |
| `acquire` | the claimed account's `id` and whether it is available now, after writing the cursor and `lastUsed` (not available, with its time, when hybrid claimed the soonest-free account while every account is cooling), else none and the next time any account frees, when one ever does; round-robin, sticky, hybrid, and any other strategy as hybrid |
| `report` | whether a success, a rate limit (its reset, else the backoff), an error, a disable (with its reason) or a quota reading changed the store |
| `token_expired` | whether a token has no expiry or expires within the skew of now, 60000 ms by default |
| `has_capacity` | whether any pool has a remaining fraction above 0 |

Backoff is `floor(raw / 2 + random * raw / 2)` with `raw = min(maxMilliseconds, baseMilliseconds * 2^attempt)`;
`random` defaults to 0.5. An `error` on a lane that holds an active reset changes nothing, and no `error` or `rate-limited` report shortens a cooldown already running, and a
`rate-limited` report with no lane becomes an account-wide cooldown, because selection only reads a lane's
reset for a named lane.

Tokens never reach the store: `upsert` refuses an account, and `report` refuses a quota reading, carrying a member named exactly `access`, `refresh`, `accessToken`, `refreshToken`, `idToken` or `apiKey` (case-sensitive, at any depth), before any copy of it exists; quota members such as `tokensRemaining` are stored. Every read-modify-write holds
`<store>.lock`, the OS file lock the runtime's registry takes, once, and the file is replaced atomically, so
two processes sharing one store lose no update. The store stays a JSON file, its format above; one that is not a JSON object answers a refusal and is
never overwritten. `test/vectors/accounts/` holds the vectors; `before` and `after` are the store, and `then`
chains further calls against it.

## The anthropic service

`anthropic` translates the Anthropic Messages wire format to and from the IR of `include/forebay/ir.h`, the port of
`anthropic-translator`. The wire body is the one place JSON is read and written: an operation takes bytes and answers
IR structs, or takes IR structs and answers bytes, and the IR crosses the definition as C structs, never as JSON.
Every body and every stream's text an encoder writes is answered as `forebay_anthropic_body_answer` and freed by the
one `release_body`.

| operation | does |
| --- | --- |
| `decode_request` | a request body to a `forebay_ir_request`: a string `content` or `system` becomes one text block flagged `$contentIsString` or `$systemIsString`, a member of the wrong type or a count outside 0 to 2^53 reads as absent, a stop sequence reads as JavaScript's `String` does, and every member nothing consumed is kept in `extensions`; a JSON root that is no object is the empty request; bytes that are not JSON are refused MALFORMED, saying to send a JSON body |
| `encode_request` | an IR request to a body, the members in the order the translator builds them and keyed as `JSON.stringify` orders them; `$` keys in `extensions` are left out, except a tool's and a tool choice's, which are written whole |
| `decode_response` | a response body to a `forebay_ir_response`: a stop reason the translator knows is kept and any other, or none, is `error`, with the wire's own in `$stopReasonRaw` beside `$stopSequenceRaw`, `$typeRaw` and `$roleRaw`; usage counts read as the request's counts do; a JSON root that is no object is the empty response |
| `encode_response` | an IR response to a body, `id`, `type`, `role`, `content`, `model`, `stop_reason`, `stop_sequence`, `usage`, then the wire members of `extensions`; the raw extensions restore what was decoded and `$` keys never reach the body |

A stream is read and written through a handle the caller opens and releases, one thread at a time:
`forebay_anthropic_stream_decoder` keeps the text of the frame not yet ended, so a piece may end anywhere, inside a
line, a frame or a CRLF. A feed that runs out of memory leaves its handle as it was, so the same piece may be fed again.

| operation | does |
| --- | --- |
| `stream_decoder_open`, `release_stream_decoder` | a new decoder, answered into the caller's empty handle (a NULL handle or one already set is refused MALFORMED); freeing one, NULL doing nothing |
| `stream_decode` | a piece of SSE text to the events of the frames it completed, as the translator's decoder reads them: a line ends at LF with one CR before it dropped, a frame at a blank line, its `data:` lines joined; comments, other fields, `ping`, frames of another type or of no JSON and deltas of another type yield nothing; a message delta keeps its stop sequence in `$stopSequenceRaw` |
| `stream_decode_finish` | a last piece, then the end: an unterminated last line and last frame yield as complete ones; the decoder is left empty for a new stream |
| `stream_encoder_open`, `release_stream_encoder` | a new encoder, answered as a decoder is; freeing one |
| `stream_encode`, `stream_encode_finish` | IR events to SSE text, `event:`, `data:` and a blank line each, with the translator's defaults for what an event leaves out; an event of a kind the header does not name is refused MALFORMED |

Three operations write the proxy's own bodies, each answered as a `forebay_anthropic_body_answer`:

| operation | does |
| --- | --- |
| `error_envelope` | an error type (not empty, such as `api_error` or `loader_proxy_error`) and a message to `{"type":"error","error":{"type":...,"message":...}}`, the message escaped as JSON text |
| `model_listing` | providers' models, in order, and the profile's default context and output limits to the `/v1/models` body: each id once as the first provider offers it, `display_name` its name or its id, `created_at` fixed, `max_input_tokens` and `max_tokens` the model's or the defaults, in `data` with `first_id`, `last_id` and `has_more` |
| `model_lookup`, `release_model_lookup` | the same providers and defaults and a URL-decoded id to the one listed model (`found` 1, answer 200) or the `not_found_error` body (`found` 0, answer 404) |

`test/vectors/anthropic/` holds the translator's vectors, copied unchanged; `anthropic_test` runs every `request-*` and
`response-*` one in both directions through the definition, `anthropic_stream_test` every `stream-*` one, a decode
also split in two at every byte and fed a byte at a time, and `anthropic_out_of_memory_test` fails each allocation of
every vector, singly and from that one on.
The test-only IR JSON codec the vectors are read with has its own `ir_codec_test` round trip and
`ir_codec_out_of_memory_test`.

## Building and testing

```
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

The shared C conventions come from `spisor/library`, built from the checkout under `siblings/library`
at the commit `repo.json` declares (CI checks it out; locally a link to the checkout does), and so do
weivel's `core`, `standard` and `c` under `siblings/`. The tests call the definitions through the built shared
library, one CTest executable per concern; `declaration_test` reads `library.json` with the weivel standard and
`weivel_host_load_test` loads the library through `weivel/c`'s host. The `layouts` program prints every public
struct's fields with offset, size and `sizeof`, held to the headers by `spisor/library`'s
`cmake/layouts_check.cmake`, and a translation of a header in another language is checked against it. The shared
library links `spisor_allocator`, the tests may take `spisor_allocator_counting`, and every out-of-memory test
requires an empty answer. CI runs six jobs: Debug under ASan and UBSan on Linux and macOS, Release on all three,
and Debug on Windows.

`contracts/` keeps the plugin contract schemas, with their vectors in `test/vectors/ir/`, as files; nothing here tests
them.

## Releasing

A `v*` tag runs `intisy/workflows`' `library.yml`, which builds `forebay_library-linux-x64.so` and
`forebay_library-win32-x64.dll`, and publishes them with `library.json` and `SHA256SUMS` as a GitHub release.
Publishing is halted, and that workflow cannot release the 0.2.0 declaration yet: it reads a top-level
`binary` the weivel standard's format does not have (spisor S-92). The releases below are the JSON builds.

### Released

| version | commit | asset | SHA-256 |
| --- | --- | --- | --- |
| `v0.1.0` | `f870fd9934e4664b1ab61151d6a91b1f9afc23d1` | `apps-linux-x64.so` | `c9fadf1be92669abff11b81a0ac3cf1d1a620b51a785be383a4836d62f9cba16` |
| | | `apps-win32-x64.dll` | `ab82e9fcce22f5c1e032c924f5deafdbd14421ab2c60026fc7f20165f8f7c59a` |
| | | `library.json` | `8bc33a51b755b8cb1dd167279fd2a53605c5de909d0150af1982f90177dd8b86` |
| `v0.1.1` | `3174ba4d874e01e008597bbd76f0b3a6bba478b1` | `apps-linux-x64.so` | `511a24739c7f05ff3c58d32171c42b3ee3c5bf840afad074aeaeba7d4d77f2cd` |
| | | `apps-win32-x64.dll` | `ecc2616750b6f1168c127ddf3619b9d46e06c4e47078155681558e565e5c5f0d` |
| | | `library.json` | `420e0f66f8b82b5ec4704c87089ec79caf9475e68262bd1b5878e079faefa2f6` |
| `v0.1.2` | `3f5c10b5b3cf932237e9612ace76440b7e894a1d` | `apps-linux-x64.so` | `ca9b8138b25db256f28324549b677fa920e700b09d948728a8ed94bb1bc868d5` |
| | | `apps-win32-x64.dll` | `f2d5554b3f232271bbb5ade94d4af5bd4f320748484a3cf9915b3c4975d974c9` |
| | | `library.json` | `7f08130d8b600a6e77260e5817c1338dc8707bcbec9003753b790806b7430264` |

`v0.1.1` takes the JSON grammar, the UTF-8 decoding and JavaScript's string rules from `spisor/library`
(`e048e09`) instead of carrying its own copies, raises out of memory where `v0.1.0` answered an empty
registry, and declares version `0.1.1`. The answers are otherwise those of `v0.1.0`.

`v0.1.2` matches environment names ignoring ASCII case on Windows, as `process.env` does there, so
a native variable spelled in another case is found, taking that from `spisor/library` (`21ce6f1`),
and declares version `0.1.2`.

A consumer pins the latest digests: <https://github.com/forebay/forebay-library/releases/tag/v0.1.2>.
