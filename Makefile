.PHONY: all build install zip clean

all: build

build:
	./build.sh --no-install

install:
	./build.sh

zip: build
	mkdir -p dist
	cd build && zip -r -y "../dist/AORUS-RGB-Fusion-macOS.zip" "AORUS RGB Fusion.app" "aorus-led"
	@echo "Created dist/AORUS-RGB-Fusion-macOS.zip"

clean:
	rm -rf build dist
