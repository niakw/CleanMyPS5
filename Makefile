PYTHON ?= python3
CC = cc
PS5_CC = $(PS5_PAYLOAD_SDK)/bin/prospero-clang
CFLAGS = -O2 -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wpedantic
ASSETS = build/generated/assets.h
NATIVE = src/native/server.c src/native/inventory.c src/native/diagnostics.c src/native/duplicates.c src/native/media.c
all: server
$(ASSETS): scripts/embed_assets.py web/index.html web/styles.css web/app.js
	$(PYTHON) scripts/embed_assets.py $(ASSETS) web/index.html web/styles.css web/app.js
server: $(ASSETS) $(NATIVE) src/native/inventory.h src/native/diagnostics.h src/native/duplicates.h src/native/media.h src/native/media.h
	$(CC) $(CFLAGS) -Isrc/native -Ibuild/generated $(NATIVE) -o build/cleanmyps5-server
ps5: $(ASSETS) $(NATIVE) src/native/inventory.h src/native/diagnostics.h src/native/duplicates.h
	@test -n "$(PS5_PAYLOAD_SDK)" || (echo "Set PS5_PAYLOAD_SDK" && exit 1)
	$(PS5_CC) $(CFLAGS) -DPS5 -Isrc/native -Ibuild/generated $(NATIVE) -o cleanmyps5.elf
clean:
	rm -rf build cleanmyps5.elf
.PHONY: all server ps5 clean
