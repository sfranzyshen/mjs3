PROG = mjs
#MFLAGS += -DMJS_DEBUG
TFLAGS += -DMJS_PROP_POOL_SIZE=30 -DMJS_STRING_POOL_SIZE=200

all: $(PROG) test vc98 test98
.PHONY: test $(PROG)

CFLAGS += -W -Wall -Werror -Wstrict-overflow -fno-strict-aliasing -Os -g
$(PROG): mjs.h example.c
	$(CC) -o $@ example.c -DNDEBUG $(CFLAGS) $(MFLAGS)

test: unit_test.c mjs.h
	$(CC) -o $@ unit_test.c $(CFLAGS) $(MFLAGS) $(TFLAGS) && ./$@


VC98 = docker run -v $(CURDIR):$(CURDIR) -w $(CURDIR) docker.io/mgos/vc98
VCFLAGS = /nologo /W4 /O1
vc98: mjs.h example.c
	$(VC98) wine cl example.c $(VCFLAGS) /DNDEBUG $(MFLAGS) /Fe$(PROG).exe

test98: unit_test.c mjs.h
	$(VC98) wine cl unit_test.c $(VCFLAGS) $(MFLAGS) $(TFLAGS) /Fe$@.exe
	$(VC98) wine $@.exe


clean:
	rm -rf $(PROG) test *.exe *.obj *.dSYM
