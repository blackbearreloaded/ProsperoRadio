#!/usr/bin/env bash
# ps5-native-app-boilerplate - Native Linux/WSL application build.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Compiles, links, signs, validates, and assembles the root skeleton app.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
format=${1:-Folder}
format=${format,,}
case "$format" in folder|ffpkg) ;; *)
    echo "usage: tools/build.sh [Folder|Ffpkg]" >&2
    exit 2
esac
# What a build that is not a release calls itself, such as a pull request's number and
# commit. Checked before anything is built; written into the app folder further down.
if [[ -n ${BUILD_LABEL:-} ]]; then
    [[ $BUILD_LABEL =~ ^[A-Za-z0-9\ ,._#-]{1,40}$ ]] || {
        echo "BUILD_LABEL must be 1 to 40 letters, digits, spaces or , . _ # -" >&2
        exit 2
    }
fi

for command in python3 sha256sum; do
    command -v "$command" >/dev/null || {
        echo "missing required command: $command" >&2
        exit 2
    }
done
bash "$root/tools/setup-native-dependencies.sh" >/dev/null

param="$root/sce_sys/param.json"
read -r title_id content_version < <(python3 - "$param" <<'PY'
import json, re, sys

with open(sys.argv[1], encoding="utf-8") as source:
    value = json.load(source)

title_id = value.get("titleId", "")
concept_id = value.get("conceptId", "")
content_id = value.get("contentId", "")
if not re.fullmatch(r"PPSA\d{5}", title_id):
    raise SystemExit("param.json titleId must use PPSA followed by five digits")
if not re.fullmatch(r"\d{5}", concept_id):
    raise SystemExit("param.json conceptId must contain five digits")
if (not re.fullmatch(r"[A-Z]{2}\d{4}-PPSA\d{5}_00-[A-Z0-9]{16}", content_id)
        or title_id not in content_id):
    raise SystemExit("param.json contentId must be valid and contain titleId")
if not re.fullmatch(r"\d{2}\.\d{3}\.\d{3}", value.get("contentVersion", "")):
    raise SystemExit("param.json contentVersion must use NN.NNN.NNN")
if not re.fullmatch(r"\d{2}\.\d{2}", value.get("masterVersion", "")):
    raise SystemExit("param.json masterVersion must use NN.NN")
size = value.get("downloadDataSize")
if isinstance(size, bool) or not isinstance(size, int) or size < 0:
    raise SystemExit("param.json downloadDataSize must be a non-negative integer")

category = value.get("applicationCategoryType")
badge = value.get("contentBadgeType")
if (category, badge) not in {(0, 1), (65536, 2)}:
    raise SystemExit("param.json category and badge must describe a game or media app")
if category == 0:
    intents = value.get("gameIntent", {}).get("permittedIntents", [])
    if not any(item.get("intentType") == "launchActivity" for item in intents):
        raise SystemExit("game param.json must permit the launchActivity intent")
elif "gameIntent" in value:
    raise SystemExit("media param.json must not contain gameIntent")

localized = value.get("localizedParameters", {})
language = localized.get("defaultLanguage", "")
title = localized.get(language, {}).get("titleName", "")
if not isinstance(title, str) or not title.strip():
    raise SystemExit("param.json default-language titleName cannot be empty")
print(title_id, value["contentVersion"])
PY
)

# Loader/container constants validated on firmware 6.02 and 12.70. These are
# deliberately separate from the public application version in param.json.
module_sdk=0x02000009
companion_sdk=0x08050001
fself_magic=0x1D3D154F

bash "$root/tools/validate-assets.sh" "$root/sce_sys"
# The interface kit and HarfBuzz are fetched, not kept in the repository.
ui_kit=$(bash "$root/tools/prepare-ui-kit.sh")
bash "$root/tools/fetch-harfbuzz.sh" >/dev/null

sdk_root="$root/.deps/native/ps5-payload-sdk"
zlib_root="$root/.deps/native/zlib/root"
zlib_archive=$(find "$zlib_root" -type f -name libz.a -print -quit)
cxx=${CXX:-}
if [[ -z $cxx ]]; then
    cxx=$(command -v clang++-18 || command -v clang++)
fi
[[ -n $cxx ]] || { echo "Clang++ was not found" >&2; exit 2; }

build="$root/build"
dist="$root/dist"
native="$root/tooling/native"
tool="$build/host/ps5-native-tool"
mkdir -p "$build/host" "$build/obj" "$dist"
"$cxx" -std=c++20 -O2 -Wall -Wextra -Werror \
    -I "$zlib_root/usr/include" \
    "$native/native_app_builder.cpp" "$native/self_container.cpp" \
    "$native/elf_object.cpp" "$native/sce_module_writer.cpp" \
    "$zlib_archive" -o "$tool"

mapfile -d '' -t source_paths < <(
    find "$root/src" "$ui_kit/src" -type f \( -name '*.c' -o -name '*.cc' -o -name '*.cpp' \) \
        -print0 | sort -z
)
sources=()
for source in "${source_paths[@]}"; do
    sources+=("${source#"$root/"}")
done
(( ${#sources[@]} > 0 )) || { echo "src/ has no C or C++ sources" >&2; exit 2; }

definitions=()
cxx_flags=()
includes=()
archives=()
import_stubs=()
pacbrew_packages=()
pacbrew_includes=()
pacbrew_archives=()
[[ -z ${APP_DEFINITIONS:-} ]] || read -r -a definitions <<< "$APP_DEFINITIONS"
[[ -z ${APP_CXXFLAGS:-} ]] || read -r -a cxx_flags <<< "$APP_CXXFLAGS"
[[ -z ${APP_INCLUDE_PATHS:-} ]] || read -r -a includes <<< "$APP_INCLUDE_PATHS"
[[ -z ${APP_STATIC_ARCHIVES:-} ]] || read -r -a archives <<< "$APP_STATIC_ARCHIVES"
[[ -z ${APP_IMPORT_STUBS:-} ]] || read -r -a import_stubs <<< "$APP_IMPORT_STUBS"
[[ -z ${PACBREW_PACKAGES:-} ]] || read -r -a pacbrew_packages <<< "$PACBREW_PACKAGES"
[[ -z ${PACBREW_INCLUDE_PATHS:-} ]] || read -r -a pacbrew_includes <<< "$PACBREW_INCLUDE_PATHS"
[[ -z ${PACBREW_STATIC_ARCHIVES:-} ]] || read -r -a pacbrew_archives <<< "$PACBREW_STATIC_ARCHIVES"

pacbrew_cflags=()
pacbrew_libs=()
if (( ${#pacbrew_packages[@]} > 0 || ${#pacbrew_includes[@]} > 0 || ${#pacbrew_archives[@]} > 0 )); then
    pacbrew_resolution=$(bash "$root/tools/setup-pacbrew-dependencies.sh" \
        --resolve "${pacbrew_packages[@]}")
    mapfile -d '' -t pacbrew_cflags < <(python3 -c \
        'import json,sys; [print(v, end="\0") for v in json.loads(sys.argv[1])["cflags"]]' \
        "$pacbrew_resolution")
    mapfile -d '' -t pacbrew_libs < <(python3 -c \
        'import json,sys; [print(v, end="\0") for v in json.loads(sys.argv[1])["libs"]]' \
        "$pacbrew_resolution")
    pacbrew_root=$(python3 -c 'import json,sys; print(json.loads(sys.argv[1])["root"])' \
        "$pacbrew_resolution")
    for include in "${pacbrew_includes[@]}"; do
        [[ $include =~ ^[A-Za-z0-9_.+-]+(/[A-Za-z0-9_.+-]+)*$ &&
            -d $pacbrew_root/user/homebrew/$include ]] || {
            echo "invalid PacBrew include path: $include" >&2; exit 2;
        }
        pacbrew_cflags+=("-I$pacbrew_root/user/homebrew/$include")
    done
    for archive in "${pacbrew_archives[@]}"; do
        [[ $archive =~ ^[A-Za-z0-9_.+-]+(/[A-Za-z0-9_.+-]+)*\.a$ &&
            -f $pacbrew_root/user/homebrew/$archive ]] || {
            echo "invalid PacBrew static archive: $archive" >&2; exit 2;
        }
        pacbrew_libs+=("$pacbrew_root/user/homebrew/$archive")
    done
    printf 'PacBrew dependencies: %s\n' "${pacbrew_packages[*]:-(manual archives)}"
fi

if command -v ccache >/dev/null && [[ -z ${PS5_CLANG:-} ]]; then
    printf '#!/bin/sh\nexec ccache clang-18 "$@"\n' > "$build/ccache-clang18"
    chmod +x "$build/ccache-clang18"
    export PS5_CLANG="$build/ccache-clang18"
fi
compile_jobs=$(nproc)
compile_pids=()
compile_failed=
objects=()
for source in "${sources[@]}"; do
    [[ $source =~ ^(src|\.deps/ui-kit/stage/src)/[A-Za-z0-9_./-]+\.(c|cc|cpp)$ &&
        -f $root/$source ]] || {
        echo "invalid source: $source" >&2; exit 2;
    }
    object="$build/obj/${source//\//_}.o"
    if [[ $source == *.c ]]; then standard=-std=c11; else standard=-std=c++20; fi
    args=("$standard" -O2 -Wall -Wextra -ffunction-sections -fdata-sections)
    [[ $source == *.c ]] || args+=(-fno-exceptions -fno-rtti "${cxx_flags[@]}")
    for definition in "${definitions[@]}"; do
        [[ $definition =~ ^[A-Za-z_][A-Za-z0-9_]*(=[A-Za-z0-9_]+)?$ ]] || {
            echo "invalid compile definition: $definition" >&2; exit 2;
        }
        args+=("-D$definition")
    done
    for include in "${includes[@]}"; do
        [[ $include =~ ^[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*$ && -d $root/$include ]] || {
            echo "invalid include path: $include" >&2; exit 2;
        }
        args+=("-I$root/$include")
    done
    args+=("${pacbrew_cflags[@]}")
    PS5_PAYLOAD_SDK="$sdk_root" sh "$root/tooling/prospero-clang18" \
        "${args[@]}" -c "$root/$source" -o "$object" &
    compile_pids+=($!)
    objects+=("$object")
    if (( ${#compile_pids[@]} >= compile_jobs )); then
        wait "${compile_pids[0]}" || compile_failed=1
        compile_pids=("${compile_pids[@]:1}")
    fi
done
for pid in "${compile_pids[@]}"; do
    wait "$pid" || compile_failed=1
done
[[ -z $compile_failed ]] || { echo "compilation failed" >&2; exit 1; }

PS5_PAYLOAD_SDK="$sdk_root" sh "$root/tooling/prospero-clang18" \
    -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti \
    -ffunction-sections -fdata-sections \
    -c "$native/app_crt.cpp" -o "$build/obj/app_crt.o"

PS5_PAYLOAD_SDK="$sdk_root" sh "$root/tooling/prospero-clang18" \
    -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti \
    -ffunction-sections -fdata-sections \
    -c "$native/app_cpp_runtime.cpp" -o "$build/obj/app_cpp_runtime.o"

link_inputs=("$build/obj/app_crt.o" "$build/obj/app_cpp_runtime.o" "${objects[@]}")
for archive in "${archives[@]}"; do
    [[ $archive =~ ^[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*\.a$ && -f $root/$archive ]] || {
        echo "invalid static archive: $archive" >&2; exit 2;
    }
    link_inputs+=("$root/$archive")
done
builder_stub_args=()
link_imports=()
for stub in "${import_stubs[@]}"; do
    [[ $stub =~ ^[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*\.(so|sprx|a)$ && -f $root/$stub ]] || {
        echo "invalid import stub path: $stub" >&2; exit 2;
    }
    builder_stub_args+=(--stub "$root/$stub")
    # The OpenGL SDK ships real import libraries: link them as they are.
    if [[ $stub == *.so ]]; then
        link_imports+=("$root/$stub")
        continue
    fi
    case "${stub##*/}" in
        libSceOpusDec_stub.a)
            link_source="$native/prospero_radio_import_stub_opus.cpp"
            soname=libSceOpusDec.sprx
            link_name=libSceOpusDec.so
            ;;
        libSceOpusCeltDec_stub.a)
            link_source="$native/prospero_radio_import_stub_opus_celt.cpp"
            soname=libSceOpusCeltDec.sprx
            link_name=libSceOpusCeltDec.so
            ;;
        *)
            echo "unsupported ProsperoRadio import stub: $stub" >&2; exit 2;
            ;;
    esac
    mkdir -p "$build/import-stubs"
    link_object="$build/import-stubs/${link_name%.so}.o"
    link_stub="$build/import-stubs/$link_name"
    PS5_PAYLOAD_SDK="$sdk_root" sh "$root/tooling/prospero-clang18" \
        -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti -fPIC \
        -c "$link_source" -o "$link_object"
    "$sdk_root/bin/prospero-lld" --shared -soname "$soname" -o "$link_stub" "$link_object"
    link_imports+=("$link_stub")
done
# The public SDK lacks a CommonDialog import stub, but the IME entry point
# needs its one initializer. Keep this link-only declaration local and feed
# its ordinary ELF metadata to the same validated import writer.
common_link_object="$build/import-stubs/libSceCommonDialog.o"
common_link_stub="$build/import-stubs/libSceCommonDialog.so"
PS5_PAYLOAD_SDK="$sdk_root" sh "$root/tooling/prospero-clang18" \
    -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti -fPIC \
    -c "$native/prospero_radio_import_stub_common_dialog.cpp" -o "$common_link_object"
"$sdk_root/bin/prospero-lld" --shared -soname libSceCommonDialog.sprx \
    -o "$common_link_stub" "$common_link_object"
builder_stub_args+=(--stub "$common_link_stub")
link_imports+=("$common_link_stub")

# AudioDec is likewise loaded dynamically by the app but absent from the
# public SDK stub directory. The facade exists only during linking.
audiodec_link_object="$build/import-stubs/libSceAudiodec.o"
audiodec_link_stub="$build/import-stubs/libSceAudiodec.so"
PS5_PAYLOAD_SDK="$sdk_root" sh "$root/tooling/prospero-clang18" \
    -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti -fPIC \
    -c "$native/prospero_radio_import_stub_audiodec.cpp" -o "$audiodec_link_object"
"$sdk_root/bin/prospero-lld" --shared -soname libSceAudiodec.sprx \
    -o "$audiodec_link_stub" "$audiodec_link_object"
builder_stub_args+=(--stub "$audiodec_link_stub")
link_imports+=("$audiodec_link_stub")
link_inputs+=("${link_imports[@]}")
if (( ${#pacbrew_libs[@]} > 0 )); then
    link_inputs+=(--start-group "${pacbrew_libs[@]}" --end-group)
fi
linker_import_flags=()
if (( ${#builder_stub_args[@]} > 0 )); then
    linker_import_flags+=(--unresolved-symbols=ignore-all)
fi
# The launch picture stays until the first frame, and the system modules
# loaded before the app leaves its sandbox stay loaded
# (src/runtime/runtime_shims.c).
wrap_options=(--wrap=sceSystemServiceHideSplashScreen --wrap=fcntl)
for symbol in sceSysmoduleLoadModule sceSysmoduleUnloadModule \
    sceSysmoduleLoadModuleInternal sceSysmoduleUnloadModuleInternal; do
    wrap_options+=("--wrap=$symbol")
done
# The OpenGL runtime needs the process-lifetime heap in the kit's runtime/app_heap.c.
if [[ -f $ui_kit/src/runtime/app_heap.c ]]; then
    for symbol in malloc calloc realloc free posix_memalign malloc_usable_size; do
        wrap_options+=("--wrap=$symbol")
    done
fi
"$sdk_root/bin/prospero-lld" -T "$native/ps5-pie.ld" --eh-frame-hdr "${wrap_options[@]}" \
    --version-script "$native/app-symbols.map" \
    -L "$sdk_root/target/lib" -e _start -o "$build/llvm-pie.elf" "${link_inputs[@]}" \
    "${linker_import_flags[@]}" \
    --as-needed "$sdk_root"/target/lib/*.so
readelf_tool=$(command -v llvm-readelf-18 || command -v llvm-readelf || command -v readelf)
for compatibility_symbol in fchown lstat; do
    if "$readelf_tool" --symbols --wide "$build/llvm-pie.elf" |
        grep -Eq "UND[[:space:]]+$compatibility_symbol$"; then
        echo "SQLite compatibility symbol remained unresolved: $compatibility_symbol" >&2
        exit 2
    fi
done
"$tool" link --in "$build/llvm-pie.elf" --out "$build/eboot.elf" \
    --stub-dir "$sdk_root/target/lib" "${builder_stub_args[@]}" --module-sdk "$module_sdk" \
    --companion-sdk "$companion_sdk" --file-name eboot.elf

app="$dist/$title_id"
rm -rf -- "$app"
mkdir -p "$app/sce_sys" "$app/sce_module"
"$tool" self --sign --in "$build/eboot.elf" --out "$app/eboot.bin" \
    --magic "$fself_magic"

cp "$param" "$app/sce_sys/param.json"
# Says which build this is (docs/PULL_REQUEST_BUILDS.md); a release has none.
[[ -z ${BUILD_LABEL:-} ]] || printf '%s\n' "$BUILD_LABEL" > "$app/build-label.txt"
# Filesystem access (src/elevation): the exact-title upstream Lapy helper,
# verified against its build manifest before it reaches the package. The app
# sends it to the local ELF loader when no resident Lapy service answers.
python3 -B "$root/tools/build-lapy-helper.py"
mkdir -p "$app/licenses"
cp "$root/build/lapy-owned-helper/lapy.elf" "$app/lapy.elf"
cp "$root/build/lapy-owned-helper/lapy-manifest.json" "$app/lapy-manifest.json"
cp "$root/build/lapy-owned-helper/LICENSE.Lapy" "$app/licenses/Lapy-MIT.txt"
# The self-update helper (third_party/self_update_helper, the boilerplate's)
# is an ordinary payload: the app sends it to the payload loader when the
# listener accepts an update.
make -s -C "$root/third_party/self_update_helper" PS5_PAYLOAD_SDK="$sdk_root" \
    OUTPUT="$build/self-update/self-updater.elf"
python3 "$root/tools/validate-loader-elf.py" "$build/self-update/self-updater.elf"
cp "$build/self-update/self-updater.elf" "$app/self-updater.elf"
cp "$root/third_party/miniz/LICENSE" "$app/licenses/miniz-MIT.txt"
for asset in icon0.png pic0.dds pic1.dds snd0.at9; do
    [[ -f $root/sce_sys/$asset ]] && cp "$root/sce_sys/$asset" "$app/sce_sys/$asset"
done
[[ ! -d $root/assets ]] || cp -a "$root/assets" "$app/assets"
# The interface's baked fonts come with the kit.
mkdir -p "$app/assets/fonts"
cp -a "$ui_kit/assets/fonts/." "$app/assets/fonts/"
if [[ -f $app/assets/ui/main.rml ]]; then
    python3 - "$app/assets/ui/main.rml" "$content_version" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
value = path.read_text(encoding="utf-8").replace("{{PROSPERO_RADIO_VERSION}}", sys.argv[2])
path.write_text(value, encoding="utf-8", newline="\n")
PY
fi

[[ -f $root/runtime/libc.prx ]] || bash "$root/tools/rebuild-libc.sh"
(cd "$root/runtime" && sha256sum --check --strict libc.prx.sha256)
runtime_modules=("$root/runtime/libc.prx")
additional_runtime=()
[[ -z ${APP_RUNTIME_MODULES:-} ]] || read -r -a additional_runtime <<< "$APP_RUNTIME_MODULES"
for source in "${additional_runtime[@]}"; do
    [[ $source =~ ^\.local/runtime/[A-Za-z0-9._-]+\.prx$ && -f $root/$source ]] || {
        echo "invalid runtime module: $source" >&2; exit 2;
    }
    runtime_modules+=("$root/$source")
done
declare -A runtime_names=()
for input in "${runtime_modules[@]}"; do
    name=${input##*/}
    [[ $name =~ ^[A-Za-z0-9._-]+\.prx$ && -z ${runtime_names[$name]+present} ]] || {
        echo "invalid or duplicate runtime module name: $name" >&2; exit 2;
    }
    runtime_names[$name]=present
    magic=$(python3 - "$input" <<'PY'
import struct, sys
with open(sys.argv[1], "rb") as stream:
    print(f"{struct.unpack('<I', stream.read(4))[0]:08x}")
PY
)
    if [[ $magic == 1d3d154f || $magic == eef51454 ]]; then
        cp "$input" "$app/sce_module/$name"
    else
        "$tool" self --sign --in "$input" --out "$app/sce_module/$name"
    fi
    "$tool" self --inspect --file "$app/sce_module/$name"
done
"$tool" self --inspect --file "$app/eboot.bin"

if [[ $format == ffpkg ]]; then
    ufs2tool=$(bash "$root/tools/setup-packaging-dependencies.sh" ffpkg)
    rm -f -- "$dist/$title_id.ffpkg"
    "$ufs2tool" makefs -S 4096 -b 20% -t ffs \
        -o version=2,bsize=32768,fsize=4096,minfree=0,softupdates=0,optimization=space \
        "$dist/$title_id.ffpkg" "$app"
    python3 - "$dist/$title_id.ffpkg" <<'PY'
import struct, sys
with open(sys.argv[1], "rb") as stream:
    stream.seek(0x1055c)
    if struct.unpack("<I", stream.read(4))[0] != 0x19540119:
        raise SystemExit("FFPKG is missing the UFS2 superblock magic")
PY
fi

printf 'Build complete.\nApp folder: %s\n' "$app"
[[ $format != ffpkg ]] || printf 'FFPKG:     %s\n' "$dist/$title_id.ffpkg"
