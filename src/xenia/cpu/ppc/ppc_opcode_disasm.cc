/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstdint>
#include <cstdlib>

#include "xenia/base/assert.h"
#include "xenia/cpu/ppc/ppc_decode_data.h"
#include "xenia/cpu/ppc/ppc_opcode.h"
#include "xenia/cpu/ppc/ppc_opcode_disasm.h"
#include "xenia/cpu/ppc/ppc_opcode_info.h"

namespace xe {
namespace cpu {
namespace ppc {

void PadStringBuffer(StringBuffer* str, size_t base, size_t pad) {
  size_t added_len = str->length() - base;
  if (added_len < pad) str->AppendBytes(kSpaces, kNamePad - added_len);
}

void PrintDisasm_bcx(const PPCDecodeData& d, StringBuffer* str) {
  // [bc][LK][AA][+-] [BO], [BI], [ADDR]
  size_t str_start = str->length();

  const uint32_t bo = d.B.BO();
  const uint32_t bi = d.B.BI();
  const uint32_t addr = d.B.ADDR();

  const bool bo0 = bo & 0x10;
  const bool bo1 = bo & 0x08;
  const bool bo2 = bo & 0x04;
  const bool bo3 = bo & 0x02;
  const bool bo4 = bo & 0x01;
  int sign_char = 0;  // Default no sign

  if (bo0 && !bo1 && !bo2 && bo3 && !bo4) {
    str->Append("bdz");
  } else if (bo0 && bo1 && !bo2 && bo3 && !bo4) {
    str->Append("bdz");
    sign_char = -1;
  } else if (bo0 && bo1 && !bo2 && bo3 && bo4) {
    str->Append("bdz");
    sign_char = +1;
  } else if (bo0 && !bo1 && !bo2 && !bo3 && !bo4) {
    str->Append("bdnz");
  } else if (bo0 && bo1 && !bo2 && !bo3 && !bo4) {
    str->Append("bdnz");
    sign_char = -1;
  } else if (bo0 && bo1 && !bo2 && !bo3 && bo4) {
    str->Append("bdnz");
    sign_char = +1;
  } else if (!bo0 && !bo1 && bo2 && !bo3 && !bo4) {
    switch (bi % 4) {
      case 0x0:
        str->Append("bge");
        break;
      case 0x1:
        str->Append("ble");
        break;
      case 0x2:
        str->Append("bne");
        break;
    }
  } else if (!bo0 && !bo1 && bo2 && bo3 && !bo4) {
    switch (bi % 4) {
      case 0x0:
        str->Append("bge");
        sign_char = -1;
        break;
      case 0x1:
        str->Append("ble");
        sign_char = -1;
        break;
      case 0x2:
        str->Append("bne");
        sign_char = -1;
        break;
    }
  } else if (!bo0 && !bo1 && bo2 && bo3 && bo4) {
    switch (bi % 4) {
      case 0x0:
        str->Append("bge");
        sign_char = +1;
        break;
      case 0x1:
        str->Append("ble");
        sign_char = +1;
        break;
      case 0x2:
        str->Append("bne");
        sign_char = +1;
        break;
    }
  } else if (!bo0 && bo1 && bo2 && !bo3 && !bo4) {
    switch (bi % 4) {
      case 0x0:
        str->Append("blt");
        break;
      case 0x1:
        str->Append("bgt");
        break;
      case 0x2:
        str->Append("beq");
        break;
    }
  } else if (!bo0 && bo1 && bo2 && bo3 && !bo4) {
    switch (bi % 4) {
      case 0x0:
        str->Append("blt");
        sign_char = -1;
        break;
      case 0x1:
        str->Append("bgt");
        sign_char = -1;
        break;
      case 0x2:
        str->Append("beq");
        sign_char = -1;
        break;
    }
  } else if (!bo0 && bo1 && bo2 && bo3 && bo4) {
    switch (bi % 4) {
      case 0x0:
        str->Append("blt");
        sign_char = +1;
        break;
      case 0x1:
        str->Append("bgt");
        sign_char = +1;
        break;
      case 0x2:
        str->Append("beq");
        sign_char = +1;
        break;
    }
  }

  if (str_start == str->length()) {
    // Default
    str->Append("bc");
    if (d.B.LK()) str->Append('l');
    if (d.B.AA()) str->Append('a');
    PadStringBuffer(str, str_start, kNamePad);
    str->AppendFormat("{}", bo);
    str->Append(", ");
    str->AppendFormat("{}", bi);
  } else {
    if (d.B.LK()) str->Append('l');
    if (d.B.AA()) str->Append('a');

    if (sign_char > 0) {
      str->Append('+');
    } else if (sign_char < 0) {
      str->Append('-');
    }

    PadStringBuffer(str, str_start, kNamePad);
    str->AppendFormat("crf{}", bi / 4);
  }

  str->Append(", ");
  str->AppendFormat("0x{:X}", addr);
}

}  // namespace ppc
}  // namespace cpu
}  // namespace xe
