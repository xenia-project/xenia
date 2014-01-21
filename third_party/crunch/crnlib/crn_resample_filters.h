// File: crn_resample_filters.h
// RG: This is public domain code, originally derived from Graphics Gems 3, see: http://code.google.com/p/imageresampler/
#pragma once

namespace crnlib
{
   typedef float (*resample_filter_func)(float t);
   
   struct resample_filter
   {
      char                 name[32];
      resample_filter_func func;
      float                support;
   };
   
   extern const resample_filter g_resample_filters[];
   extern const int g_num_resample_filters;
   
   int find_resample_filter(const char* pName);
   
} // namespace crnlib   
