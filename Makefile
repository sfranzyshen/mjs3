PROG = mjs
MFLAGS = -g -O0
all: $(PROG)
.PHONY: test

$(PROG): mjs.c mjs.h
	$(CC) -o $@ mjs.c -W -Wall -DMJS_MAIN $(MFLAGS)
	./$@ -e 'let a = 1.23; a + 1;'

VC98 = docker run -v $(CURDIR):$(CURDIR) -w $(CURDIR) docker.io/mgos/vc98
vc98: mjs.c mjs.h 
	$(VC98) wine cl mjs.c /nologo /W4 /O1 /DNDEBUG /DMJS_MAIN /Fe$(PROG).exe
	$(VC98) wine $(PROG).exe -e '1 + 2 * 3.8 - 7 % 3'

test: unit_test.c mjs.c mjs.h
	$(CC) -o $@ mjs.c unit_test.c -W -Wall $(MFLAGS) && ./$@

test98: unit_test.c mjs.c mjs.h
	$(VC98) wine cl mjs.c unit_test.c /nologo /W4 /Os $(MFLAGS) /Fe$@.exe
	$(VC98) wine $@.exe

clean:
	rm -rf $(PROG) test *.exe *.obj
