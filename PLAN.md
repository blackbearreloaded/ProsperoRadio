# PS5 Radio Browser

> Implementation status (PPSA99768): the completed sibling LVGL application
> has now been ported to RmlUi. The original phased plan below is retained as
> design history; current architecture, controls, validation, and the one
> remaining international-text boundary are documented in
> [docs/LVGL_PORT.md](docs/LVGL_PORT.md).

## Current codec and stream roadmap

The shipped playback baseline is AAC over continuous HTTP streams. Radio
Browser catalog requests remain restricted to formats that the player can
actually decode; a format must pass on-device playback, stop, reconnect, and
error-recovery checks before its stations are exposed in normal browsing.

### Stage 1: MP3

- introduce codec detection and a shared compressed-audio-to-PCM boundary;
- keep the existing native AAC path unchanged behind that boundary;
- add an open-source distributable MP3 decoder, frame synchronization, and
  stream metadata handling;
- expose AAC and MP3 stations only after representative MP3 streams pass the
  PS5 investigation loop.

This is the next codec milestone because it reuses the existing continuous
HTTP, buffering, PCM, and AudioOut paths.

### Stage 2: Ogg Vorbis and Ogg Opus

- add the minimal Ogg demuxing needed for live radio streams;
- integrate open-source Vorbis and Opus decoders with compatible licenses;
- normalize decoded channel layouts and sample rates for the existing PS5
  output path;
- validate malformed pages, reconnects, long-running playback, and station
  switching before enabling each codec in catalog results.

Vorbis and Opus share an Ogg-container milestone but remain independently
gated capabilities.

### Stage 3: FLAC

- add a small open-source FLAC decoder;
- size compressed and PCM buffers for higher-bitrate lossless streams;
- verify memory use, underrun behavior, and sustained playback on PS5;
- expose FLAC stations only when their higher resource cost is acceptable.

### Stage 4: HLS and playlist delivery

- treat HLS as a delivery-protocol feature rather than another codec;
- parse master and media playlists, fetch segments, and follow live playlist
  updates;
- support only already-validated codecs inside HLS segments;
- add cancellation, variant selection, discontinuity, retry, and stale-segment
  handling;
- separately cover simple M3U/PLS playlist resolution where Radio Browser does
  not provide a usable direct stream URL.

### Completion criteria

For every newly supported format:

- use only dependencies whose licenses permit redistribution with the app;
- advertise only formats enabled in the packaged build;
- preserve responsive controller input during buffering and decoding;
- handle unsupported containers or codec variants without crashing;
- document and commit the decoder choice, licenses, test streams, and PS5
  hardware results as one milestone.

Detailed implementation plan for a native PS5 internet-radio application using
the Radio Browser API, SDL2, and RmlUi RML/RCSS documents.

## 1. Project context

Project directory:

```text
C:\Users\denis\Documents\PS5\workspace\dev\ps5-radio-browser
```

Starting template:

```text
C:\Users\denis\Documents\PS5\workspace\dev\ps5-native-app-boilerplate
```

The project directory is a clean working copy of the boilerplate. Generated
`.deps`, `.local`, `build`, and `dist` directories were intentionally not copied.
The original boilerplate remains untouched and is the source of future template
updates.

PS5 RmlUi build reference:

```text
C:\Users\denis\Documents\PS5\workspace\others\pacbrew-repo\rmlui\PKGBUILD
```

That recipe currently targets RmlUi 6.2 and builds it for PS5 with the PacBrew
SDL2, SDL2_image, FreeType, LuaJIT, and libc++ packages. SDL2 itself is provided
by the PS5 payload SDL package.

Audio research reference:

```text
codex://threads/01a02b59-e3af-74c3-b87f-fccd5d336314
```

The current research identifies the native PS5 audio stack and likely codec
paths, but it does not yet prove the complete radio-stream pipeline. The audio
implementation must therefore remain behind a small boundary until a focused
runtime test confirms the decoder and output path.

## 2. Product goal

Build a controller-first native PS5 internet-radio app that can:

1. Start in a stable PS5 title environment.
2. Render a readable 10-foot UI using RmlUi RML/RCSS.
3. Browse and search Radio Browser stations.
4. Play a supported live station stream.
5. Show the selected station and basic now-playing state.
6. Save a small favorites list locally.

The Radio Browser API is the source of station metadata and stream URLs. A local
cache may be added later for resilience, but a complete station database is not
required for the first working version.

## 3. Deliberate first milestone

The first implementation milestone is only:

> Render one RmlUi document through SDL on top of the PS5 boilerplate, accept
> controller input, and prove that the native title can repeatedly launch and
> exit without a crash.

It will use mock station data. It will not include HTTP, JSON parsing, audio
decoding, favorites persistence, artwork downloads, or the full station catalog.

This keeps the first hardware run useful: if the screen does not render, adding
networking or audio would only make the failure harder to diagnose.

## 4. Technical boundaries

### Application layer

The application is native C/C++. The boilerplate currently starts from
`src/main.c`; the first C++ implementation step will change the entry source to
`src/main.cpp` and update `project.json` accordingly.

Keep the application small at first. Do not introduce a general-purpose clean
architecture, dependency-injection framework, or large event bus.

Initial native responsibilities:

- application lifetime and the main loop;
- SDL initialization, events, timing, and window/surface ownership;
- RmlUi context and document ownership;
- a small UI state/action bridge;
- later, Radio Browser requests and audio playback.

### SDL and RmlUi

SDL owns the platform-facing window, input events, timing, and the initial
presentation path. RmlUi owns document layout, styles, focus navigation, and DOM
events.

RmlUi is renderer-agnostic. Linking SDL2 alone is not enough: the application
must provide an RmlUi `SystemInterface` and `RenderInterface`, or reuse a
PS5-compatible implementation if one is already available in the local
toolchain. The first technical risk to resolve is the simplest renderer that
works with the installed PS5 SDL backend.

The first render proof should contain only:

- a solid background;
- a title;
- one focused button;
- one station card with mock data;
- a visible focus change when the DualSense directional input is pressed.

### UI/backend contract

RML documents should express presentation and user intent only. They must not
perform synchronous HTTP calls, decoder work, file I/O, or PS5 SDK calls.

Use a small state/action shape:

```text
UiState
  route: Boot | Browse | Search | Player | Favorites | Error
  focused_id
  stations[]
  selected_station
  playback_state
  error_message

UiAction
  Navigate(route)
  Focus(id)
  Search(query)
  SelectStation(station_uuid)
  Play(station_uuid)
  Stop
  ToggleFavorite(station_uuid)
  Back
```

The RmlUi bridge converts DOM events into `UiAction` values. The native layer
updates `UiState`; the bridge then refreshes the document. Long-running work is
performed outside the UI loop and reports completion, progress, or failure back
to the state owner.

## 5. Radio Browser integration

### Source and API behavior

Use the official Radio Browser API rather than copying a third-party station
database into the repository.

Required API behavior:

- discover or rotate API mirrors rather than permanently hardcoding one server;
- send a descriptive application `User-Agent`;
- use `hidebroken=true` for normal browsing/search;
- use `stationuuid` as the stable station identifier;
- prefer `url_resolved`, falling back to `url` when needed;
- send a station click notification when playback begins;
- treat station URLs, favicons, and metadata as untrusted remote input;
- keep HTTP and JSON parsing off the UI thread;
- surface timeout, TLS, DNS, HTTP, parse, and stream errors distinctly enough
  for the UI to explain what failed.

### Station model

The first native model should contain only fields needed by the UI and player:

```text
station_uuid
name
stream_url
resolved_stream_url
homepage_url
favicon_url
tags
country_code
language
codec
bitrate
```

Do not mirror every Radio Browser field until a feature needs it.

### HTTP and JSON dependencies

Use the existing PacBrew packages where possible:

- libcurl for HTTPS/HTTP;
- the existing PS5 CA-bundle support from the curl package;
- `json-c` or `jansson` for API responses, selected after the first link test.

Do not add a second networking stack or write a custom JSON parser. If the
existing package link is awkward, resolve that build issue before adding more
application code.

### Local persistence

Start with a small local JSON file for favorites and the last selected station.
SQLite is deferred until measured requirements justify it. The complete Radio
Browser catalog should remain remote and paginated; downloading tens of
thousands of rows into the title is unnecessary for the first release.

If offline browsing becomes a product requirement, add a build-time curated
station snapshot or a bounded cache later. That cache should be an optimization,
not a second source of truth with an independent schema.

## 6. Audio plan

Audio is a separate workstream from the UI and Radio Browser client.

### Research-backed starting point

The prior investigation identified these important PS5 targets:

- `libSceAvPlayer.native.sprx`;
- `libSceAudiodec.native.sprx`;
- `libSceAjm.native.sprx`;
- `libSceAudiodecCpuM4aac.sprx` as a CPU AAC control path;
- the AJM/AJMI libraries and their batch decode functions.

The key unanswered runtime question is whether the selected hardware path
actually reaches AJM for the codecs needed by radio streams, and which stream
formats can be decoded reliably in this application context.

### Audio milestones

1. Prove an audio-output path independently of Radio Browser.
2. Decode one known local or controlled sample.
3. Confirm one supported compressed format end to end.
4. Feed decoded PCM through a bounded ring buffer into the SDL/PS5 output path.
5. Replace the controlled sample with one Radio Browser stream.
6. Add buffering, reconnect, stop, and stream-error handling.

Do not commit to FFmpeg, `SDL_mixer`, or a large decoder dependency before the
native PS5 decoder path is tested. Radio streams vary in codec, container,
redirects, playlists, and metadata; the supported-format list must be based on
observed behavior rather than assumptions.

The UI should see only player state such as `Idle`, `Connecting`, `Buffering`,
`Playing`, `Stopped`, and `Error`. It should not know whether the decoder is
AJM, Audiodec, FFmpeg, or another implementation.

## 7. UI scope and design order

### First visual pass

Build a static RML/RCSS shell with mock data before wiring the API:

- boot/loading state;
- browse page with a small station list;
- focused station card;
- player strip with play/stop state;
- error/empty state;
- favorites affordance, initially mocked.

Use the existing `moonlight-ps5-rmlui-ui` project as a design and interaction
reference, especially its controller-first focus treatment, safe-area thinking,
RmlUi/C++ boundary, and reusable component approach. Do not copy its Moonlight
screens or create a second UI framework.

### Controller behavior

- D-pad moves focus predictably.
- Cross activates the focused control.
- Circle backs out of a nested route or closes a modal.
- Options opens the app menu only when a menu exists.
- Focus is restored after a modal or player overlay closes.
- No action should require a mouse or keyboard on the target.

### Visual constraints

- design reference: 1920x1080;
- use safe-area insets;
- readable typography at television distance;
- one clear accent color for focus and primary actions;
- low-glare dark background;
- explicit loading, empty, offline, and error states;
- no animation that blocks controller input.

Do not build country/tag filters, station artwork grids, timers, accounts,
lyrics, recommendations, or complex settings until the basic browse-and-play
loop is working.

## 8. Repository layout after implementation begins

Keep the initial tree small and add directories only when their first feature
needs them:

```text
ps5-radio-browser/
  PLAN.md
  project.json
  build.ps1
  src/
    main.cpp
    app_state.hpp/.cpp
    ui_bridge.hpp/.cpp
    radio_browser_client.hpp/.cpp       # later
    station_store.hpp/.cpp              # later
    audio_player.hpp/.cpp               # later
  ui/
    main.rml
    styles/
      tokens.rcss
      app.rcss
    assets/
  data/
    favorites.json                      # later
  docs/
    RUNTIME_NOTES.md                    # later
    AUDIO_NOTES.md                      # later
```

The exact split may be simplified if one file is still clearer. The directory
layout is a guide, not a requirement to pre-create empty abstractions.

## 9. Implementation phases

### Phase 0: Preserve and baseline the template

Status: project copy created; doctor and clean-room baseline build pass;
on-device launch remains pending.

- keep the source boilerplate unchanged;
- assign a unique title name, title ID, concept ID, and content ID for this app;
- run the boilerplate doctor script;
- build the untouched copied project;
- deploy the unchanged title and confirm the known notification/lifetime path;
- record the working compiler, SDK, PacBrew, and loader revisions.

Exit condition: the copied project builds and launches before RmlUi or SDL
changes are introduced.

### Phase 1: Static RmlUi design

Status: initial mock station UI created; visual review remains pending.

- create the initial RML/RCSS documents and mock station data;
- define page shell, card, button, focus, player strip, loading, empty, and
  error primitives;
- review the visual direction before adding real data operations;
- keep markup independent from the PS5 APIs.

Exit condition: the UI design is coherent enough to implement in the PS5
runtime, with controller focus behavior explicitly represented.

### Phase 2: SDL + RmlUi PS5 smoke test

Status: SDL + RmlUi link and folder package pass; on-device render/input
validation remains pending.

- convert the entry point to C++;
- add SDL2 headers and static archives through the boilerplate project config;
- initialize SDL video, input, and timing;
- initialize RmlUi with the required system and render interfaces;
- stage the RML/RCSS/assets into the title output;
- render the static document on PS5;
- map basic DualSense input into SDL/RmlUi events;
- validate repeated launch, render, focus movement, Cross activation, Circle
  back, and clean shutdown behavior.

Exit condition: a target run visibly renders the RmlUi document and responds to
the controller. No Radio Browser or audio code is required for this gate.

### Phase 3: Radio Browser client

- add libcurl and a JSON library through the existing PS5 package system;
- implement server discovery/fallback and a descriptive `User-Agent`;
- implement paginated top-station browsing;
- implement name search;
- map API responses into the small native station model;
- add background request execution and cancellation;
- expose loading, empty, error, and retry states to RmlUi;
- send click notifications after a station is successfully selected for play.

Exit condition: the PS5 app can browse and search live station metadata without
blocking the UI or crashing on a failed server.

### Phase 4: Audio proof and player boundary

- implement the smallest output proof using the validated PS5/SDL path;
- test the selected decoder path with a controlled sample;
- document the verified codec/container combinations;
- implement one live stream with a bounded buffer;
- add stop, reconnect, timeout, and decoder-error states;
- keep the decoder implementation replaceable behind the player boundary.

Exit condition: one supported Radio Browser stream plays reliably, and failure
returns control to the UI with a useful state.

### Phase 5: First useful app loop

- browse stations;
- search by name;
- select a station;
- play/stop it;
- show station and now-playing information when available;
- save and remove favorites;
- restore the last selected station without auto-playing it;
- add a simple settings/about screen only for implemented capabilities.

Exit condition: a controller-only user can browse, select, play, stop, and
favorite a station in a complete launch session.

### Phase 6: Hardening and later features

Only after the basic loop works:

- country, language, and tag filters;
- bounded metadata/artwork cache;
- improved now-playing metadata;
- offline favorites and cached browse results;
- better stream health checks and reconnect policy;
- volume and output settings exposed by the verified PS5 audio path;
- accessibility/readability and reduced-motion pass;
- localization;
- package-size and startup-time optimization;
- license and third-party notice review.

## 10. Validation strategy

### Host-side checks

- boilerplate doctor succeeds;
- project source list matches the files that exist;
- all configured include paths and archives resolve;
- static ELF/FSELF inspection succeeds;
- staged RML/RCSS/assets are present in the title output;
- no accidental dependency on generated files from the template source tree.

### PS5 smoke checks

- cold launch;
- repeated launch after exit;
- RmlUi document load;
- visible focus state;
- D-pad navigation;
- Cross/Circle behavior;
- suspend/resume if supported by the loader/runtime;
- clean behavior when a document, network request, or stream fails.

### Network checks

- DNS or mirror rotation;
- TLS certificate validation;
- timeout and cancellation;
- malformed JSON;
- empty result;
- station with missing favicon;
- station with redirect or playlist URL;
- stale/broken stream;
- API server failure.

### Audio checks

- decoder initialization failure;
- short reads and buffering starvation;
- stream reconnect;
- stop during buffering;
- malformed or unsupported stream;
- output underrun;
- UI remains responsive during decode and network activity.

## 11. Risks and decisions to revisit

1. **RmlUi render backend:** the local build recipe proves library packaging, not
   that a complete PS5 RmlUi renderer exists. Resolve this before designing a
   large UI.
2. **C++ runtime/linking:** the boilerplate is already C/C++, but RmlUi pulls in
   libc++ and other static dependencies. Keep the first source file small and
   inspect link errors one dependency at a time.
3. **SDL presentation path:** use the existing PS5 SDL package/backend first;
   do not copy the BFplayer-specific 4K override unless the radio app actually
   needs it.
4. **HTTP/TLS behavior:** use the package-provided curl/CA setup and verify it
   on the target before building a custom networking layer.
5. **Station stream diversity:** Radio Browser metadata is broad, but individual
   streams may use formats, redirects, playlists, or headers the first player
   does not support. Start with one verified format and report unsupported
   streams clearly.
6. **Audio hardware/offload:** the research suggests useful native paths but does
   not replace an end-to-end PS5 test. Keep the player boundary independent from
   the first UI milestone.
7. **Dependency scope:** the RmlUi recipe enables Lua bindings. If the app never
   uses Lua, disable that dependency later only after the first package link is
   proven and the resulting build is smaller.
8. **Licenses:** preserve notices for the GPL boilerplate, MIT RmlUi, SDL2, curl,
   JSON library, fonts, and any decoder used by the final app.

## 12. Immediate next actions

1. Run the copied boilerplate doctor and baseline build.
2. Update the copied `project.json` identity for this project.
3. Add the first static `ui/main.rml` and `ui/styles/app.rcss` files.
4. Decide the first visual direction together before wiring API or audio.
5. Implement the smallest SDL + RmlUi PS5 smoke test.
6. Validate that smoke test on target hardware before continuing.

No Radio Browser client, audio decoder, database, or feature-heavy UI should be
added before the SDL + RmlUi smoke-test gate passes.
