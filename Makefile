CFLAGS = -Wall -pedantic -std=gnu99
LDFLAGS = -lm

CFLAGS += -ggdb

DEP = build/
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEP)/$*.d

# https://stackoverflow.com/a/12099167
ifeq ($(OS),Windows_NT)
	EXE = conv.exe
else
	EXE = conv
endif

SRCFILES = main.c

OBJFILES = $(addprefix build/, $(patsubst %.c, %.o, $(SRCFILES)))

$(EXE): $(OBJFILES)
	$(CC) $^ -o $@ $(LDFLAGS)

build/%.o: src/%.c Makefile | build
	$(CC) $(DEPFLAGS) $(CFLAGS) -c $< -o $@

build:
	mkdir build

.PHONY: full
full: clean $(EXE)

.PHONY: clean
clean:
	rm -fv $(OBJFILES)
	rm -frv build
	rm -fv $(EXE)

DEPFILES := $(OBJFILES:%.o=%.d)
include $(wildcard $(DEPFILES))

