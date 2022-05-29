CC ?= clang
PKGCONFIG ?= pkg-config

FLAGS = -O2 -g
CFLAGS = $(FLAGS) `$(PKGCONFIG) --cflags gtk4`
LDFLAGS = $(FLAGS)
LDLIBS = `$(PKGCONFIG) --libs gtk4`

BUILD_DIR = build
TARGET = mosaic
SRCS = $(wildcard *.c)
OBJS = $(patsubst %.c, $(BUILD_DIR)/%.o, $(SRCS))

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $< $(LDLIBS)

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $^

$(BUILD_DIR):
	mkdir -p $@

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)
	rm -f mosaic
