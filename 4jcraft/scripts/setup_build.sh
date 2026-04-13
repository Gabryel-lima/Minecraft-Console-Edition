#!/usr/bin/env sh

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)

builddir=${BUILD_DIR:-build}
default_buildtype=${BUILDTYPE:-debug} # Mude aqui o tipo de build padrão (debug, release, etc.)
default_renderer=${RENDERER:-gles} # Mude aqui o renderizador padrão (gles, vulkan, etc.)
default_unity=${UNITY:-on} # Mude aqui a configuração de unity build padrão (on, off, etc.)

if [ $# -gt 0 ] && [ "${1#-}" = "$1" ]; then
  builddir=$1
  shift
fi

has_buildtype=0
has_renderer=0
has_native=0
has_unity=0
has_setup_mode=0
requested_native_file=
wait_for_native_file_path=0
ccache_native_requested=0

for arg in "$@"; do
  if [ "$wait_for_native_file_path" -eq 1 ]; then
    requested_native_file=$arg
    wait_for_native_file_path=0
    has_native=1
    case "$requested_native_file" in
      *llvm_native_ccache.txt)
        ccache_native_requested=1
        ;;
    esac
    continue
  fi

  case "$arg" in
    -Dbuildtype=*|--buildtype=*)
      has_buildtype=1
      ;;
    -Drenderer=*)
      has_renderer=1
      ;;
    -Dunity=*|--unity=*)
      has_unity=1
      ;;
    --native-file)
      has_native=1
      wait_for_native_file_path=1
      ;;
    --native-file=*)
      has_native=1
      requested_native_file=${arg#--native-file=}
      case "$requested_native_file" in
        *llvm_native_ccache.txt)
          ccache_native_requested=1
          ;;
      esac
      ;;
    --cross-file|--cross-file=*)
      has_native=1
      ;;
    --wipe|--reconfigure)
      has_setup_mode=1
      ;;
  esac
done

cache_enabled=0

install_linux_packages() {
  if [ $# -eq 0 ]; then
    return 0
  fi

  case "$(uname -s)" in
    Linux)
      ;;
    *)
      printf 'Automatic dependency installation is only supported on Linux.\n' >&2
      printf 'Install these packages manually: %s\n' "$*" >&2
      exit 1
      ;;
  esac

  if ! command -v apt-get >/dev/null 2>&1; then
    printf 'apt-get not found; install these packages manually: %s\n' "$*" >&2
    exit 1
  fi

  if [ "$(id -u)" -eq 0 ]; then
    apt_get() {
      apt-get "$@"
    }
  elif command -v sudo >/dev/null 2>&1; then
    apt_get() {
      sudo apt-get "$@"
    }
  else
    printf 'Need root or sudo to install these packages: %s\n' "$*" >&2
    exit 1
  fi

  printf 'Installing missing Linux build tools: %s\n' "$*" >&2
  apt_get update
  apt_get install -y "$@"
}

install_python_requirements() {
  if command -v meson >/dev/null 2>&1; then
    return 0
  fi

  if ! command -v python3 >/dev/null 2>&1; then
    install_linux_packages python3
  fi

  if ! python3 -m pip --version >/dev/null 2>&1; then
    install_linux_packages python3-pip
  fi

  if [ -n "${VIRTUAL_ENV:-}" ]; then
    python3 -m pip install -r "$repo_root/requirements.txt"
  else
    python3 -m pip install --user -r "$repo_root/requirements.txt"
    export PATH="$HOME/.local/bin:$PATH"
  fi

  if ! command -v meson >/dev/null 2>&1; then
    printf 'meson is still missing after installing Python requirements.\n' >&2
    exit 1
  fi
}

missing_packages=

if ! command -v cc >/dev/null 2>&1 || ! command -v c++ >/dev/null 2>&1; then
  missing_packages="$missing_packages build-essential"
fi

if ! command -v ninja >/dev/null 2>&1; then
  missing_packages="$missing_packages ninja-build"
fi

if ! command -v pkg-config >/dev/null 2>&1; then
  missing_packages="$missing_packages pkg-config"
fi

if [ "$ccache_native_requested" -eq 1 ]; then
  if ! command -v ccache >/dev/null 2>&1; then
    missing_packages="$missing_packages ccache"
  fi

  if ! command -v clang >/dev/null 2>&1 || ! command -v clang++ >/dev/null 2>&1; then
    missing_packages="$missing_packages clang"
  fi

  if ! command -v lld >/dev/null 2>&1; then
    missing_packages="$missing_packages lld"
  fi

  if [ -n "$missing_packages" ]; then
    install_linux_packages $missing_packages
  fi
fi

install_python_requirements

if command -v ccache >/dev/null 2>&1; then
  cache_enabled=1
  export CCACHE_BASEDIR="${CCACHE_BASEDIR:-$repo_root}"
  export CCACHE_COMPILERCHECK="${CCACHE_COMPILERCHECK:-content}"
  export CCACHE_DIR="${CCACHE_DIR:-$HOME/.cache/ccache}"
  mkdir -p "$CCACHE_DIR"
fi

auto_native_file=
if [ "$has_native" -eq 0 ]; then
  if command -v clang >/dev/null 2>&1 && command -v clang++ >/dev/null 2>&1; then
    if [ "$cache_enabled" -eq 1 ]; then
      auto_native_file="$repo_root/scripts/llvm_native_ccache.txt"
    else
      auto_native_file="$repo_root/scripts/llvm_native.txt"
    fi
  elif [ "$cache_enabled" -eq 1 ] && [ -z "${CC:-}" ] && [ -z "${CXX:-}" ]; then
    export CC='ccache cc'
    export CXX='ccache c++'
  fi
fi

effective_native_file=${requested_native_file:-$auto_native_file}

if [ -n "$auto_native_file" ]; then
  printf 'Using Meson native file: %s\n' "$auto_native_file" >&2
elif [ "$cache_enabled" -eq 1 ] && [ "${CC:-}" = 'ccache cc' ] && [ "${CXX:-}" = 'ccache c++' ]; then
  printf 'Using ccache with the default compiler toolchain.\n' >&2
elif [ "$cache_enabled" -eq 0 ]; then
  printf 'ccache not found; configuring build without compiler cache.\n' >&2
fi

build_uses_ccache_toolchain=0
if [ -n "$effective_native_file" ] && [ -f "$builddir/meson-info/intro-compilers.json" ]; then
  if grep -q '"id": "clang"' "$builddir/meson-info/intro-compilers.json" &&
     grep -q 'ccache' "$builddir/meson-info/intro-compilers.json"; then
    build_uses_ccache_toolchain=1
  fi
fi

cd "$repo_root"

set -- setup "$builddir" "$@"

if [ "$has_setup_mode" -eq 0 ] && { [ -f "$builddir/build.ninja" ] || [ -d "$builddir/meson-private" ]; }; then
  if [ -n "$effective_native_file" ] && [ "${effective_native_file##*/}" = 'llvm_native_ccache.txt' ] && [ "$build_uses_ccache_toolchain" -eq 0 ]; then
    printf 'Recreating existing build directory via --wipe to apply the ccache toolchain.\n' >&2
    set -- "$@" --wipe
  else
    printf 'Reusing existing build directory via --reconfigure.\n' >&2
    set -- "$@" --reconfigure
  fi
fi

if [ -n "$auto_native_file" ]; then
  set -- "$@" "--native-file=$auto_native_file"
fi

if [ "$has_buildtype" -eq 0 ]; then
  set -- "$@" "-Dbuildtype=$default_buildtype"
fi

if [ "$has_renderer" -eq 0 ]; then
  set -- "$@" "-Drenderer=$default_renderer"
fi

if [ "$has_unity" -eq 0 ]; then
  set -- "$@" "-Dunity=$default_unity"
fi

exec meson "$@"
