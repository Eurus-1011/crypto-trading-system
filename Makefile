.PHONY: all build clean test setup

all: build

setup:
	@bash scripts/install-hooks.sh

build:
	@mkdir -p build
	@cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -s -j$$(nproc 2>/dev/null || echo 4)

clean:
	@rm -rf build/

test:
	@cd build && ./okx_integration_test
