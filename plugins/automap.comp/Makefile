WIT_DIR := wit
WIT_PACKAGE := gams:automap@1.0.0.wasm
WIT_WORLD := automap-plugin
COMPONENT_NAME := automap_plugin
WIT_STAMP := .wit-package.stamp
WASM_OUT := automap.comp.wasm

WIT_BINDGEN ?= wit-bindgen
WASI_P2_CC ?= wasm32-wasip2-clang
WASM_TOOLS ?= wasm-tools

.PHONY: all clean bindgen wasm wit

all: $(WASM_OUT)

wit: $(WIT_STAMP)

bindgen: .wit-bindgen.stamp

wasm: $(WASM_OUT)

$(WIT_STAMP): $(WIT_DIR)/package.wit
	../../script/wit-github-fallback.py --wit-dir $(WIT_DIR) --output '$(WIT_PACKAGE)'
	test -f '$(WIT_PACKAGE)'
	touch $@

.wit-bindgen.stamp: $(WIT_STAMP)
	rm -rf build
	mkdir -p build
	cd build && $(WIT_BINDGEN) c ../$(WIT_PACKAGE) -w $(WIT_WORLD)
	touch $@

$(WASM_OUT): component.c $(WIT_STAMP) .wit-bindgen.stamp
	$(WASI_P2_CC) -o $@ -mexec-model=reactor -Ibuild \
		-O2 -DNDEBUG \
		build/$(COMPONENT_NAME).c component.c build/$(COMPONENT_NAME)_component_type.o \
		-Wl,--strip-all
	$(WASM_TOOLS) strip -a $@ -o $@.strip
	mv $@.strip $@

clean:
	rm -rf build .wit-bindgen.stamp $(WIT_STAMP) $(WASM_OUT)
