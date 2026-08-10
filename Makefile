# Minecraft: Console Edition — workspace Makefile
#
# Wraps the Meson/Ninja flow documented in 4jcraft/README.md so the common
# operations are one command each. Every target is phony: Meson/Ninja own the
# dependency graph, this file only drives them.
#
# Quick start:
#   make deps      # system packages (Debian/Ubuntu)
#   make setup     # configure the build dir
#   make build     # compile the client
#   make run       # launch the game

SHELL := /bin/bash
.SHELLFLAGS := -eu -o pipefail -c

# --- Layout -----------------------------------------------------------------

PROJECT_DIR := 4jcraft
BUILD_DIR   ?= build
BUILD_PATH  := $(PROJECT_DIR)/$(BUILD_DIR)
CLIENT_DIR  := $(BUILD_PATH)/Minecraft.Client
CLIENT_BIN  := $(CLIENT_DIR)/Minecraft.Client
VENV_DIR    := $(PROJECT_DIR)/.venv

# --- Build configuration (override on the command line) ---------------------
#
#   make setup BUILDTYPE=release RENDERER=gl3
#
# RENDERER=gles is the verified Linux path and does not require GLU.

BUILDTYPE  ?= debugoptimized
RENDERER   ?= gles
UNITY      ?= on
UI_BACKEND ?=
JOBS       ?= $(shell nproc 2>/dev/null || echo 4)
PREFIX     ?= $(HOME)/.local

# Extra flags appended to `meson setup`, e.g. make setup SETUP_ARGS=-Denable_vsync=false
SETUP_ARGS ?=

# Prefer the project-local virtualenv's Meson (the distro package is often
# older than the required 1.7). Falls back to whatever is on PATH.
MESON := $(shell if [ -x "$(VENV_DIR)/bin/meson" ]; then \
                   printf '%s' "$(VENV_DIR)/bin/meson"; \
                 else \
                   command -v meson || printf 'meson'; \
                 fi)
PYTHON := $(shell if [ -x "$(VENV_DIR)/bin/python" ]; then \
                    printf '%s' "$(VENV_DIR)/bin/python"; \
                  else \
                    command -v python3 || printf 'python3'; \
                  fi)

ui_backend_flag := $(if $(UI_BACKEND),-Dui_backend=$(UI_BACKEND),)

.DEFAULT_GOAL := help

# --- Help -------------------------------------------------------------------

.PHONY: help
help: ## Show this help
	@printf '\nMinecraft: Console Edition — available targets\n\n'
	@grep -hE '^[a-zA-Z0-9_-]+:.*?## ' $(MAKEFILE_LIST) \
		| sort \
		| awk 'BEGIN {FS = ":.*?## "} {printf "  \033[36m%-16s\033[0m %s\n", $$1, $$2}'
	@printf '\nConfiguration (current values):\n'
	@printf '  BUILDTYPE=%s  RENDERER=%s  UNITY=%s  JOBS=%s\n' \
		'$(BUILDTYPE)' '$(RENDERER)' '$(UNITY)' '$(JOBS)'
	@printf '  BUILD_DIR=%s  PREFIX=%s\n' '$(BUILD_DIR)' '$(PREFIX)'
	@printf '  meson: %s\n\n' '$(MESON)'

# --- Environment ------------------------------------------------------------

.PHONY: deps
deps: ## Install the system build dependencies (Debian/Ubuntu)
	sudo apt-get update
	sudo apt-get install -y build-essential ccache python3 python3-pip python3-venv \
		ninja-build pkg-config libsdl2-dev libgl-dev libglu1-mesa-dev \
		libgles2-mesa-dev libpthread-stubs0-dev libglm-dev zlib1g-dev

.PHONY: venv
venv: ## Create the local Python venv, install Meson and AI dependencies training dependencies
	python3 -m venv "$(VENV_DIR)"
	"$(VENV_DIR)/bin/python" -m pip install --upgrade pip setuptools wheel
	"$(VENV_DIR)/bin/python" -m pip install -r requirements.txt
	"$(VENV_DIR)/bin/python" -m pip install -r mods/CuriousMob/ai/requirements.txt
	"$(VENV_DIR)/bin/python" -m pip install -r mods/CuriousMob/ai/requirements-train.txt

.PHONY: doctor
doctor: ## Report which toolchain pieces are present
	@printf 'Toolchain check:\n'
	@for tool in cc c++ clang clang++ ninja pkg-config ccache python3 clang-format gdb; do \
		if command -v "$$tool" >/dev/null 2>&1; then \
			printf '  \033[32mok\033[0m      %s (%s)\n' "$$tool" "$$(command -v $$tool)"; \
		else \
			printf '  \033[33mmissing\033[0m %s\n' "$$tool"; \
		fi; \
	done
	@if [ -x "$(MESON)" ] || command -v "$(MESON)" >/dev/null 2>&1; then \
		printf '  \033[32mok\033[0m      meson %s (%s)\n' "$$("$(MESON)" --version)" "$(MESON)"; \
	else \
		printf '  \033[31mmissing\033[0m meson — run: make venv\n'; \
	fi
	@for lib in sdl2 glesv2 gl glu zlib; do \
		if pkg-config --exists "$$lib" 2>/dev/null; then \
			printf '  \033[32mok\033[0m      %s %s\n' "$$lib" "$$(pkg-config --modversion $$lib)"; \
		else \
			printf '  \033[33mmissing\033[0m %s (pkg-config)\n' "$$lib"; \
		fi; \
	done
	@if [ -f "$(BUILD_PATH)/build.ninja" ]; then \
		printf '  \033[32mok\033[0m      build dir configured at %s\n' '$(BUILD_PATH)'; \
	else \
		printf '  \033[33mmissing\033[0m build dir — run: make setup\n'; \
	fi

# --- Configure --------------------------------------------------------------

.PHONY: setup
setup: ## Configure the build dir (reuses it via --reconfigure when it exists)
	BUILDTYPE='$(BUILDTYPE)' RENDERER='$(RENDERER)' UNITY='$(UNITY)' \
		./$(PROJECT_DIR)/scripts/setup_build.sh '$(BUILD_DIR)' $(ui_backend_flag) $(SETUP_ARGS)

.PHONY: setup-llvm
setup-llvm: ## Configure using the CI clang + lld + ccache toolchain
	BUILDTYPE='$(BUILDTYPE)' RENDERER='$(RENDERER)' UNITY='$(UNITY)' \
		./$(PROJECT_DIR)/scripts/setup_build.sh '$(BUILD_DIR)' \
		--native-file=./scripts/llvm_native_ccache.txt $(ui_backend_flag) $(SETUP_ARGS)

.PHONY: reconfigure
reconfigure: ## Re-apply build options to the existing build dir
	"$(MESON)" setup "$(BUILD_PATH)" "$(PROJECT_DIR)" --reconfigure \
		-Dbuildtype=$(BUILDTYPE) -Drenderer=$(RENDERER) -Dunity=$(UNITY) \
		$(ui_backend_flag) $(SETUP_ARGS)

.PHONY: wipe
wipe: ## Recreate the build dir from scratch (use when switching toolchain)
	BUILDTYPE='$(BUILDTYPE)' RENDERER='$(RENDERER)' UNITY='$(UNITY)' \
		./$(PROJECT_DIR)/scripts/setup_build.sh '$(BUILD_DIR)' --wipe $(ui_backend_flag) $(SETUP_ARGS)

# --- Build ------------------------------------------------------------------

# Configure on demand so `make build` works on a fresh clone.
.PHONY: ensure-setup
ensure-setup: ## Configure the build dir if it doesn't exist (run automatically by build)
	@if [ ! -f "$(BUILD_PATH)/build.ninja" ]; then \
		printf 'No build dir at %s; configuring first.\n' '$(BUILD_PATH)'; \
		$(MAKE) setup; \
	fi

.PHONY: build
build: ensure-setup ## Compile the game client
	"$(MESON)" compile -C "$(BUILD_PATH)" -j $(JOBS) Minecraft.Client

.PHONY: all
all: build ## Alias for build

.PHONY: verbose
verbose: ensure-setup ## Compile with full compiler command lines
	"$(MESON)" compile -C "$(BUILD_PATH)" -j $(JOBS) -v Minecraft.Client

.PHONY: assets
assets: ensure-setup ## Rebuild the packed assets (.arc/.loc/.col) and copy them next to the binary
	"$(MESON)" compile -C "$(BUILD_PATH)" -j $(JOBS) \
		Minecraft.Assets_Localisation Minecraft.Assets_Colour_Table \
		Minecraft.Media_Archive copy_assets_to_client

.PHONY: rebuild
rebuild: wipe build ## Wipe the build dir and compile from scratch

.PHONY: debug release
debug: ## Build a full-debug binary
	$(MAKE) build BUILDTYPE=debug

release: ## Build an optimised release binary
	$(MAKE) build BUILDTYPE=release

# --- Run --------------------------------------------------------------------
#
# The client resolves Common/, music/ and Sound/ relative to its working
# directory, so it must be launched from inside the client build dir.

.PHONY: run
run: build ## Build and launch the game
	cd "$(CLIENT_DIR)" && CURIOUSMOB_SPAWN=1 ./Minecraft.Client $(ARGS)

.PHONY: run-only
run-only: ## Launch the existing binary without rebuilding
	@if [ ! -x "$(CLIENT_BIN)" ]; then \
		printf 'No binary at %s — run: make build\n' '$(CLIENT_BIN)' >&2; \
		exit 1; \
	fi
	cd "$(CLIENT_DIR)" && ./Minecraft.Client $(ARGS)

.PHONY: gdb
gdb: build ## Launch the game under gdb
	cd "$(CLIENT_DIR)" && gdb --args ./Minecraft.Client $(ARGS)

# --- AI (CuriousMob) ---------------------------------------------------------
#
# Treina o bot CuriousMob via PPO contra o jogo ao vivo. Requer o jogo já
# rodando com `make run` (ele exporta CURIOUSMOB_SPAWN=1 e abre a ponte).
# Requer as dependências de treino: `make venv`.
#
#   make train-ai                        # treino padrão, 100k passos
#   make train-ai ARGS="--steps 10000"   # sessão curta
#   make train-ai ARGS="--rnd"           # com curiosidade RND
#   make train-ai ARGS="--resume 4jcraft/models/ppo_curiousmob.zip"
#
.PHONY: train-ai
train-ai: ## Treina o CuriousMob com PPO (jogo precisa estar rodando)
	"$(PYTHON)" mods/CuriousMob/ai/train.py $(ARGS)

.PHONY: smoke
smoke: build ## Boot the game for SMOKE_SECONDS and confirm it reaches the main menu
	@log=$$(mktemp); \
	printf 'Booting client for %ss...\n' '$(SMOKE_SECONDS)'; \
	if timeout $(SMOKE_SECONDS) sh -c 'cd "$(CLIENT_DIR)" && ./Minecraft.Client' >"$$log" 2>&1; then \
		status=0; \
	else \
		status=$$?; \
	fi; \
	if [ "$$status" -ne 124 ] && [ "$$status" -ne 0 ]; then \
		printf '\033[31mFAIL\033[0m client exited early (status %s):\n' "$$status"; \
		tail -20 "$$log"; rm -f "$$log"; exit 1; \
	fi; \
	if grep -q 'Loaded iggy movie MainMenu' "$$log"; then \
		printf '\033[32mOK\033[0m client booted and reached the main menu.\n'; \
		rm -f "$$log"; \
	else \
		printf '\033[31mFAIL\033[0m client never reached the main menu:\n'; \
		tail -20 "$$log"; rm -f "$$log"; exit 1; \
	fi

SMOKE_SECONDS ?= 20

# --- Install ----------------------------------------------------------------

.PHONY: install
install: build ## Install into PREFIX (default: ~/.local)
	"$(MESON)" setup "$(BUILD_PATH)" "$(PROJECT_DIR)" --reconfigure --prefix="$(PREFIX)"
	"$(MESON)" compile -C "$(BUILD_PATH)" -j $(JOBS) Minecraft.Client
	"$(MESON)" install -C "$(BUILD_PATH)"

# --- Quality ----------------------------------------------------------------

.PHONY: format
format: ## Reformat the tracked C/C++ sources with clang-format
	cd "$(PROJECT_DIR)" && git ls-files \
		'*.c' '*.cc' '*.cpp' '*.cxx' '*.h' '*.hh' '*.hpp' '*.hxx' '*.inl' \
		| xargs -r clang-format -i
	@printf 'Formatted. Review with: git diff\n'

.PHONY: format-check
format-check: ## Check formatting of the sources changed against origin/main
	cd "$(PROJECT_DIR)" && bash ./.github/scripts/check-clang-format.sh \
		"$$(git merge-base origin/main HEAD 2>/dev/null || git rev-parse HEAD~1)" HEAD

.PHONY: compdb
compdb: ensure-setup ## Refresh compile_commands.json for editors/clangd
	@ln -sf "$(BUILD_DIR)/compile_commands.json" "$(PROJECT_DIR)/compile_commands.json"
	@printf 'Linked %s/compile_commands.json\n' '$(PROJECT_DIR)'

.PHONY: log
log: ## Show the tail of the last Meson configure log
	@tail -40 "$(BUILD_PATH)/meson-logs/meson-log.txt"

# --- Clean ------------------------------------------------------------------

.PHONY: clean
clean: ## Remove compiled objects, keeping the build configuration
	@if [ -f "$(BUILD_PATH)/build.ninja" ]; then \
		ninja -C "$(BUILD_PATH)" -t clean; \
	else \
		printf 'Nothing to clean.\n'; \
	fi

.PHONY: distclean
distclean: ## Delete the whole build dir
	rm -rf "$(BUILD_PATH)"

.PHONY: mrproper
mrproper: distclean ## Delete the build dir and the local Python venv
	rm -rf "$(VENV_DIR)"
