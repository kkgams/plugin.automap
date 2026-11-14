SHELL := bash
.SHELLFLAGS := -eu -o pipefail -c
.DEFAULT_GOAL := build
PYTHON ?= python3
CURL ?= curl
GAMS_RELEASE_API ?= https://api.github.com/repos/kkgams/gams/releases/latest
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)
DOCS_PORT ?= 8080
WIKI_VERSION := v1.4.0
WIKI_REPOSITORY := https://github.com/justgook/wiki
WIKI_MAKEFILE := build.nosync/wiki-$(WIKI_VERSION).mk
WIKI_ENGINE := $(abspath build.nosync/wiki-$(WIKI_VERSION))
WIT := $(shell find plugins/automap.comp/wit -type f)
LEGAL := LICENSE NOTICE LICENSING.md $(sort $(wildcard LICENSES/*))
export PYTHONDONTWRITEBYTECODE := 1
export TMPDIR := $(abspath build.nosync/tmp)

.PHONY: build package clean open docs serve-docs validate-docs
build: build.nosync/plugin.automap.wasm
package: build.nosync/plugin.automap.wasm build.nosync/LICENSES.zip build.nosync/SHA256SUMS

build.nosync/tmp:
	mkdir -p "$@"

build.nosync/automap/automap_plugin.c build.nosync/automap/automap_plugin.h build.nosync/automap/automap_plugin_component_type.o &: $(WIT) Makefile flake.lock | build.nosync/tmp
	mkdir -p build.nosync/automap
	cd build.nosync/automap && wit-bindgen c ../../plugins/automap.comp/wit -w automap-plugin

build.nosync/TERMS: $(LEGAL) Makefile | build.nosync/tmp
	for file in $(LEGAL); do printf '\n--- %s ---\n' "$$file"; cat "$$file"; done > "$@"

build.nosync/plugin.automap.wasm: plugins/automap.comp/component.c build.nosync/automap/automap_plugin.c build.nosync/automap/automap_plugin.h build.nosync/automap/automap_plugin_component_type.o build.nosync/TERMS scripts/wasm-notices.py Makefile flake.lock
	wasm32-wasip2-clang -o "$@" -mexec-model=reactor -Ibuild.nosync/automap -O2 -DNDEBUG build.nosync/automap/automap_plugin.c plugins/automap.comp/component.c build.nosync/automap/automap_plugin_component_type.o -Wl,--strip-all
	wasm-tools strip -a "$@" -o "$@.strip"
	mv "$@.strip" "$@"
	$(PYTHON) scripts/wasm-notices.py embed "$@" --license build.nosync/TERMS --notice NOTICE
	wasm-tools validate "$@"

build.nosync/LICENSES.zip: $(LEGAL) Makefile | build.nosync/tmp
	rm -f "$@"
	zip -q -X "$@" $(LEGAL)

build.nosync/SHA256SUMS: build.nosync/plugin.automap.wasm build.nosync/LICENSES.zip
	cd build.nosync && shasum -a 256 plugin.automap.wasm LICENSES.zip > SHA256SUMS

clean:
	$(PYTHON) scripts/clean.py

# Retain an existing app; never update it implicitly.
tools/gams.app:
	@test "$(UNAME_S)" = Darwin && test "$(UNAME_M)" = arm64 || \
		{ echo 'GAMS release download requires macOS Apple Silicon' >&2; exit 1; }
	@test ! -e tools/gams-release-notices || \
		{ echo 'tools/gams-release-notices already exists; move it aside before reinstalling GAMS' >&2; exit 1; }
	@mkdir -p tools build.nosync/tmp
	@set -eu; stage=$$(mktemp -d "build.nosync/tmp/gams-download.XXXXXX"); \
		trap 'rm -rf "$$stage"' EXIT; \
		$(CURL) --fail --location --silent --show-error "$(GAMS_RELEASE_API)" --output "$$stage/release.json"; \
		url=$$($(PYTHON) -c 'import json, sys; release = json.load(open(sys.argv[1])); assets = [a for a in release["assets"] if a["name"].startswith("GAMS-") and a["name"].endswith("-macos-aarch64.zip")]; assert len(assets) == 1, "Expected exactly one GAMS macOS Apple Silicon ZIP asset"; print(assets[0]["browser_download_url"])' "$$stage/release.json"); \
		$(CURL) --fail --location --silent --show-error "$$url" --output "$$stage/gams.zip"; \
		unzip -q "$$stage/gams.zip" -d "$$stage/unpacked"; \
		test -f "$$stage/unpacked/GAMS.app/Contents/Info.plist"; \
		test -x "$$stage/unpacked/GAMS.app/Contents/MacOS/gams"; \
		mv "$$stage/unpacked/GAMS.app" "$$stage/GAMS.app"; \
		mv "$$stage/unpacked" tools/gams-release-notices; \
		mv "$$stage/GAMS.app" "$@"; \
		printf 'plugin.automap Makefile Host download\n' > tools/gams-release-notices/.gams-managed

open: build tools/gams.app
	GAMS_APP_CWD="$(CURDIR)" ./tools/gams.app/Contents/MacOS/gams

$(WIKI_MAKEFILE): | build.nosync/tmp
	$(CURL) --fail --location --retry 3 "$(WIKI_REPOSITORY)/raw/$(WIKI_VERSION)/Makefile" -o "$@.tmp"
	mv "$@.tmp" "$@"

docs serve-docs validate-docs: $(WIKI_MAKEFILE)
	$(MAKE) -f "$(WIKI_MAKEFILE)" "$(if $(filter docs,$@),build,$(patsubst %-docs,%,$@))" WIKI_REPOSITORY="$(WIKI_REPOSITORY)" WIKI_VERSION="$(WIKI_VERSION)" WIKI_ENGINE="$(WIKI_ENGINE)" WIKI_SOURCE="$(abspath docs)" WIKI_OUTPUT="$(abspath build.nosync/site)" PORT="$(DOCS_PORT)"
