PROG = mjs
CFLAGS += -W -Wall -Werror -Wstrict-overflow -fno-strict-aliasing
OFLAGS += -Os
DFLAGS += -g -O0 -DMJS_DEBUG
TFLAGS = -DMJS_PROP_POOL_SIZE=30 -DMJS_STRING_POOL_SIZE=200
all: $(PROG)
.PHONY: test $(PROG)

$(PROG): mjs.h
	$(CC) -o $@ $^ $(CFLAGS) -DMJS_MAIN $(OFLAGS)

VC98 = docker run -v $(CURDIR):$(CURDIR) -w $(CURDIR) docker.io/mgos/vc98
vc98: mjs.h
	$(VC98) wine cl $^ /nologo /TC /W4 /O1 /DNDEBUG /DMJS_DEBUG /DMJS_MAIN /Fe$(PROG).exe
	$(VC98) wine $(PROG).exe -e '1 + 2 * 3.8 - 7 % 3'

test: unit_test.c mjs.h
	$(CC) -o $@ unit_test.c $(CFLAGS) $(DFLAGS) $(TFLAGS) && ./$@

test98: unit_test.c mjs.h
	$(VC98) wine cl unit_test.c /nologo /W4 /Os /DMJS_DEBUG $(TFLAGS) /Fe$@.exe
	$(VC98) wine $@.exe

clean:
	rm -rf $(PROG) test *.exe *.obj *.dSYM
