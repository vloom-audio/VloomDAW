#!/usr/bin/env bash
# Builds a small static FFmpeg into PREFIX for VloomDAW's release builds on
# macOS and Linux: only what audio/FileDecoder uses (local files; MP3, FLAC,
# Ogg, WAV, AIFF and M4A; their audio decoders; libswresample), so the app
# carries little FFmpeg code and needs no FFmpeg on the user's system.
#
#   scripts/build-ffmpeg.sh PREFIX            # this machine's architecture
#   scripts/build-ffmpeg.sh PREFIX universal  # macOS: arm64 + x86_64 in one
#
# Then configure VloomDAW with -DFFMPEG_ROOT=PREFIX. The source is pinned to
# a release whose SHA-256 is checked before anything is built (the tarball
# is also signed by FFmpeg's release key FCF986EA15E6E293A5644F10B4322F04D67658D8).
set -euo pipefail

VERSION=8.1.3
SHA256=7138d28c96d9d3e3af4ee3d8cad72741f8ffb40da90c1112235dea3ecd3178a3
MACOS_MIN=13.0 # the app's deployment target (release.yml)

[[ $# -ge 1 ]] || { echo "usage: $0 PREFIX [universal]" >&2; exit 2; }
mkdir -p "$1"
PREFIX="$(cd "$1" && pwd)"
MODE="${2:-native}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

tarball="$WORK/ffmpeg-$VERSION.tar.xz"
curl -fsSL -o "$tarball" "https://ffmpeg.org/releases/ffmpeg-$VERSION.tar.xz"
if command -v sha256sum >/dev/null; then
    echo "$SHA256  $tarball" | sha256sum -c -
else
    echo "$SHA256  $tarball" | shasum -a 256 -c -
fi
tar -xJf "$tarball" -C "$WORK"
SRC="$WORK/ffmpeg-$VERSION"

# Keep in step with kFormats and kCodecs in src/audio/FileDecoder.cpp.
DEMUXERS=mp3,flac,ogg,wav,aiff,mov
DECODERS=mp3,mp3float,flac,vorbis,opus,aac,alac,pcm_u8,pcm_s8,pcm_s16le,pcm_s16be,pcm_s24le,pcm_s24be,pcm_s32le,pcm_s32be,pcm_f32le,pcm_f32be,pcm_f64le,pcm_f64be
PARSERS=mpegaudio,flac,vorbis,opus,aac
COMMON=(
    --disable-everything --disable-autodetect --disable-programs --disable-doc
    --disable-network --disable-avdevice --disable-avfilter --disable-swscale
    --disable-debug --enable-static --disable-shared --enable-pic
    --enable-protocol=file --enable-demuxer="$DEMUXERS" --enable-decoder="$DECODERS"
    --enable-parser="$PARSERS" --enable-swresample
)
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

# build ARCH DEST [configure options...]
build() {
    local dest="$2"
    shift 2
    local out="$WORK/build-$(basename "$dest")"
    mkdir -p "$out"
    (cd "$out" && "$SRC/configure" --prefix="$dest" "${COMMON[@]}" "$@" && make -j"$JOBS" && make install)
}

case "$(uname -s)-$MODE" in
Darwin-universal)
    for arch in arm64 x86_64; do
        extra=()
        [[ $arch == x86_64 ]] && extra+=(--disable-x86asm) # no nasm needed
        build "$arch" "$WORK/prefix-$arch" --arch="$arch" --target-os=darwin --enable-cross-compile \
            --cc="clang -arch $arch" --extra-cflags="-mmacosx-version-min=$MACOS_MIN" \
            --extra-ldflags="-mmacosx-version-min=$MACOS_MIN" ${extra[@]+"${extra[@]}"}
    done
    # One library per name holding both architectures; headers and
    # pkg-config files are the same for both apart from the prefix.
    mkdir -p "$PREFIX/lib/pkgconfig"
    rm -rf "$PREFIX/include"
    cp -R "$WORK/prefix-arm64/include" "$PREFIX/include"
    for lib in "$WORK"/prefix-arm64/lib/*.a; do
        name="$(basename "$lib")"
        lipo -create "$lib" "$WORK/prefix-x86_64/lib/$name" -output "$PREFIX/lib/$name"
    done
    for pc in "$WORK"/prefix-arm64/lib/pkgconfig/*.pc; do
        sed "s|$WORK/prefix-arm64|$PREFIX|g" "$pc" >"$PREFIX/lib/pkgconfig/$(basename "$pc")"
    done
    ;;
Darwin-native)
    build native "$PREFIX" --extra-cflags="-mmacosx-version-min=$MACOS_MIN" \
        --extra-ldflags="-mmacosx-version-min=$MACOS_MIN" --disable-x86asm
    ;;
Linux-native)
    build native "$PREFIX" --disable-x86asm
    ;;
*)
    echo "unsupported: $(uname -s) $MODE (Windows uses the prebuilt FFmpeg, see cmake/FFmpeg.cmake)" >&2
    exit 2
    ;;
esac
echo "FFmpeg $VERSION installed into $PREFIX"
