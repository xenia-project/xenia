// File: crn_arealist.h - 2D shape algebra
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once

namespace crnlib
{
   struct Area
   {
      struct Area *Pprev, *Pnext;

      int x1, y1, x2, y2;
      
      uint get_width() const { return x2 - x1 + 1; }
      uint get_height() const { return y2 - y1 + 1; }
      uint get_area() const { return get_width() * get_height(); }
   };

   typedef Area * Area_Ptr;

   struct Area_List
   {
      int total_areas;
      int next_free;

      Area *Phead, *Ptail, *Pfree;
   };

   typedef Area_List * Area_List_Ptr;

   Area_List * Area_List_init(int max_areas);
   void Area_List_deinit(Area_List* Pobj_base);

   void Area_List_print(Area_List *Plist);

   Area_List * Area_List_dup_new(Area_List *Plist,
                                 int x_ofs, int y_ofs);
                                 
   uint Area_List_get_num(Area_List* Plist);                                 

   // src and dst area lists must have the same number of total areas.
   void Area_List_dup(Area_List *Psrc_list,
                      Area_List *Pdst_list,
                      int x_ofs, int y_ofs);

   void Area_List_copy(Area_List *Psrc_list,
                       Area_List *Pdst_list,
                       int x_ofs, int y_ofs);

   void Area_List_clear(Area_List *Plist);

   void Area_List_set(Area_List *Plist,
                      int x1, int y1, int x2, int y2);

   // logical: x and (not y)
   void Area_List_remove(Area_List *Plist,
                         int x1, int y1, int x2, int y2);

   // logical: x or y
   void Area_List_insert(Area_List *Plist,
                         int x1, int y1, int x2, int y2,
                         bool combine); 

   // logical: x and y
   void Area_List_intersect_area(Area_List *Plist,
                                 int x1, int y1, int x2, int y2);

   // logical: x and y
   void Area_List_intersect_Area_List(Area_List *Pouter_list,
                                      Area_List *Pinner_list,
                                      Area_List *Pdst_list);

   Area_List_Ptr Area_List_create_optimal(Area_List_Ptr Plist);

} // namespace crnlib
