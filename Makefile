.DEFAULT_GOAL := all
.DELETE_ON_ERROR:
 
NAME      := fish
BUILD_DIR ?= build
PREFIX    ?= /usr/local
DESTDIR   ?=
 
$(if $(strip $(BUILD_DIR)),,$(error BUILD_DIR shouldn't be empty))
 
CC      ?= gcc
CFLAGS  ?= -O2 -pipe
LDFLAGS ?=
LDLIBS  ?=
 
ALL_CFLAGS  := -std=gnu11 -Wall -Wextra $(CFLAGS)
ALL_LDFLAGS := $(LDFLAGS)
ALL_LDLIBS  := $(LDLIBS) -lm
 
SRC   := $(sort $(wildcard *.c))
OBJ   := $(SRC:%.c=$(BUILD_DIR)/%.o)
BIN   := $(BUILD_DIR)/$(NAME)
STAMP := $(BUILD_DIR)/.flags
 
# V=1 — полные команды, иначе короткие строки
ifeq ($(V),1)
  Q :=
  say = @:
else
  Q := @
  say = @printf '  %-3s %s\n' $(1) $(2)
endif
 
.PHONY: all run debug install uninstall clean help
 
all: $(BIN)
 
$(BIN): $(OBJ)
	$(call say,LD,$@)
	$(Q)$(CC) $(ALL_CFLAGS) $(ALL_LDFLAGS) -o $@ $(OBJ) $(ALL_LDLIBS)
 
$(BUILD_DIR)/%.o: %.c $(STAMP) | $(BUILD_DIR)
	$(call say,CC,$<)
	$(Q)$(CC) $(ALL_CFLAGS) -MMD -MP -c -o $@ $<
 
$(STAMP): FORCE | $(BUILD_DIR)
	@printf '%s\n' '$(CC) $(ALL_CFLAGS) $(ALL_LDFLAGS) $(ALL_LDLIBS)' | cmp -s - $@ \
	  || printf '%s\n' '$(CC) $(ALL_CFLAGS) $(ALL_LDFLAGS) $(ALL_LDLIBS)' > $@
 
$(BUILD_DIR):
	@mkdir -p $@
 
FORCE:
 
run: all
	$(BIN) $(ARGS)
 
debug:
	$(Q)$(MAKE) --no-print-directory BUILD_DIR=$(BUILD_DIR)/debug \
	  CFLAGS='-O0 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined' \
	  LDFLAGS='-fsanitize=address,undefined' all
 
install: all
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(NAME)
 
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(NAME)
 
clean:
	rm -rf $(BUILD_DIR)
 
help:
	@sed -n '3,9p' $(firstword $(MAKEFILE_LIST)) | sed 's/^# \{0,1\}//'
 
-include $(OBJ:.o=.d)
 