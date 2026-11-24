## What this is

The forebay ecosystem's C library, `forebay-library`. `library.json` declares it in the weivel standard's
format, `forebay-library` 0.2.0, with three services, `routing`, `accounts` and `anthropic`, each one function of kind
`definition` and its `description`, `forebay_<service>_definition_description`: the function's exported symbol
(`forebay_routing_definition_of`, `forebay_accounts_definition_of`, `forebay_anthropic_definition_of`)
answers a static C struct that begins with its `size`, then the provider's `context`, then one function pointer
per operation and one release per kind of answer, living until the library is unloaded. The structs are declared
in `include/forebay/routing.h`, `include/forebay/accounts.h` and `include/forebay/anthropic.h`, which document every operation, what it takes
and answers, and every refusal; this file says what they decide. No JSON crosses the boundary. The reply kinds both
`routing` and `anthropic` answer with are `include/forebay/reply.h`'s.

`library.json` also declares one contract, a definition this library describes and does not implement:
`forebay.provider`, whose description is `forebay_provider_definition_description`, generated from
`include/forebay/provider.h`. A provider plugin implements it: `models` answers its catalog entry (its models with
their limits, its ranking and its default model), `handle` serves an IR request as a whole response, an upstream
failure, a stream or the message of a call that got no upstream response (which a proxy reports to `step` as thrown), and `next` reads that stream event by event to its end or to the message of its failure, and `cancel` ends the call a
`handle` argument's `token` named (an unknown token is a no-op); `models`,
`handle` and `next` may block. Every header that includes `definition.h` declares a definition: with a
`forebay_<id>_definition_of` getter it is a service, and without one it is the contract `forebay.<id>`.

An operation takes the `context` and, when it reads anything, a size-prefixed argument struct; it fills the
caller's empty answer, or the consumer's empty handle for an operation opening one, or, when it refuses, the
caller's `spisor_refusal`, and answers a `spisor_status`, both from `spisor/library`'s
`spisor/status.h`. A size-prefixed struct is read only up to its `size` and only grows at its end; an array
element has no size, and changes only with the declaration's version. Text handed in is NUL-terminated UTF-8 borrowed for the call, and what an
answer points at is the provider's until the release for its kind. Data whose shape belongs to someone else (a
user's settings record, an account, a quota reading) crosses as a `spisor_value`. The operations read nothing of
the process they run in: time, randomness and every path arrive as arguments. Names are the operations' own,
in snake case.

## The routing service

`routing` holds the decisions of basekit's model map and router. The profile members it reads are
`forebay_routing_profile`, with the default token limits of a model that gives none, the providers' models a
catalog of `forebay_catalog_model` (each with its limits when it has them) and each provider's default model, and the
model map a slot per tier and the default, each served by `forebay_assignment`s (`provider`, `model`, `name`,
`derived`).

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
| `limits` | the largest request body, the 413 `proxyError` reply for a larger one (its kind, status and message), the wait for providers' models at a start or a request finding no model (a host reads it from `catalog`'s ask), the least time between that request's asks (`catalog`'s alone, never a host's), and the interval of the periodic refresh |
| `port` | the port and the loopback address for an integer 1 to 65535; else a refusal that the setting is no port, or, given a listen failure, that the port cannot be listened on |
| `failure` | the reply: for an unreachable upstream a 502 `proxyError` naming the upstream's origin (scheme, host and port) and the detail, an unusable upstream refused as `pass_through` refuses it; for a failed request a 500 `proxyError` whose message is the detail, with a `warn` line `a request failed: <detail>` |
| `model_entries` | every model id the catalog serves, once as the first provider offers it, with its name (the id when the catalog names none) and each limit the model's or the profile's default; the tier members are not read, so a profile with no tiers lists every model |
| `catalog` | what a host does about an event of its providers' catalog, so it keeps only the timers, the asks and the models: a change always starts a new round, superseding an open one: a change of providers (their ordered list, a reordering included), and a read whose list changed, ask every provider within `limits`' budget whether or not a round is open, and a late answer of the superseded round from a provider the new round does not ask is dropped; every request that wants the catalog sends a read, which otherwise waits while a round is open and else reads the catalog as it stands; the periodic refresh asks unless a round is open, which it waits for; a request finding no model does the same, but asks nothing within `limits`' interval of the last round's start; a failed ask keeps the provider's last models with the `warn` line `a provider answered no models: <message>`; an answer after its round closed, and a closed round, re-derive the model map; an answer from an implementation the current round did not ask is dropped (a failure of it still warned); a request that asked or waits reads the catalog only after the round's close and its re-derivation; a failed derivation keeps the last map with the `warn` line `the model map could not be derived, so routing keeps the last one: <message>` |
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
`test/vectors/routing/` holds the vectors, each from a basekit test, a rule of `ModelMap.java` or
`Router.java`, or a proxy plugin's profile (`model-entries-*`, OpenCode's); `routing_step_test` drives `step` call by call. A vector's `files` are laid out in a fresh
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
| `decode_request` | a request body to a `forebay_ir_request`: a string `content` or `system` becomes one text block flagged `$contentIsString` or `$systemIsString`, a member of the wrong type or a count outside 0 to 2^53 reads as absent, a stop sequence reads as JavaScript's `String` does, and every member nothing consumed is kept in `extensions`; a body that is no JSON this library can read (bad bytes, an empty body, a NUL in a string) or whose root is no object holds no request and answers the `FOREBAY_REPLY_INVALID_REQUEST` reply, status 400, with a message saying which; the client's headers answer as `forwarded_headers`, lower cased and less the 14 `NOT_FORWARDED` names (credentials a provider replaces, headers the transport sets, hop by hop headers), compared ignoring case |
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

Every body and lookup answer carries the reply it goes out in, so a proxy names no status, media type or header of
its own: `status` (0 where another decision names it, an error body's, or where the bytes are no reply, a request
body's), `content_type` and `headers`. A JSON body is `application/json`; a stream's every feed is a 200
`text/event-stream` reply with `cache-control: no-cache`; an encoded response and a listing are 200, and a lookup 200
or 404. An answer short of `header_count` gets the bytes alone.

Four operations write the proxy's own replies; the first three answer a `forebay_anthropic_body_answer`:

| operation | does |
| --- | --- |
| `error_envelope` | a reply kind and a message to `{"type":"error","error":{"type":...,"message":...}}`, the message escaped as JSON text and the type the kind's: `loader_proxy_error` for a proxy error, `api_error` for an API error and a provider failure, `invalid_request_error` for an invalid request, `rate_limit_error` for a rate limit |
| `model_listing` | the entries `routing.model_entries` answered, in order, to the `/v1/models` body: each entry as it is, `display_name` its name, `created_at` fixed, `max_input_tokens` and `max_tokens` its limits when it has them, in `data` with `first_id`, `last_id` and `has_more`; which models are listed, under which names and with which limits is routing's decision, never this one's |
| `model_lookup`, `release_model_lookup` | the same entries and a URL-decoded id to the first entry the id names (`found` 1, answer 200) or the `not_found_error` body (`found` 0, answer 404) |
| `rate_limit_reply`, `release_reply` | an upstream's own 429 (its headers, the latest reset, the clock's `now`, and the caller's zone name and its `offset_minutes` at the reset, since C does no zone lookup) to a `forebay_anthropic_reply_answer`: status 429, the upstream headers less `content-encoding`, `content-length` and the two `x-hub-` ones, the reset the latest of `reset_at` and the two `anthropic-ratelimit-unified-` reset headers, `content-type`, `retry-after` (seconds to the reset, else 60), and `anthropic-ratelimit-unified-status` and `-reset` when the upstream sent none, with the `rate_limit_error` body naming `h:mm AM` and the zone, or `try again later` for a reset not after `now` |
| `route_request`, `release_route` | a method and a path to what the proxy does with the request: `messages` for a POST to `/v1/messages`, `modelList` for a GET to `/v1/models` or `/v1/models/`, `modelLookup` with the id for a GET to `/v1/models/<id>`, else `passThrough`; the method is compared ignoring case and a query is cut off. It lives here and not in `routing` because the paths are Anthropic wire names |

`test/vectors/anthropic/` holds the translator's vectors, copied unchanged, and the `rate-limit-*`, `decode-*`, `route-*`, `envelope-*` and `reply-*` ones of the operations the proxy needs beside them;
`anthropic_test` runs every `request-*` and `response-*` one in both directions through the definition, each `rate-limit-*`
one through `rate_limit_reply`, each `decode-*` one through `decode_request` with its headers, each `route-*` one through `route_request`, each `envelope-*` one through `error_envelope`, each `reply-*` one through its direction with the reply its answer names, `anthropic_stream_test` every `stream-*` one, a decode
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
library, one CTest executable per concern; `declaration_test` reads `library.json` with the weivel standard,
requiring a service for every getter and a contract for every header that includes `definition.h` without one, and
`weivel_host_load_test` loads the library through `weivel/c`'s host, which checks every description with the
standard's check. Each description is generated at build time by the weivel standard's `weivel_describe` from
`spisor/status.h` and every `include/forebay` header, so a language that cannot read a C header binds the definition
from it, and `description_test` loads each one, a contract's among them, from the built library and runs the
standard's own check on it. The
symbol is the definition's name with `_description`, which is the name another library nesting a forebay definition
looks for. `forebay_library` links `weivel_standard` statically, and no other weivel library, and exports its
definitions, their descriptions and the contract's description and nothing else, which
`forebay_exports_only_definitions_test` holds.
`forebay_library_short_shape_test` and `forebay_library_empty_answer_test`, added by `spisor/library`'s
`spisor_add_described_checks`, walk those descriptions through the built library: every operation with each
size-prefixed argument and answer one byte short of its first shape, and every operation whose answer counts an array, text or
bytes with that count nonzero beside NULL storage, each refused MALFORMED with every answer untouched. They walk the
services alone: a contract has no definition here to call, so `description_test` and the `layouts` check are what
hold its description and its structs. The `layouts`
program prints every public
struct's fields with offset, size and `sizeof`, held to the headers by `spisor/library`'s
`cmake/layouts_check.cmake`, and a translation of a header in another language is checked against it. The shared
library links `spisor_allocator`, the tests may take `spisor_allocator_counting`, and every out-of-memory test
requires an empty answer. CI runs six jobs: Debug under ASan and UBSan on Linux and macOS, Release on all three,
and Debug on Windows.

### Bound from TypeScript and Java

`test/bindings/` proves that a language which cannot read a C header binds forebay-library from its descriptions
alone, through weivel's generic binding: no file translates a forebay header, and the tests name
`forebay_step_answer.walk` and `forebay_walk` only as strings, the data a check is about. `bindings_typescript` and `bindings_java`
load the built `forebay_library` from `library.json` through weivel/typescript's and weivel/java's hosts, given the
standard's check: `routing`'s `step` answers a request for a slot's model as a call of the chain's first entry with
the `forebay_walk` handle its report hands back, and a second `step` taking that walk and a report that the entry
threw answers a call of the next entry with a warning logged, every answer read as plain values; a copy of
`routing`'s description with `forebay_step_answer.walk` moved by 8 bytes is refused by each binding naming the member,
and the same copy unmoved binds. `bindings_typescript_cost` and `bindings_java_cost` print a
`spisor-bindings-measurement` line, the median time of a `routing.arrival` beside the binding's own echo benchmark,
which nothing judges.

weivel/typescript and weivel/java come from `siblings/typescript` and `siblings/java` at the commits `repo.json`
pins, or from `SPISOR_WEIVEL_TYPESCRIPT_DIR` and `SPISOR_WEIVEL_JAVA_DIR`, and are built into the build tree, never
into the checkout: the package's dependencies installed with `npm ci` and its sources compiled beside them, and the
jar built by the checkout's own Gradle wrapper into `test/bindings/weivel-java`. The TypeScript tests need the node
weivel/typescript's `engines` names and npm on the `PATH`, and the Java tests a JDK of release 22 or later at
`SPISOR_BINDINGS_JAVA_HOME`, which defaults to `JAVA_HOME`. Without them, without a checkout, on a sanitized build
or on a 32-bit platform each test is registered as skipped, saying why. Nothing under `test/bindings/` is linked
into, staged beside or installed with any shipped artifact: the core's `echo` the cost tests measure against is built
into `test/bindings/echo`, apart from the shipped library.

`contracts/ir.schema.json` keeps the IR's schema as a file, which `include/forebay/ir.h` follows member for member,
with its documents in `test/vectors/ir/`; nothing here validates against it.

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
