.PHONY: all build source clean

all: build

build:
	@mkdir -p build
	@cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make -s -j$$(nproc 2>/dev/null || echo 4)

source:
	@mkdir -p build
	@cd build && cmake -DCMAKE_BUILD_TYPE=Release -DSIMDJSON_BUILD_FROM_SOURCE=ON .. && make -s -j$$(nproc 2>/dev/null || echo 4)

clean:
	@rm -rf build/
