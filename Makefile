PROG = mjs
CFLAGS += -W -Wall -Werror -Wstrict-overflow -fno-strict-aliasing -Os -g
MFLAGS += -DMJS_DEBUG
TFLAGS += -DMJS_PROP_POOL_SIZE=30 -DMJS_STRING_POOL_SIZE=200

all: $(PROG)
.PHONY: test $(PROG)

$(PROG): mjs.h main.c
	$(CC) -o $@ main.c -DNDEBUG $(CFLAGS) $(MFLAGS)

VC98 = docker run -v $(CURDIR):$(CURDIR) -w $(CURDIR) docker.io/mgos/vc98
vc98: mjs.h main.c
	$(VC98) wine cl main.c /nologo /W4 /O1 /DNDEBUG $(MFLAGS) /Fe$(PROG).exe
	$(VC98) wine $(PROG).exe -e 'let a = 1 + 2;'

test: unit_test.c mjs.h
	$(CC) -o $@ unit_test.c $(CFLAGS) $(MFLAGS) $(TFLAGS) && ./$@

test98: unit_test.c mjs.h
	$(VC98) wine cl unit_test.c /nologo /W4 /Os $(MFLAGS) $(TFLAGS) /Fe$@.exe
	$(VC98) wine $@.exe

clean:
	rm -rf $(PROG) test *.exe *.obj *.dSYM
