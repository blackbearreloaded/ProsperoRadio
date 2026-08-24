# Contributing

Contributions should keep PSRadio reproducible, controller-first, and stable on
the limited native PS5 environment. Discuss large dependency, codec, renderer,
or persistence changes before implementing them.

## Development workflow

1. Read [Getting started](docs/GETTING_STARTED.md) and run
   `./tools/doctor.ps1`.
2. Create a focused branch and keep unrelated cleanup out of the change.
3. Run the relevant host regression checks documented in
   [Architecture](docs/ARCHITECTURE.md).
4. Run `./build.ps1` and confirm the build reports zero static FSELF errors.
5. Test platform-facing changes on the intended firmware and loader. Include
   that context in the pull request; do not generalize one hardware result to
   every PS5 environment.

## Repository rules

- Do not commit `.deps/`, `build/`, `dist/`, `.local/`, editor caches, console
  dumps, logs, package images, keys, credentials, proprietary PRXs, firmware,
  or extracted game files.
- The independently generated `runtime/libc.prx` is the sole intentional
  tracked PRX. Keep its hash pinned unless the clean-room emitter changes.
- Add application sources explicitly to `project.json`; unused experiments do
  not belong in the release tree.
- Keep UI operations non-blocking. Network, decoder, and filesystem work must
  not stall controller input.
- Record every new dependency and its redistribution terms in `NOTICE.md`.
- Do not expose a codec in Radio Browser queries until it passes the completion
  criteria in [ROADMAP.md](ROADMAP.md).

Changes to the SharpProspero compatibility patch should include a deterministic
output comparison or a narrowly scoped regression check. Do not silently
change its pinned upstream revision.

Pull requests run the [Build workflow](.github/workflows/build.yml), including
the host regressions and a complete `.ffpfsc` package build. A passing workflow
does not replace PS5 hardware validation for platform-facing changes.

## Pull requests

Describe the user-visible change, host checks, PS5 test environment when
applicable, and any remaining limitation. Prefer small commits that each leave
the repository buildable.
