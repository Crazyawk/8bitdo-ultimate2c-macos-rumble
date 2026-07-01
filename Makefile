CC ?= clang
BUILD_DIR ?= build
CFLAGS ?= -arch x86_64 -O2 -Wall -Wextra
FRAMEWORKS = -framework IOKit -framework CoreFoundation

.PHONY: all clean
.PHONY: firmware-tools

all: \
	$(BUILD_DIR)/libSDL2-2.0.0.dylib \
	$(BUILD_DIR)/sdlshim_curve_ladder \
	$(BUILD_DIR)/sdlshim_gta_cancel_test \
	$(BUILD_DIR)/dlopen_real_sdl_test \
	$(BUILD_DIR)/bt_raw_all4_ladder

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/libSDL2-2.0.0.dylib: src/sdlshim/8bitdo_sdlshim.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -dynamiclib -undefined dynamic_lookup $(FRAMEWORKS) \
		-install_name @rpath/libSDL2-2.0.0.dylib \
		-o $@ $<

$(BUILD_DIR)/sdlshim_curve_ladder: tools/sdl/sdlshim_curve_ladder.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD_DIR)/sdlshim_gta_cancel_test: tools/sdl/sdlshim_gta_cancel_test.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD_DIR)/dlopen_real_sdl_test: tools/sdl/dlopen_real_sdl_test.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $<

$(BUILD_DIR)/bt_raw_all4_ladder: tools/bt-hid/bt_raw_all4_ladder.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(FRAMEWORKS) -o $@ $<

firmware-tools: \
	$(BUILD_DIR)/ultimate2c_boot_backup \
	$(BUILD_DIR)/ultimate2c_boot_flash

$(BUILD_DIR)/ultimate2c_boot_backup: tools/firmware/ultimate2c_boot_backup.c | $(BUILD_DIR)
	$(CC) -O2 -Wall -Wextra $(FRAMEWORKS) -o $@ $<

$(BUILD_DIR)/ultimate2c_boot_flash: tools/firmware/ultimate2c_boot_flash.c | $(BUILD_DIR)
	$(CC) -O2 -Wall -Wextra $(FRAMEWORKS) -o $@ $<

clean:
	rm -rf $(BUILD_DIR)
