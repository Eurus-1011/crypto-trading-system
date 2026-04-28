PREFIX ?= $(HOME)/.local

.PHONY: all build install source clean

all: build install

build:
	@mkdir -p build
	@cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -s -j$$(nproc 2>/dev/null || echo 4)

install: build
	@cmake --install build --prefix $(PREFIX) > /dev/null
	@echo "sdk installed to $(PREFIX)"

source:
	@mkdir -p build
	@cd build && cmake -DCMAKE_BUILD_TYPE=Release -DSIMDJSON_BUILD_FROM_SOURCE=ON .. && make -s -j$$(nproc 2>/dev/null || echo 4)

clean:
	@rm -rf build/
