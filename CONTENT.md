## What this is

The forebay ecosystem's C library, `forebay-library`. It declares two services in `library.json`,
`routing` and `accounts`, each a set of synchronous `json` functions that read nothing of the process
they run in: time, randomness and every path arrive as arguments. Every answer is a buffer the caller
frees with `forebay_library_release`, the library's one release symbol.

## The routing service

`routing` holds the decisions of basekit's model map and router as synchronous `json` functions that
read nothing of the process: time and the filesystem are arguments. `profile` has the shape of
`contracts/front-door.schema.json#/definitions/Profile`, `models` is basekit's `models.json`
(`{ <provider>: { models, ranking } }`) and `map` is `{ <tier>|default: Assignment[] }` with
`Assignment = { provider, model, name, derived }`.

| function | answer |
| --- | --- |
| `tiers({ profile, models })` | `{ tiers }`: `tierRegex` group 1 over the tier source's ranking, ordered by `tierOrder`, unknown last |
| `modelMap({ profile, stored, models })` | `{ map, healed }`: stored entries kept, healed within their provider, or derived |
| `resolve({ profile, map, models, requestedModel })` | `{ chain, notices }`: exact id, tier keyword, an offered model, then the default |
| `outcome({ status, headers, now, retryAfterMilliseconds? })` | `{ rateLimited, resetAt? }`: rate limited on 429; the reset is `retryAfterMilliseconds` when given, else the `retry-after` header in seconds |
| `environment({ profile, map, port, providersEnabled, environment, userHome })` | `{ environment }`: the base URL, and with providers the model pairs and the placeholder token unless the application has a login of its own |

A refusal is a successful answer `{ "refused", "fix" }`. Windows has no `regcomp`, so one matcher
serves every platform: `^ $`, literals, `.`, classes, `\d \w \s`, `* + ?` on one atom and at most one
capture group. A pattern using anything else is refused naming the construct. `notices` lists
`model_switched` when the default tier serves a model nothing offers (unless `nativeModelPattern`
says it is the application's own), then `healed` when the chain's primary is derived; both are
listed when both apply, in that order.
The application has a login of its own when its environment carries a non-blank `ANTHROPIC_API_KEY` or
`CLAUDE_CODE_OAUTH_TOKEN`, or an `ANTHROPIC_AUTH_TOKEN` other than the placeholder, or when `.claude.json`
records an `oauthAccount`: the one in `CLAUDE_CONFIG_DIR` when that is set, else the one in `userHome`, the
file Claude Code itself reads.
`test/vectors/routing/` holds the vectors, each from a basekit test or a rule of `ModelMap.java` or
`Router.java`. A vector's `files` are laid out in a fresh directory that becomes its `userHome`, or with
`filesUnder` the value of the environment variable it names.

## The accounts service

`accounts` holds the account selection, health reporting, backoff and store of basekit's `AccountManager` as
synchronous `json` functions a provider plugin calls once per attempt; the attempt loop, the network and every
token stay in the provider. Every store function takes `store`, the path of the provider's `accounts.json`
(its `ctx.paths.state`). The file is `{ version: 1, accounts, activeIndex, activeIndexByLane }`, one pool per
provider. Time and randomness are arguments (`now`, `random` in [0, 1)).

| function | answer |
| --- | --- |
| `list({ store })` | `{ accounts }`; a missing or unreadable store lists as empty |
| `upsert({ store, account })` | `{ outcome: "added" or "updated" or "unchanged" }`, matched by `id`, members the store does not know kept, compared with key order ignored and array order kept |
| `remove({ store, id })` | `{ removed }` |
| `acquire({ store, lane?, strategy, now })` | `{ id, available, availableAt? }` after writing the cursor and `lastUsed` (`available: false` with the account's `availableAt` when hybrid claimed the soonest-free account while every account is cooling), else `{ none: true, nextAvailableAt? }`; `round-robin`, `sticky`, `hybrid`, anything else as `hybrid` |
| `report({ store, id, lane?, outcome, now, ... })` | `{ changed }` for `success`, `rate-limited` (`resetAt`, else the backoff), `error`, `disabled` (`reason`) or `quota` (`quota`) |
| `tokenExpired({ expires?, now, skewMilliseconds? })` | `{ expired }`, 60000 ms of skew by default |
| `hasCapacity({ pools })` | `{ capacity }`: any pool with `remainingFraction` above 0 |

Backoff is `floor(raw / 2 + random * raw / 2)` with `raw = min(maxMilliseconds, baseMilliseconds * 2^attempt)`;
`random` defaults to 0.5. An `error` on a lane that holds an active reset changes nothing, and no `error` or `rate-limited` report shortens a cooldown already running, and a
`rate-limited` report with no lane becomes an account-wide cooldown, because selection only reads a lane's
reset for a named lane.

Tokens never reach the store: `upsert` refuses an account, and `report` refuses a `quota`, carrying a member named exactly `access`, `refresh`, `accessToken`, `refreshToken`, `idToken` or `apiKey` (case-sensitive, at any depth) as a successful `{ refused, fix }` answer; quota members such as `tokensRemaining` are stored. Every read-modify-write holds
`<store>.lock`, the OS file lock the runtime's registry takes, once, and the file is replaced atomically, so
two processes sharing one store lose no update. A store that is not a JSON object answers a refusal and is
never overwritten. `test/vectors/accounts/` holds the vectors; `before` and `after` are the store, and `then`
chains further calls against it.

## Building and testing

```
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

The shared C conventions come from `spisor/library`, built from the checkout under `siblings/library`
at the commit `.weivel/repo.json` declares (CI checks it out; locally a link to the checkout does). The
tests call the exported functions through the built shared library, one CTest executable per concern,
on Windows, Linux and macOS in CI; `release_test` runs under the address sanitizer where the compiler
has one.

`parity/typescript` holds the contract schemas in `contracts/` to their vectors in `test/vectors/ir/`
(`parity.yml`):

```
npm ci --prefix parity/typescript
npm test --prefix parity/typescript
```

## Releasing

A `v*` tag runs `intisy/workflows`' `library.yml`, which builds `forebay_library-linux-x64.so` and
`forebay_library-win32-x64.dll`, and publishes them with `library.json` and `SHA256SUMS` as a GitHub release.

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
