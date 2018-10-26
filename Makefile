PROG = mjs
all: $(PROG)
  
$(PROG): mjs.c mjs.h
	$(CC) -o $@ mjs.c -W -Wall -DMJS_MAIN

VC98 = docker run -v $(CURDIR):$(CURDIR) -w $(CURDIR) docker.io/mgos/vc98
vc98: mjs.c mjs.h 
	$(VC98) wine cl mjs.c /nologo /W3 /O2 -DMJS_MAIN /Fe$(PROG).exe
	$(VC98) wine $(PROG).exe -h
