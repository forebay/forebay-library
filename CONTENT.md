## What this is

The forebay ecosystem's C libraries. Today it holds one, **`apps`**: basekit's app detection
(`getApps`, `getApp`, `resolveHome`, `currentAppId`) as a shared library a plugin reaches through the
`apps` service, so every weivel implementation answers exactly what basekit answers.

A function reads no state of the process it runs in. The registry location, the user's home, the
environment and the arguments arrive as the call's `context`, which the host fills from the facts
each function declares in `library.json`.

| function | symbol | context | argument | answer |
| --- | --- | --- | --- | --- |
| `list` | `apps_list` | environment, home | `{}` | `{ "apps": [appDescriptor, ...] }` |
| `get` | `apps_get` | environment, home | `{ "id": string }` | `{ "app": appDescriptor }`, or `{}` for an unknown id |
| `home` | `apps_home` | environment, home | `{ "id": string }` | `{ "home": string }`, or `{}` for an unknown id |
| `current` | `apps_current` | environment, home, arguments | `{}` | `{ "id": string }`, `""` when no app owns the process |

`appDescriptor` is the resolved descriptor the model publishes at
`https://weivel.github.io/model/plugin.schema.json#/definitions/appDescriptor`.

**What it is not.** It writes nothing: basekit's `registerApp` and `setAppPaths` are not here. It
keeps no cache either, so it answers what basekit answers on a cold cache; basekit keys its cache by
the registry path and mtime alone, so a second call with a different environment, or a write within
the same mtime, can answer stale there and never here.

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
| `outcome({ status, headers, now })` | `{ rateLimited, resetAt? }` from 429, `x-hub-rate-limited`, `x-hub-retry-after-ms`, `retry-after` |
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

## Where basekit answers empty and where it throws

An absent, unreadable or malformed registry, or one that is not an object, answers `{"apps": []}` with
status 0, as basekit's `readJson` fallback does. A failure object (`{message, fix}`, status 1) is
answered only for a malformed call or where basekit **throws**:

- a registry entry that is JSON `null` (basekit reads `entry.id` on it), for every function that reads
  the registry;
- `home` for an app whose candidates hold a non-string, or whose `xdgSubdir` is a non-string while
  `XDG_CONFIG_HOME` is set;
- `current` when any app's `detect.binary` is a non-empty non-string, or when the `HUB_CONFIG_DIR`
  match reaches an app with a non-string candidate or `xdgSubdir` before an earlier app matched;
- reading an unset environment variable named after an `Object.prototype` member (`toString`,
  `__proto__` and the rest), which basekit's plain-object lookup answers with a function and then
  fails to trim.

## How the registry text is read

basekit reads the file as UTF-8, strips whole-line comments with `/^\s*\/\/[^\n]*/gm`, and hands the
rest to `JSON.parse`. Each step is mirrored, in that order. The Node and JavaScript semantics come from
`spisor/library` (`spisor/javascript.h` and `spisor_parse_json`); only the comment regex, which is
basekit's own, lives here.

- **Invalid UTF-8 becomes U+FFFD** per maximal invalid subpart, as Node decodes it
  (`spisor_decode_utf8_lossy`), rather than failing; so does an unpaired surrogate escape, through
  `spisor_parse_json`.
- **The comment regex is JavaScript's**: its `\s` also eats a BOM, a no-break space, every other
  Unicode space, and whole earlier lines of nothing but whitespace (a vertical tab or form feed
  included) before the `//`; its `^` also holds after a CR, U+2028 or U+2029, while the comment runs
  to the next LF. Anything inside a stripped comment, a NUL or invalid bytes included, is gone before
  parsing.
- **The grammar is `JSON.parse`'s**: `spisor_parse_json` checks it before cJSON reads anything,
  because cJSON is laxer (a BOM, any byte up to 0x20 as whitespace, raw control characters in a
  string, `01` and `1.`). So a registry starting with a BOM answers `[]`, exactly as basekit's does.
- **Entries come out in `Object.entries` order**: array-index keys (`0` to `4294967294`, no leading
  zero, no sign) ascending, then every other key as written. A duplicated key keeps its first
  position and its last value.

## How a descriptor is resolved

As basekit's `build`: the id is `entry.id ?? key`; an entry is kept only with a non-empty string id, a
string label, and a home whose `candidates` is a non-empty array (its elements unchecked). Every `??`
default (`detect.binary` to the id, `detect.pkg` to `""`, `commandsSubdir` to `commands`, `proxyPort`
to 0, `integration` to `env-baseurl`, `wireFormat` to `anthropic`) treats JSON `null` as absent. A
declared value of another type is copied as it is. The optional fields (`icon`, `loader`, `usage`,
`accent`, `wrapperCommand`, `npmPlugins`, `startupHook`, `discovery`, `projects`, `modelCatalog`) are
copied raw when present, `null` included, and omitted when absent. Each storage name in `paths` is the
declared one, then the `HUB_<KIND>_SUBDIR` variable, then the default, taking the first that is a
string and, trimmed, one path segment (not empty, `.` or `..`, and holding no `/` or `\`).

A home field that names a variable (`envOverride`, `nativeEnv`) is keyed as JavaScript keys any
value (`spisor_javascript_string`): a number by its shortest decimal form, `true` as `true`, an
array by its join, an object as `[object Object]`. Paths are joined with the rules of the OS the binary runs on, as Node's
`path.join` there does; trimming is `String.prototype.trim` (`spisor_javascript_trim`).

## Recorded divergences

- **Lowercasing is ASCII only**, where basekit's `toLowerCase` is Unicode. `current` compares argument
  tokens and `HUB_CONFIG_DIR` paths case-insensitively, so a non-ASCII letter whose case differs, or
  the Kelvin sign and dotted capital I that lowercase to ASCII letters, can match in basekit and not
  here.
- **An escaped NUL (`\u0000`) anywhere in the registry answers `[]`**, where `JSON.parse` reads it: no
  C string can carry one.
- **Nesting deeper than cJSON's limit of 1000 answers `[]`**, where `JSON.parse` has no limit.
- **Out of memory is status 2**, never an answer, except for an allocation failing inside cJSON while
  it parses the registry, which `spisor_parse_json` cannot tell from a refusal and so reads as `[]`.
- **A relative candidate or override is checked against the working directory** of the process, as
  `existsSync` does; that is the one ambient fact a call depends on.

## Building and testing

```
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build --output-on-failure -C Debug
```

The shared C conventions come from `spisor/library` through `FetchContent`, pinned to commit
`21ce6f12ced44d3bb3f92aae72a2d0fa6e7bb6cd`. The tests call the exported functions through the built
shared library, one CTest executable per concern, on Windows and Linux in CI.

## Parity with basekit

`parity/` holds the released library to basekit's answers through both weivel hosts, on Windows and
Linux in CI (`parity.yml`). Each case in `parity/cases/` is a registry (`apps.json`, or the `files`
its `case.json` places), an environment, a home layout, an argument vector and a list of calls;
`{home}` and `{root}` stand for the directories the case is laid out in.

- `parity/oracle.mjs` runs every case through basekit's `getApps`, `getApp`, `resolveHome` and
  `currentAppId` at the pinned commit and writes `parity/expected/`, never committed, since paths
  differ per platform. basekit throwing from its own code is recorded as `{"fails": true}`; any
  other error stops the oracle.
- `parity/typescript` starts `weivel/typescript`'s host with the pinned release and a plugin requiring
  `apps`, and compares what the plugin observes with the expected answers.
- `parity/java` does the same through `weivel/java`'s `LibraryServices` and the `AppsService`
  generated from the `library.json` of the pinned release (fetched and held to the digest in
  `parity/pin.json`), reading each answer through the getters a Java plugin calls.
- `parity/divergences.json` lists every answer that is not basekit's, with its reason. Each is held
  both ways: the implementation must answer what the table records and basekit must answer something
  else. Beside the library's own divergences above, which include an unpaired surrogate escape
  read as U+FFFD, the rest are the Java host's: a Java data interface cannot tell a `null` optional
  field from an absent one, Gson reads a number into a `String` property as a string, and a getter
  whose value its type cannot hold (an unknown enum literal, a string `proxyPort`) refuses while
  the descriptor's other getters answer. The Java driver observes such a getter as
  `{"refused": true}`.
- `parity/oracle.mjs` hands basekit the case's environment with `process.env`'s matching on the
  platform it runs on, which on Windows ignores case.

The siblings are checked out under `siblings/` (CI pins each to a commit; locally a link to each
checkout does):

```
node parity/oracle.mjs
npm test --prefix parity/typescript
cd parity/java && ./gradlew test
```

## Releasing

A `v*` tag runs `intisy/workflows`' `library.yml`, which builds `apps-linux-x64.so` and
`apps-win32-x64.dll`, and publishes them with `library.json` and `SHA256SUMS` as a GitHub release.

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

A consumer pins the latest digests: <https://github.com/forebay/library/releases/tag/v0.1.2>. The
parity suite pins the same release, in `parity/pin.json`.
