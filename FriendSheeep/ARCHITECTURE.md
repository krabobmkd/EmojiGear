# FriendSheeep — Architecture & Roadmap

FriendSheeep is a Mastodon client for AmigaOS 3.x / 68k (m68k-amigaos,
68020+), sharing infrastructure with EmojiGear: `utf8rastport.library` for
UTF-8/emoji rendering, `unitexteditor.gadget` for UTF-8 text input, and the
BOOPSI main-window helpers from `EmojiGear/boopsimainwindow.c`.

## 1. Reference material

- **brutaldon** (`~/Code/Amiga/brutaldon`) — a minimal Django Mastodon web
  client, used by Mastodon.py. We mirror its data model and its sequence of
  Mastodon REST API calls (see `brutaldon/views.py`), not its Django/HTML
  parts.
- **testsocket** (`EmojiGear/testsocket/httpget.c`) — proven, builds and
  links on m68k-amigaos: opens `bsdsocket.library` + AmiSSL v5 manually
  (libnix-safe `errno` handling), and performs an HTTPS GET via
  `OSSL_HTTP_get()`.
- **EmojiGear** (`EmojiGear/EmojiGear`) — BOOPSI main window, menu, locale,
  settings, and process/CreateNewProc conventions to mirror.
- **AmigaDeveloperCD 2.1** (`~/Code/Amiga/AmigaDeveloperCD2.1`, includes
  `NDK3.9`) — authoritative `dos.library`/`exec.library` autodoc reference
  for `CreateNewProc()`, `MsgPort`/`Message` semantics, etc. used to confirm
  the IPC pattern in §5.

## 2. High-level architecture

```
 +-------------------------+        MsgPort          +---------------------------+
 |   FriendSheeep (GUI)     | <--------------------> |  fsnet network process     |
 |   friendsheep.c          |   FSNetMessage          |  (own CreateNewProc task)  |
 |                          |   request / reply       |                            |
 |  - Intuition/BOOPSI loop |                          |  - bsdsocket + AmiSSL v5   |
 |  - boopsimainwindow      |                          |  - cJSON encode/decode     |
 |  - post-render gadget    |                          |  - Mastodon REST calls     |
 |    (utf8rastport)        |                          |  - disk cache (T:)         |
 |  - unitexteditor compose |                          |                            |
 +-------------------------+                          +---------------------------+
```

- The GUI process never calls bsdsocket/AmiSSL directly and the network
  library never includes Intuition/BOOPSI/utf8rastport headers — the two
  halves are decoupled like a web frontend talking to a backend over HTTP,
  except the "wire protocol" is Exec messages.
- `fsnet` is built as a **static library** and linked into the
  `FriendSheeep` executable, but its code runs in a **separate AmigaDOS
  process** created with `CreateNewProc()`. The GUI gets back only a
  `struct MsgPort *` to post requests to.
- Replies are `PutMsg()`'d to a reply port supplied by the GUI in each
  request; the GUI integrates that port's signal bit into its main
  `Wait()` mask alongside the Intuition window signal (added in the GUI
  phase).

## 3. Mastodon API surface (from brutaldon)

Endpoints brutaldon exercises that FriendSheeep needs, in the order a
session uses them:

| Step | Endpoint | brutaldon ref |
|---|---|---|
| Register app | `POST /api/v1/apps` (`Mastodon.create_app`) | `views.py:428` |
| Authorize (user's browser) | `GET /oauth/authorize?...` | `views.py:454` |
| Exchange code | `POST /oauth/token` (`mastodon.log_in`) | `views.py:474` |
| Verify login | `GET /api/v1/accounts/verify_credentials` | `views.py:478` |
| Timeline | `GET /api/v1/timelines/{home,public}` | `views.py:215` |
| Notifications | `GET /api/v1/notifications` | `views.py:116` |
| Post status | `POST /api/v1/statuses` | `views.py:937` |
| Upload media | `POST /api/v2/media` | `views.py:912` |
| Fav/boost/delete/follow | `POST /api/v1/statuses/:id/{favourite,reblog}`, etc. | `views.py:1249+` |
| Filters | `GET /api/v1/filters` | `views.py:262` |

### Core entities (subset to model in C structs)

- **Status** (toot): `id`, `created_at`, `content` (HTML), `account`,
  `media_attachments[]`, `visibility`, `spoiler_text`, `sensitive`,
  `in_reply_to_id`, `reblog`, `favourited`, `reblogged`,
  `favourites_count`, `reblogs_count`, `replies_count`.
- **Account**: `id`, `username`, `acct`, `display_name`, `avatar`/
  `avatar_static`, `note` (bio HTML), `followers_count`, `following_count`,
  `statuses_count`.
- **MediaAttachment**: `id`, `type` (image/video/gifv/audio), `url`,
  `preview_url`, `description`.
- **Notification**: `id`, `type`, `account`, `status`, `created_at`.
- **Client** (our equivalent of brutaldon's `Client` model): `api_base_url`,
  `client_id`, `client_secret`, `access_token` — persisted in an
  `ENV:`/`ENVARC:` or icon tooltype-style settings file (mirrors
  `tooltypepref.c` in EmojiGear).

`content` is HTML (Mastodon strips it down to a small safe subset: `<p>`,
`<br>`, `<a>`, `<span class="...">` for emoji/mentions/hashtags). The GUI
phase needs a small HTML-subset-to-runs-of-text converter feeding
utf8rastport — **not** a general HTML renderer.

## 4. Networking feasibility

### 4.1 GET — proven

`testsocket/httpget.c` already builds (`testsocket/build-Amiga/httpget`,
5120 bytes) and demonstrates: opening `bsdsocket.library` v4 +
`amisslmaster.library`, `OpenAmiSSLTags()` with the libnix-safe
`AmiSSL_ErrNoPtr, &errno` (the manual-init path — `USE_AUTOINIT` is
incompatible with libnix' `errno` macro), and `OSSL_HTTP_get()` with custom
headers (`User-Agent`, `Authorization: Basic ...`) via
`X509V3_add_value()`.

This covers: timelines, notifications, verify_credentials, fetching
avatar/media URLs — all with `Authorization: Bearer <token>` added the same
way as the Basic-auth example.

### 4.2 POST — needs a small spike, looks feasible

`OSSL_HTTP_get()` is GET-only. AmiSSL v5's `<openssl/http.h>` (present in
`amigacommonlibs/extrasdk/AmiSSL/Developer/include/openssl/http.h`) exposes
the lower-level API needed for POST:

- `OSSL_HTTP_open()` → `OSSL_HTTP_REQ_CTX *`
- `OSSL_HTTP_REQ_CTX_set1_req(rctx, content_type, req_bio)` to attach a
  request body (e.g. `application/x-www-form-urlencoded` for
  `/oauth/token` and `/api/v1/apps`, or `application/json` for
  `/api/v1/statuses`)
- `OSSL_HTTP_exchange()` to send and get the response `BIO *`
- or the all-in-one `OSSL_HTTP_transfer()` helper, which takes `headers`,
  `content_type`, and `req` BIO directly — closest drop-in replacement for
  `OSSL_HTTP_get()` but with POST support.

**Phase 1 spike**: write `fsnet_http.c` with `FSHttp_Get()` and
`FSHttp_Post()` wrappers around `OSSL_HTTP_transfer()`, and validate against
a real Mastodon instance:
1. `POST /api/v1/apps` (form-encoded) → parse `client_id`/`client_secret`.
2. `POST /oauth/token` (form-encoded, with a manually-obtained `code`) →
   parse `access_token`.
3. `GET /api/v1/accounts/verify_credentials` with `Authorization: Bearer`
   → parse the account JSON.

If `OSSL_HTTP_transfer()` proves awkward on this AmiSSL build, fall back to
`OSSL_HTTP_open()` + `OSSL_HTTP_REQ_CTX_*` directly (same underlying
mechanism, more manual).

### 4.3 JSON

cJSON is already vendored in sibling Amiga projects
(`~/Code/Amiga/aukadicty/cjson`, `~/Code/Amiga/amigatests/boopsiwizard/cjson`
— `cJSON.c`/`cJSON.h`, no dependencies beyond libc). Plan: copy
`cJSON.c`/`cJSON.h` into `FriendSheeep/network/cjson/` and use it for both
encoding request bodies and decoding all Mastodon responses.

### 4.4 Login flow on Amiga (no embedded browser)

Mastodon's OAuth2 `authorization_code` flow expects the user to authenticate
in a browser and be redirected back with a `code`. Classic desktop-client
approach, adapted here:

1. FriendSheeep calls `POST /api/v1/apps` with
   `redirect_uris=urn:ietf:wg:oauth:2.0:oob` (the standard
   "out-of-band" redirect URI Mastodon supports for non-web clients).
2. FriendSheeep shows the resulting `/oauth/authorize?...` URL to the user
   (selectable text, so it can be copied to a clipboard and opened in any
   browser, e.g. on another machine, or OWB if available).
3. The user logs in/authorizes in that browser; Mastodon then **displays**
   the `code` on screen (rather than redirecting), because the redirect URI
   is the OOB sentinel.
4. The user types/pastes that `code` back into a FriendSheeep text field
   (the `unitexteditor` gadget, single line).
5. FriendSheeep does `POST /oauth/token` with that code →
   `access_token`, then `GET /api/v1/accounts/verify_credentials` to confirm
   and fetch the logged-in account.

This needs **zero** local HTTP listener and **zero** browser embedding —
only the POST support from §4.2.

## 5. IPC protocol (`FriendSheeep/network/fsnet.h`)

- `FSNetMessage` — an Exec `struct Message` extended with a request/reply
  type tag (`fsm_Type`, see `enum FSNetRequestType`), a result code
  (`fsm_Result`, see `enum FSNetResult`), and an `AllocVec()`'d payload
  (`fsm_Data`/`fsm_DataLen`) whose ownership transfers to whichever side
  receives the message (receiver `FreeVec()`s it).
- `FSNet_Start()` creates the network process via `CreateNewProc()` and
  returns its request `struct MsgPort *` once a startup handshake (a
  stack-allocated `struct Message` + `WaitPort()`) confirms the process is
  ready. `FSNet_Stop()` sends `FSNETQ_SHUTDOWN` and waits for the process to
  exit and free its own port.
- Request types beyond `FSNETQ_SHUTDOWN` (`FSNETQ_LOGIN_START`,
  `FSNETQ_LOGIN_FINISH`, `FSNETQ_TIMELINE`, `FSNETQ_POST_STATUS`,
  `FSNETQ_FETCH_IMAGE`, ...) are declared in `fsnet.h` now but dispatched as
  stubs (`FSNETR_OK`, no-op) until Phase 2 — this lets the GUI and protocol
  headers stabilize before the HTTP/JSON plumbing lands.

## 6. Media/icon cache

The network process owns a disk cache under `T:FriendSheeep/` (one file per
avatar/media URL, named by a hash of the URL). `FSNETQ_FETCH_IMAGE` either
returns the existing cache path immediately or downloads first. The GUI asks
`datatypes.library` (as EmojiGear already does for images) to load the
cached file once notified — the network library never touches datatypes or
any GUI library.

## 7. GUI (later phases, not started)

- BOOPSI main window via `BoopsiMainWindow` (reuse
  `EmojiGear/boopsimainwindow.c`/`.h` as-is or lightly extended).
- A private BOOPSI gadget class (`postview.gadget`-style, statically linked
  like `unitexteditor`) that renders one `Status` (avatar thumbnail,
  display name + handle, content runs, media placeholders, action row) via
  `utf8rastport.library`.
- `unitexteditor.gadget` reused verbatim for the compose/reply box and for
  the OAuth-code paste field.
- `egmenu`/`eglocale`/`egsettingsview`-style modules reused for menus,
  translations, and the account/instance settings panel.

## 8. Roadmap

- **Phase 0 — Architecture** (this document + CMake scaffolding): done.
- **Phase 1 — Network feasibility spike**
  - Vendor cJSON into `network/cjson/`.
  - `fsnet_http.c`: `FSHttp_Get`/`FSHttp_Post` over `OSSL_HTTP_transfer()`
    (or `OSSL_HTTP_open`/`exchange` fallback), with custom headers.
  - `fsnet_mastodon.c`: `create_app` → OOB authorize URL → code exchange →
    `verify_credentials`, validated against a real instance from a small
    CLI test tool (like `testsocket`).
- **Phase 2 — Network process & protocol**
  - Flesh out `FSNet_ProcEntry()` dispatch for all `FSNetRequestType`s.
  - Timeline fetch, status post, media upload, notifications, action calls
    (fav/boost/delete/follow).
  - Disk cache for avatars/media under `T:FriendSheeep/`.
- **Phase 3 — Minimal text GUI**
  - BOOPSI window, login dialog (instance name + OOB code paste), timeline
    as a plain-text list, manual refresh, basic compose (status text only).
- **Phase 4 — Rich rendering gadget**
  - Private BOOPSI post-view class via `utf8rastport.library`
    (avatars/emoji/media thumbnails), HTML-subset → text-run conversion for
    `content`.
  - Full compose/reply via `unitexteditor.gadget` (spoiler text, visibility,
    media attach).
- **Phase 5 — Feature completeness**
  - Notifications, favourites/boosts/threads, search, multiple accounts,
    filters, settings/preferences, themes (mirroring brutaldon's
    `Preference`/`Theme` models).

## 9. Open questions

- Which Mastodon instance + test account should Phase 1's spike target?
- Minimum AmigaOS/CPU target — assuming OS3.x / 68020+ (matches
  `testsocket` and EmojiGear's `-mcpu=68020`); confirm before picking
  cJSON build flags (`-Os`, no FPU-heavy code paths).
- Should media thumbnails be decoded via `datatypes.library` (PNG/JPEG) in
  the GUI process, same as EmojiGear's emoji handling, or do we need a
  fallback decoder for animated GIFs/`gifv` previews?
