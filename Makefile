PYTHON ?= python3
CC = cc
PS5_CC = $(PS5_PAYLOAD_SDK)/bin/prospero-clang
CFLAGS = -O2 -std=c11 -Wall -Wextra -Wpedantic
ASSETS = build/generated/assets.h
RULES = build/generated/scan_rules.h
NATIVE = src/native/server.c src/native/inventory.c src/native/diagnostics.c src/native/duplicates.c src/native/media.c src/native/volumes.c src/native/video.c src/native/preflight.c src/native/homebrew.c
all: server
$(RULES): rules/metadata-v1.json scripts/embed_rules.py
	$(PYTHON) scripts/embed_rules.py $< $@
$(ASSETS): VERSION scripts/embed_assets.py web/index.html web/styles.css web/app.js
	$(PYTHON) scripts/embed_assets.py $(ASSETS) web/index.html web/styles.css web/app.js
server: $(ASSETS) $(RULES) $(NATIVE) src/native/inventory.h src/native/diagnostics.h src/native/duplicates.h src/native/media.h src/native/volumes.h src/native/video.h src/native/preflight.h src/native/homebrew.h
	$(CC) $(CFLAGS) -pthread -Isrc/native -Ibuild/generated $(NATIVE) -o build/cleanmyps5-server
ps5: $(ASSETS) $(RULES) $(NATIVE) src/native/inventory.h src/native/diagnostics.h src/native/duplicates.h src/native/media.h src/native/volumes.h src/native/video.h src/native/preflight.h src/native/homebrew.h
	@test -n "$(PS5_PAYLOAD_SDK)" || (echo "Set PS5_PAYLOAD_SDK" && exit 1)
	$(PS5_CC) $(CFLAGS) -DPS5 -DCMP5_AUTO_BROWSER -Isrc/native -Ibuild/generated $(NATIVE) -lSceSystemService -lSceUserService -o cleanmyps5.elf
package: ps5
	$(PYTHON) scripts/package_release.py cleanmyps5.elf dist
clean:
	rm -rf build cleanmyps5.elf
.PHONY: all server ps5 package clean
