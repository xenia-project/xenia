BINUTILS=../../../third_party/binutils/bin/
PPC_AS=$(BINUTILS)/powerpc-none-elf-as
PPC_LD=$(BINUTILS)/powerpc-none-elf-ld
PPC_OBJDUMP=$(BINUTILS)/powerpc-none-elf-objdump

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
		-mvmx128 \
		-R \
		-o $@ \
		$<

%.dis: %.o
	$(PPC_OBJDUMP) --adjust-vma=0x100000 -Mpower7 -Mvmx128 -D -EB $< > $@

%.bin: %.o
	$(PPC_LD) \
		-A powerpc:common64 \
		-melf64ppc \
		-EB \
		-nostdlib \
		--oformat binary \
		-Ttext 0x100000 \
		-e 0x100000 \
		-o $@ \
		$<

all: $(BINS) $(DISASMS)
