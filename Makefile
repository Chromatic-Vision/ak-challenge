CFLAGS = -Wall -pedantic -std=c99
LDFLAGS = -lm

# CFLAGS += -ggdb

DEP = build/
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEP)/$*.d

EXE = conv

SRCFILES = main.c

OBJFILES = $(addprefix build/, $(patsubst %.c, %.o, $(SRCFILES)))

$(EXE): $(OBJFILES)
	$(CC) $^ -o $@ $(LDFLAGS)

build/%.o: src/%.c Makefile | build
	$(CC) $(DEPFLAGS) $(CFLAGS) -c $< -o $@

build:
	mkdir build

.PHONY: full
full: clean micg

.PHONY: clean
clean:
	rm -fv $(OBJFILES)
	rm -frv build
	rm -fv $(EXE) micg.*

DEPFILES := $(OBJFILES:%.o=%.d)
include $(wildcard $(DEPFILES))

