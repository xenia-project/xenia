/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/elf_module.h"

#include <algorithm>
#include <memory>

#include "xenia/base/byte_order.h"
#include "xenia/base/logging.h"
#include "xenia/cpu/processor.h"

namespace xe {
namespace cpu {

ElfModule::ElfModule(Processor* processor, kernel::KernelState* kernel_state)
    : Module(processor), kernel_state_(kernel_state) {}

ElfModule::~ElfModule() = default;

// ELF structures
struct elf32_ehdr {
  uint8_t e_ident[16];
  xe::be<uint16_t> e_type;
  xe::be<uint16_t> e_machine;
  xe::be<uint32_t> e_version;
  xe::be<uint32_t> e_entry;
  xe::be<uint32_t> e_phoff;
  xe::be<uint32_t> e_shoff;
  xe::be<uint32_t> e_flags;
  xe::be<uint16_t> e_ehsize;
  xe::be<uint16_t> e_phentsize;
  xe::be<uint16_t> e_phnum;
  xe::be<uint16_t> e_shentsize;
  xe::be<uint16_t> e_shnum;
  xe::be<uint16_t> e_shtrndx;
};

struct elf32_phdr {
  xe::be<uint32_t> p_type;
  xe::be<uint32_t> p_offset;
  xe::be<uint32_t> p_vaddr;
  xe::be<uint32_t> p_paddr;
  xe::be<uint32_t> p_filesz;
  xe::be<uint32_t> p_memsz;
  xe::be<uint32_t> p_flags;
  xe::be<uint32_t> p_align;
};

bool ElfModule::is_executable() const {
  auto hdr = reinterpret_cast<const elf32_ehdr*>(elf_header_mem_.data());
  return hdr->e_entry != 0;
}

bool ElfModule::Load(const std::string_view name, const std::string_view path,
                     const void* elf_addr, size_t elf_length) {
  name_ = name;
  path_ = path;

  uint8_t* pelf = (uint8_t*)elf_addr;
  elf32_ehdr* hdr = (elf32_ehdr*)(pelf + 0x0);
  if (hdr->e_ident[0] != 0x7F || hdr->e_ident[1] != 'E' ||
      hdr->e_ident[2] != 'L' || hdr->e_ident[3] != 'F') {
    // Not an ELF file!
    return false;
  }

  assert_true(hdr->e_ident[4] == 1);  // 32bit

  if (hdr->e_type != 2 /* ET_EXEC */) {
    // Not executable (shared objects not supported yet)
    XELOGE("ELF: Could not load ELF because it isn't executable!");
    return false;
  }

  if (hdr->e_machine != 20 /* EM_PPC */) {
    // Not a PPC ELF!
    XELOGE(
        "ELF: Could not load ELF because target machine is not PPC! (target: "
        "{})",
        uint32_t(hdr->e_machine));
    return false;
  }

  // Parse LOAD program headers and load into memory.
  if (!hdr->e_phoff) {
    XELOGE("ELF: File doesn't have a program header!");
    return false;
  }

  if (!hdr->e_entry) {
    XELOGE("ELF: Executable has no entry point!");
    return false;
  }

  // Entry point virtual address
  entry_point_ = hdr->e_entry;

  // Copy the ELF header
  elf_header_mem_.resize(hdr->e_ehsize);
  std::memcpy(elf_header_mem_.data(), hdr, hdr->e_ehsize);

  assert_true(hdr->e_phentsize == sizeof(elf32_phdr));
  elf32_phdr* phdr = (elf32_phdr*)(pelf + hdr->e_phoff);
  for (uint32_t i = 0; i < hdr->e_phnum; i++) {
    if (phdr[i].p_type == 1 /* PT_LOAD */ ||
        phdr[i].p_type == 2 /* PT_DYNAMIC */) {
      // Allocate and copy into memory.
      // Base address @ 0x80000000
      if (phdr[i].p_vaddr < 0x80000000 || phdr[i].p_vaddr > 0x9FFFFFFF) {
        XELOGE("ELF: Could not allocate memory for section @ address 0x{:08X}",
               uint32_t(phdr[i].p_vaddr));
        return false;
      }

      uint32_t virtual_addr = phdr[i].p_vaddr & ~(phdr[i].p_align - 1);
      uint32_t virtual_size = xe::round_up(phdr[i].p_vaddr + phdr[i].p_memsz,
                                           uint32_t(phdr[i].p_align)) -
                              virtual_addr;
      if (!memory()
               ->LookupHeap(virtual_addr)
               ->AllocFixed(
                   virtual_addr, virtual_size, phdr[i].p_align,
                   xe::kMemoryAllocationReserve | xe::kMemoryAllocationCommit,
                   xe::kMemoryProtectRead | xe::kMemoryProtectWrite)) {
        XELOGE("ELF: Could not allocate memory!");
      }

      auto p = memory()->TranslateVirtual(phdr[i].p_vaddr);
      std::memset(p, 0, phdr[i].p_memsz);
      std::memcpy(p, pelf + phdr[i].p_offset, phdr[i].p_filesz);

      // Notify backend about executable code.
      if (phdr[i].p_flags & 0x1 /* PF_X */) {
        processor_->backend()->CommitExecutableRange(
            virtual_addr, virtual_addr + virtual_size);
      }
    }
  }

  return true;
}

std::unique_ptr<Function> ElfModule::CreateFunction(uint32_t address) {
  return std::unique_ptr<Function>(
      processor_->backend()->CreateGuestFunction(this, address));
}

}  // namespace cpu
}  // namespace xe
