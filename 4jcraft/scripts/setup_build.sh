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

version_ge() {
  if [ -z "$1" ] || [ -z "$2" ]; then
    return 1
  fi
  if command -v sort >/dev/null 2>&1; then
    smallest=$(printf '%s\n%s\n' "$1" "$2" | sort -V | head -n1)
    [ "$smallest" = "$2" ]
  else
    [ "$1" = "$2" ]
  fi
}

install_python_requirements() {
  REQUIRED_MESON_VERSION=1.7

  # If meson exists and is new enough, nothing to do.
  if command -v meson >/dev/null 2>&1; then
    current=$(meson --version 2>/dev/null || echo "")
    if [ -n "$current" ] && version_ge "$current" "$REQUIRED_MESON_VERSION"; then
      return 0
    else
      printf 'Found Meson version %s but need >= %s; will try to install/upgrade Meson in a virtualenv.\n' "$current" "$REQUIRED_MESON_VERSION" >&2
    fi
  fi

  if ! command -v python3 >/dev/null 2>&1; then
    install_linux_packages python3
  fi

  if ! python3 -m pip --version >/dev/null 2>&1; then
    install_linux_packages python3-pip
  fi

  # Try to create and use a local virtualenv to avoid PEP 668/system pip issues.
  # Locate a requirements file either in the repo or the parent folder.
  req_file=""
  if [ -f "$repo_root/requirements.txt" ]; then
    req_file="$repo_root/requirements.txt"
  elif [ -f "$repo_root/../requirements.txt" ]; then
    req_file="$repo_root/../requirements.txt"
  elif [ -f "$script_dir/../requirements.txt" ]; then
    req_file="$script_dir/../requirements.txt"
  fi

  if [ -z "${VIRTUAL_ENV:-}" ]; then
    if python3 -m venv "$repo_root/.venv" >/dev/null 2>&1; then
      printf 'Created virtualenv at %s/.venv\n' "$repo_root" >&2
    else
      printf 'Creating virtualenv failed; attempting to install python3-venv and retry.\n' >&2
      install_linux_packages python3-venv || true
      if ! python3 -m venv "$repo_root/.venv" >/dev/null 2>&1; then
        printf 'Unable to create virtualenv; will try other fallbacks.\n' >&2
      fi
    fi

    if [ -f "$repo_root/.venv/bin/activate" ]; then
      # Activate the virtualenv for this script's execution.
      # shellcheck disable=SC1091
      . "$repo_root/.venv/bin/activate"
      python -m pip install --upgrade pip setuptools wheel >/dev/null 2>&1 || true

      if [ -n "$req_file" ]; then
        if ! python -m pip install -r "$req_file"; then
          printf 'pip install failed inside the created virtualenv.\n' >&2
          exit 1
        fi
      else
        printf 'No requirements.txt found (checked %s, %s); installing Meson only in the venv.\n' "$repo_root/requirements.txt" "$repo_root/../requirements.txt" >&2
      fi

      # Ensure Meson of sufficient version is available in the venv.
      if ! command -v meson >/dev/null 2>&1; then
        python -m pip install "meson>=$REQUIRED_MESON_VERSION" || true
      fi

      if command -v meson >/dev/null 2>&1; then
        current=$(meson --version 2>/dev/null || echo "")
        if [ -n "$current" ] && version_ge "$current" "$REQUIRED_MESON_VERSION"; then
          return 0
        fi
      fi
    fi
  else
    # Already inside a virtualenv.
    if ! python3 -m pip install -r "$repo_root/requirements.txt"; then
      printf 'pip install failed inside the virtualenv.\n' >&2
      exit 1
    fi
    if ! command -v meson >/dev/null 2>&1; then
      python3 -m pip install "meson>=$REQUIRED_MESON_VERSION" || true
    fi
    if command -v meson >/dev/null 2>&1; then
      current=$(meson --version 2>/dev/null || echo "")
      if [ -n "$current" ] && version_ge "$current" "$REQUIRED_MESON_VERSION"; then
        return 0
      fi
    fi
  fi

  # Fallback: try user install (may be blocked by PEP 668).
  if python3 -m pip install --user -r "$repo_root/requirements.txt" >/dev/null 2>&1; then
    export PATH="$HOME/.local/bin:$PATH"
    if command -v meson >/dev/null 2>&1; then
      current=$(meson --version 2>/dev/null || echo "")
      if [ -n "$current" ] && version_ge "$current" "$REQUIRED_MESON_VERSION"; then
        return 0
      fi
    fi
  fi

  # Last resort: try installing meson via apt, but verify the version.
  printf 'Attempting to install Meson using the system package manager (apt).\n' >&2
  install_linux_packages meson || true

  if command -v meson >/dev/null 2>&1; then
    current=$(meson --version 2>/dev/null || echo "")
    if [ -n "$current" ] && version_ge "$current" "$REQUIRED_MESON_VERSION"; then
      return 0
    else
      printf 'System Meson is version %s which is older than required %s.\n' "$current" "$REQUIRED_MESON_VERSION" >&2
      printf 'Please install Meson >= %s inside a virtualenv or via pipx (example: `python3 -m venv .venv && . .venv/bin/activate && python -m pip install meson>=%s`).\n' "$REQUIRED_MESON_VERSION" "$REQUIRED_MESON_VERSION" >&2
      exit 1
    fi
  fi

  printf 'meson is still missing after all attempts; please install Meson >= %s and re-run the script.\n' "$REQUIRED_MESON_VERSION" >&2
  exit 1
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
    # Only use the llvm native file if an LLD-compatible linker is available,
    # otherwise Meson may be invoked with -fuse-ld=lld which can fail.
    if command -v lld >/dev/null 2>&1 || command -v ld.lld >/dev/null 2>&1; then
      if [ "$cache_enabled" -eq 1 ]; then
        auto_native_file="$repo_root/scripts/llvm_native_ccache.txt"
      else
        auto_native_file="$repo_root/scripts/llvm_native.txt"
      fi
    else
      printf 'clang found but lld linker not found; skipping llvm native file to avoid -fuse-ld=lld.\n' >&2
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

# Run Meson and attempt a sensible fallback if a known subproject is missing.
if meson "$@"; then
  exit 0
else
  rc=$?
  log="$builddir/meson-logs/meson-log.txt"
  if [ -f "$log" ]; then
    if grep -q 'https://github.com/4jcraft/shiggy.git' "$log" 2>/dev/null || grep -q 'subproject shiggy is buildable' "$log" 2>/dev/null; then
      printf 'Meson setup failed due to missing subproject "shiggy"; retrying with -Dui_backend=java.\n' >&2
      if meson setup "$builddir" --reconfigure -Dui_backend=java; then
        printf 'Reconfigured build with ui_backend=java. Run `meson compile -C build -j $(nproc)` to build.\n' >&2
        exit 0
      else
        printf 'Retry with ui_backend=java failed; see %s for details.\n' "$log" >&2
        exit $?
      fi
    fi
  fi
  exit $rc
fi
