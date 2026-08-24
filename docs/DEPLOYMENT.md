# Deployment

This project creates a directory-style homebrew application and optional
filesystem images. It does not install software or create a signed retail
package.

## Requirements

Use a console you own with an already configured, compatible homebrew
environment and loader. Follow that loader's documentation for setup and
supported input formats. This repository does not configure the console.

Keep loader and mount services on a trusted local network.

## Build and stage

1. Complete a build successfully:

   ```powershell
   ./build.ps1
   ```

2. Choose one complete output supported by the loader:

   - `dist/<TITLE_ID>/`: directory form;
   - `dist/<TITLE_ID>.ffpkg`: UFS2 image;
   - `dist/<TITLE_ID>.ffpfsc`: compressed image.

3. For directory deployment, stage the entire `dist/<TITLE_ID>/` tree. Do not
   upload only `eboot.bin`.
4. Wait for the loader to report that the title is ready, then launch it from
   the Games section of the home screen.

Rebuild the selected format immediately before deployment so an older package
is not mistaken for the current application.

## Title ID allocation

The current app identity is `PPSA99769`. Allocate the next app as `PPSA99770`
and increment the five-digit numeric suffix by one for each future app. Keep
the `conceptId` and the title-ID portion of `contentId` synchronized.

## Smoke test

PSRadio hides the splash screen after SDL, RmlUi, native controller input, IME,
and the Radio Browser service initialize. The browse screen then remains alive
until it is closed through the home-screen interface.

The examples intentionally keep `main` alive. Do not return from `main` or
call an exit function unless the target loader and application lifecycle
explicitly support that path.

If launch fails, see [Troubleshooting](TROUBLESHOOTING.md).
