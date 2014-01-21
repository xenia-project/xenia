// File: crn_value.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
#include "crn_core.h"
#include "crn_value.h"

namespace crnlib
{
   const char* gValueDataTypeStrings[cDTTotal + 1] =
   {
      "invalid",
      "string",
      "bool",
      "int",
      "uint",
      "float",
      "vec3f",
      "vec3i",

      NULL,
   };

} // namespace crnlib
