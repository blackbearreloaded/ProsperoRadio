# Update check and self-update

These files are the PS5 Native App Boilerplate's (`examples/update-check` and
`examples/self-update`, boilerplate commit 2966cac), taken as ProsperoEden
carries them: `self_update_ps5.c` takes the paths of the helper, of the app's
`param.json` and of the sequence file from `SELF_UPDATE_HELPER_PATH`,
`SELF_UPDATE_PARAM_PATH` and `SELF_UPDATE_SEQUENCE_PATH` when they are defined.
ProsperoRadio defines them (`src/update_kit/self_update_ps5.c`,
`src/radio_update.cpp`) because its folder is not `/app0` once it runs with
filesystem access.

The boilerplate's `console_curl.c` is not here: ProsperoRadio's radio service
already carries libcurl with the same console stand-ins
(`src/runtime/runtime_shims.c`, `src/runtime/netdb_shims.c`), so the three
functions of `console_curl.h` are implemented beside them in
`src/radio_http_curl.cpp`.

The helper the app sends to the console's payload loader is in
`third_party/self_update_helper` (the boilerplate's
`examples/self-update-helper` with ProsperoEden's three changes: its Makefile's
source folders; `swap_entries` in `updater.cpp`, which moves aside only the
entries the release replaces, so files a listener put in the app folder stay;
and `mounted_source`, which takes the installed folder from ShadowMountPlus's
`/user/app/<TITLEID>/mount.lnk` and refuses an image install).
