# Platform constraints

These notes describe PSRadio's validated compatibility baseline. PS5 firmware
and homebrew loaders vary, so changes must be verified in their target
environment.

## Loader and runtime

- The application is a native PS5 ELF of type `0xFE10` inside a development
  FSELF.
- FSELF magic `0x1D3D154F` and SDK pair
  `0x02000009 / 0x08050001` are the validated project defaults.
- The supported title layout includes `sce_module/libc.prx`. The bundled
  clean-room shim supplies its minimal loader contract and contains no Sony
  implementation code.
- The shim is not a C library. Application imports bind to platform modules
  selected by the linker.
- PSRadio keeps `main` alive and relies on the host application lifecycle for
  process closure.

## Filesystem

- `/app0` is the read-only application image.
- `/download0` is available for writable application data when
  `downloadDataSize` is positive in `param.json`.
- Applications should use the sandbox path rather than relying on its host
  backing-file location.
- Availability of `/temp0` and other mounts depends on the loader and title
  environment.
- SaveData setup is not included. Use `/download0` for ordinary configuration
  and provide export/import for data that must survive title removal or cache
  management.

## Home-screen presentation

- `icon0.png` is a 512x512 launcher icon.
- `pic0.dds` and `pic1.dds` are 3840x2160, single-surface,
  `DXGI_FORMAT_BC7_UNORM` DX10 DDS images.
- The package controls `titleName`, the launcher icon, selection backgrounds,
  and optional selection audio.
- Retail-style custom logos and descriptions are online catalog metadata and
  cannot be defined by the supported package-local fields for a synthetic
  homebrew concept.
- Selection audio uses a 48 kHz stereo ATRAC9 RIFF file named `snd0.at9` with
  one `smpl` loop. The asset tool enforces a conservative 15-second,
  approximately 360 KB profile.
- Presentation metadata may be cached by the shell or loader. Follow the
  loader's refresh procedure after structural asset changes.

## Application capabilities

PSRadio uses SDL's CPU-rendered presentation path, native controller input and
IME, native HTTP/TLS, `/download0` persistence, native AAC decoding, and
AudioOut. GPU rendering and GPU decode are outside this repository's scope.
See [Architecture](ARCHITECTURE.md) for the runtime boundaries.
