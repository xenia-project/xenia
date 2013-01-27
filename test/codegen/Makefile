BINUTILS=../../build/binutils/
PPC_AS=$(BINUTILS)/gas/as-new
PPC_LD=$(BINUTILS)/ld/ld-new
PPC_OBJDUMP=$(BINUTILS)/binutils/objdump

SRCS=$(wildcard *.s)
BINS=$(SRCS:.s=.bin)
DISASMS=$(SRCS:.s=.dis)

%.o: %.s
	$(PPC_AS) \
		-a64 \
		-be \
		-mregnames \
		-mpower7 \
		-maltivec \
		-mvsx \
		-R \
		-o $@ \
		$<

%.dis: %.o
	$(PPC_OBJDUMP) --adjust-vma=0x82010000 -Mpower7 -D -EB $< > $@

%.bin: %.o
	$(PPC_LD) \
		-A powerpc:common64 \
		-melf64ppc \
		-EB \
		-nostdlib \
		--oformat binary \
		-Ttext 0x82010000 \
		-e 0x82010000 \
		-o $@ \
		$<

all: $(BINS) $(DISASMS)
