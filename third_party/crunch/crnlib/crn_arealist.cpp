// File: crn_arealist.cpp - 2D shape algebra (currently unused)
// See Copyright Notice and license at the end of inc/crnlib.h
// Ported from the PowerView DOS image viewer, a product I wrote back in 1993. Not currently used in the open source release of crnlib.
#include "crn_core.h"
#include "crn_arealist.h"

#define RECT_DEBUG

namespace crnlib
{

   static void area_fatal_error(const char* pFunc, const char* pMsg, ...)
   {
      pFunc;
      va_list args;
      va_start(args, pMsg);

      char buf[512];
#ifdef _MSC_VER
      _vsnprintf_s(buf, sizeof(buf), pMsg, args);
#else
      vsnprintf(buf, sizeof(buf), pMsg, args);
#endif

      va_end(args);

      CRNLIB_FAIL(buf);
   }

   static Area * delete_area(Area_List *Plist, Area *Parea)
   {
      Area *p, *q;

   #ifdef RECT_DEBUG
      if ((Parea == Plist->Phead) || (Parea == Plist->Ptail))
         area_fatal_error("delete_area", "tried to remove head or tail");
   #endif

      p = Parea->Pprev;
      q = Parea->Pnext;
      p->Pnext = q;
      q->Pprev = p;

      Parea->Pnext = Plist->Pfree;
      Parea->Pprev = NULL;
      Plist->Pfree = Parea;

      return (q);
   }

   static Area * alloc_area(Area_List *Plist)
   {
      Area *p = Plist->Pfree;

      if (p == NULL)
      {
         if (Plist->next_free == Plist->total_areas)
            area_fatal_error("alloc_area", "Out of areas!");

         p = Plist->Phead + Plist->next_free;
         Plist->next_free++;
      }
      else
         Plist->Pfree = p->Pnext;

      return (p);
   }

   static Area * insert_area_before(Area_List *Plist, Area *Parea,
                                    int x1, int y1, int x2, int y2)
   {
      Area *p, *Pnew_area = alloc_area(Plist);

      p = Parea->Pprev;

      p->Pnext = Pnew_area;

      Pnew_area->Pprev = p;
      Pnew_area->Pnext = Parea;

      Parea->Pprev = Pnew_area;

      Pnew_area->x1 = x1;
      Pnew_area->y1 = y1;
      Pnew_area->x2 = x2;
      Pnew_area->y2 = y2;

      return (Pnew_area);
   }

   static Area * insert_area_after(Area_List *Plist, Area *Parea,
                                   int x1, int y1, int x2, int y2)
   {
      Area *p, *Pnew_area = alloc_area(Plist);

      p = Parea->Pnext;

      p->Pprev = Pnew_area;

      Pnew_area->Pnext = p;
      Pnew_area->Pprev = Parea;

      Parea->Pnext = Pnew_area;

      Pnew_area->x1 = x1;
      Pnew_area->y1 = y1;
      Pnew_area->x2 = x2;
      Pnew_area->y2 = y2;

      return (Pnew_area);
   }

   void Area_List_deinit(Area_List* Pobj_base)
   {
      Area_List *Plist = (Area_List *)Pobj_base;

      if (!Plist)
         return;

      if (Plist->Phead)
      {
         crnlib_free(Plist->Phead);
         Plist->Phead = NULL;
      }

      crnlib_free(Plist);
   }

   Area_List * Area_List_init(int max_areas)
   {
      Area_List *Plist = (Area_List*)crnlib_calloc(1, sizeof(Area_List));

      Plist->total_areas = max_areas + 2;

      Plist->Phead = (Area *)crnlib_calloc(max_areas + 2, sizeof(Area));
      Plist->Ptail = Plist->Phead + 1;

      Plist->Phead->Pprev = NULL;
      Plist->Phead->Pnext = Plist->Ptail;

      Plist->Ptail->Pprev = Plist->Phead;
      Plist->Ptail->Pnext = NULL;

      Plist->Pfree = NULL;
      Plist->next_free = 2;

      return (Plist);
   }

   void Area_List_print(Area_List *Plist)
   {
      Area *Parea = Plist->Phead->Pnext;

      while (Parea != Plist->Ptail)
      {
         printf("%04i %04i : %04i %04i\n", Parea->x1, Parea->y1, Parea->x2, Parea->y2);

         Parea = Parea->Pnext;
      }
   }

   Area_List * Area_List_dup_new(Area_List *Plist,
                                 int x_ofs, int y_ofs)
   {
      int i;
      Area_List *Pnew_list = (Area_List*)crnlib_calloc(1, sizeof(Area_List));

      Pnew_list->total_areas = Plist->total_areas;

      Pnew_list->Phead = (Area *)crnlib_malloc(sizeof(Area) * Plist->total_areas);
      Pnew_list->Ptail = Pnew_list->Phead + 1;

      Pnew_list->Pfree = (Plist->Pfree) ? ((Plist->Pfree - Plist->Phead) + Pnew_list->Phead) : NULL;

      Pnew_list->next_free = Plist->next_free;

      memcpy(Pnew_list->Phead, Plist->Phead, sizeof(Area) * Plist->total_areas);

      for (i = 0; i < Plist->total_areas; i++)
      {
         Pnew_list->Phead[i].Pnext = (Plist->Phead[i].Pnext == NULL) ? NULL : (Plist->Phead[i].Pnext - Plist->Phead) + Pnew_list->Phead;
         Pnew_list->Phead[i].Pprev = (Plist->Phead[i].Pprev == NULL) ? NULL : (Plist->Phead[i].Pprev - Plist->Phead) + Pnew_list->Phead;

         Pnew_list->Phead[i].x1 += x_ofs;
         Pnew_list->Phead[i].y1 += y_ofs;
         Pnew_list->Phead[i].x2 += x_ofs;
         Pnew_list->Phead[i].y2 += y_ofs;
      }

      return (Pnew_list);
   }

   uint Area_List_get_num(Area_List* Plist)
   {
      uint num = 0;

      Area *Parea = Plist->Phead->Pnext;

      while (Parea != Plist->Ptail)
      {
         num++;

         Parea = Parea->Pnext;
      }

      return num;
   }

   void Area_List_dup(Area_List *Psrc_list, Area_List *Pdst_list,
                      int x_ofs, int y_ofs)
   {
      int i;

      if (Psrc_list->total_areas != Pdst_list->total_areas)
         area_fatal_error("Area_List_dup", "Src and Dst total_areas must be equal!");

      Pdst_list->Pfree = (Psrc_list->Pfree) ? ((Psrc_list->Pfree - Psrc_list->Phead) + Pdst_list->Phead) : NULL;

      Pdst_list->next_free = Psrc_list->next_free;

      memcpy(Pdst_list->Phead, Psrc_list->Phead, sizeof(Area) * Psrc_list->total_areas);

      if ((x_ofs) || (y_ofs))
      {
         for (i = 0; i < Psrc_list->total_areas; i++)
         {
            Pdst_list->Phead[i].Pnext = (Psrc_list->Phead[i].Pnext == NULL) ? NULL : (Psrc_list->Phead[i].Pnext - Psrc_list->Phead) + Pdst_list->Phead;
            Pdst_list->Phead[i].Pprev = (Psrc_list->Phead[i].Pprev == NULL) ? NULL : (Psrc_list->Phead[i].Pprev - Psrc_list->Phead) + Pdst_list->Phead;

            Pdst_list->Phead[i].x1 += x_ofs;
            Pdst_list->Phead[i].y1 += y_ofs;
            Pdst_list->Phead[i].x2 += x_ofs;
            Pdst_list->Phead[i].y2 += y_ofs;
         }
      }
      else
      {
         for (i = 0; i < Psrc_list->total_areas; i++)
         {
            Pdst_list->Phead[i].Pnext = (Psrc_list->Phead[i].Pnext == NULL) ? NULL : (Psrc_list->Phead[i].Pnext - Psrc_list->Phead) + Pdst_list->Phead;
            Pdst_list->Phead[i].Pprev = (Psrc_list->Phead[i].Pprev == NULL) ? NULL : (Psrc_list->Phead[i].Pprev - Psrc_list->Phead) + Pdst_list->Phead;
         }
      }
   }

   void Area_List_copy(
                       Area_List *Psrc_list, Area_List *Pdst_list,
                       int x_ofs, int y_ofs)
   {
      Area *Parea = Psrc_list->Phead->Pnext;

      Area_List_clear(Pdst_list);

      if ((x_ofs) || (y_ofs))
      {
         Area *Pprev_area = Pdst_list->Phead;

         while (Parea != Psrc_list->Ptail)
         {
            //      Area *p, *Pnew_area;
            Area *Pnew_area;

            if (Pdst_list->next_free == Pdst_list->total_areas)
               area_fatal_error("Area_List_copy", "Out of areas!");

            Pnew_area = Pdst_list->Phead + Pdst_list->next_free;
            Pdst_list->next_free++;

            Pnew_area->Pprev = Pprev_area;
            Pprev_area->Pnext = Pnew_area;

            Pnew_area->x1 = Parea->x1 + x_ofs;
            Pnew_area->y1 = Parea->y1 + y_ofs;
            Pnew_area->x2 = Parea->x2 + x_ofs;
            Pnew_area->y2 = Parea->y2 + y_ofs;

            Pprev_area = Pnew_area;

            Parea = Parea->Pnext;
         }

         Pprev_area->Pnext = Pdst_list->Ptail;
      }
      else
      {
   #if 0
         while (Parea != Psrc_list->Ptail)
         {
            insert_area_after(Pdst_list, Pdst_list->Phead,
               Parea->x1,
               Parea->y1,
               Parea->x2,
               Parea->y2);

            Parea = Parea->Pnext;
         }
   #endif

         Area *Pprev_area = Pdst_list->Phead;

         while (Parea != Psrc_list->Ptail)
         {
            //      Area *p, *Pnew_area;
            Area *Pnew_area;

            if (Pdst_list->next_free == Pdst_list->total_areas)
               area_fatal_error("Area_List_copy", "Out of areas!");

            Pnew_area = Pdst_list->Phead + Pdst_list->next_free;
            Pdst_list->next_free++;

            Pnew_area->Pprev = Pprev_area;
            Pprev_area->Pnext = Pnew_area;

            Pnew_area->x1 = Parea->x1;
            Pnew_area->y1 = Parea->y1;
            Pnew_area->x2 = Parea->x2;
            Pnew_area->y2 = Parea->y2;

            Pprev_area = Pnew_area;

            Parea = Parea->Pnext;
         }

         Pprev_area->Pnext = Pdst_list->Ptail;
      }
   }

   void Area_List_clear(Area_List *Plist)
   {
      Plist->Phead->Pnext = Plist->Ptail;
      Plist->Ptail->Pprev = Plist->Phead;
      Plist->Pfree = NULL;
      Plist->next_free = 2;
   }

   void Area_List_set(Area_List *Plist, int x1, int y1, int x2, int y2)
   {
      Plist->Pfree = NULL;

      Plist->Phead[2].x1 = x1;
      Plist->Phead[2].y1 = y1;
      Plist->Phead[2].x2 = x2;
      Plist->Phead[2].y2 = y2;

      Plist->Phead[2].Pprev = Plist->Phead;
      Plist->Phead->Pnext = Plist->Phead + 2;

      Plist->Phead[2].Pnext = Plist->Ptail;
      Plist->Ptail->Pprev = Plist->Phead + 2;

      Plist->next_free = 3;
   }

   void Area_List_remove(Area_List *Plist,
                         int x1, int y1, int x2, int y2)
   {
      int l, h;
      Area *Parea = Plist->Phead->Pnext;

   #ifdef RECT_DEBUG
      if ((x1 > x2) || (y1 > y2))
         area_fatal_error("area_list_remove", "invalid coords: %i %i %i %i", x1, y1, x2, y2);
   #endif

      while (Parea != Plist->Ptail)
      {
         // Not touching
         if ((x2 < Parea->x1) || (x1 > Parea->x2) ||
            (y2 < Parea->y1) || (y1 > Parea->y2))
         {
            Parea = Parea->Pnext;
            continue;
         }

         // Completely covers
         if ((x1 <= Parea->x1) && (x2 >= Parea->x2) &&
            (y1 <= Parea->y1) && (y2 >= Parea->y2))
         {
            if ((x1 == Parea->x1) && (x2 == Parea->x2) &&
               (y1 == Parea->y1) && (y2 == Parea->y2))
            {
               delete_area(Plist, Parea);
               return;
            }

            Parea = delete_area(Plist, Parea);

            continue;
         }

         // top
         if (y1 > Parea->y1)
         {
            insert_area_before(Plist, Parea,
               Parea->x1, Parea->y1,
               Parea->x2, y1 - 1);
         }

         // bottom
         if (y2 < Parea->y2)
         {
            insert_area_before(Plist, Parea,
               Parea->x1, y2 + 1,
               Parea->x2, Parea->y2);
         }

         l = math::maximum(y1, Parea->y1);
         h = math::minimum(y2, Parea->y2);

         // left middle
         if (x1 > Parea->x1)
         {
            insert_area_before(Plist, Parea,
               Parea->x1, l,
               x1 - 1, h);
         }

         // right middle
         if (x2 < Parea->x2)
         {
            insert_area_before(Plist, Parea,
               x2 + 1, l,
               Parea->x2, h);
         }

         // early out - we know there's nothing else to remove, as areas can
         // never overlap
         if ((x1 >= Parea->x1) && (x2 <= Parea->x2) &&
            (y1 >= Parea->y1) && (y2 <= Parea->y2))
         {
            delete_area(Plist, Parea);
            return;
         }

         Parea = delete_area(Plist, Parea);
      }
   }

   void Area_List_insert(Area_List *Plist,
                         int x1, int y1, int x2, int y2,
                         bool combine)
   {
      Area *Parea = Plist->Phead->Pnext;

   #ifdef RECT_DEBUG
      if ((x1 > x2) || (y1 > y2))
         area_fatal_error("Area_List_insert", "invalid coords: %i %i %i %i", x1, y1, x2, y2);
   #endif

      while (Parea != Plist->Ptail)
      {
         // totally covers
         if ((x1 <= Parea->x1) && (x2 >= Parea->x2) &&
            (y1 <= Parea->y1) && (y2 >= Parea->y2))
         {
            Parea = delete_area(Plist, Parea);
            continue;
         }

         // intersects
         if ((x2 >= Parea->x1) && (x1 <= Parea->x2) &&
            (y2 >= Parea->y1) && (y1 <= Parea->y2))
         {
            int ax1, ay1, ax2, ay2;

            ax1 = Parea->x1;
            ay1 = Parea->y1;
            ax2 = Parea->x2;
            ay2 = Parea->y2;

            if (x1 < ax1)
               Area_List_insert(Plist, x1, math::maximum(y1, ay1), ax1 - 1, math::minimum(y2, ay2), combine);

            if (x2 > ax2)
               Area_List_insert(Plist, ax2 + 1, math::maximum(y1, ay1), x2, math::minimum(y2, ay2), combine);

            if (y1 < ay1)
               Area_List_insert(Plist, x1, y1, x2, ay1 - 1, combine);

            if (y2 > ay2)
               Area_List_insert(Plist, x1, ay2 + 1, x2, y2, combine);

            return;
         }

         if (combine)
         {
            if ((x1 == Parea->x1) && (x2 == Parea->x2))
            {
               if ((y2 == Parea->y1 - 1) || (y1 == Parea->y2 + 1))
               {
                  delete_area(Plist, Parea);
                  Area_List_insert(Plist, x1, math::minimum(y1, Parea->y1), x2, math::maximum(y2, Parea->y2), CRNLIB_TRUE);
                  return;
               }
            }
            else if ((y1 == Parea->y1) && (y2 == Parea->y2))
            {
               if ((x2 == Parea->x1 - 1) || (x1 == Parea->x2 + 1))
               {
                  delete_area(Plist, Parea);
                  Area_List_insert(Plist, math::minimum(x1, Parea->x1), y1, math::maximum(x2, Parea->x2), y2, CRNLIB_TRUE);
                  return;
               }
            }
         }

         Parea = Parea->Pnext;
      }

      insert_area_before(Plist, Parea, x1, y1, x2, y2);
   }

   void Area_List_intersect_area(Area_List *Plist,
                                 int x1, int y1, int x2, int y2)
   {
      Area *Parea = Plist->Phead->Pnext;

      while (Parea != Plist->Ptail)
      {
         // doesn't cover
         if ((x2 < Parea->x1) || (x1 > Parea->x2) ||
            (y2 < Parea->y1) || (y1 > Parea->y2))
         {
            Parea = delete_area(Plist, Parea);
            continue;
         }

         // totally covers
         if ((x1 <= Parea->x1) && (x2 >= Parea->x2) &&
            (y1 <= Parea->y1) && (y2 >= Parea->y2))
         {
            Parea = Parea->Pnext;
            continue;
         }

         // Oct 21- should insert after, because deleted area will access the NEXT area!
         //    insert_area_after(Plist, Parea,
         //                      math::maximum(x1, Parea->x1),
         //                      math::maximum(y1, Parea->y1),
         //                      math::minimum(x2, Parea->x2),
         //                      math::minimum(y2, Parea->y2));

         insert_area_before(Plist, Parea,
            math::maximum(x1, Parea->x1),
            math::maximum(y1, Parea->y1),
            math::minimum(x2, Parea->x2),
            math::minimum(y2, Parea->y2));

         Parea = delete_area(Plist, Parea);
      }
   }

   #if 0
   void Area_List_intersect_Area_List(
                                      Area_List *Pouter_list,
                                      Area_List *Pinner_list,
                                      Area_List *Pdst_list)
   {
      Area *Parea1 = Pouter_list->Phead->Pnext;

      while (Parea1 != Pouter_list->Ptail)
      {
         Area *Parea2 = Pinner_list->Phead->Pnext;
         int x1, y1, x2, y2;

         x1 = Parea1->x1; x2 = Parea1->x2;
         y1 = Parea1->y1; y2 = Parea1->y2;

         while (Parea2 != Pinner_list->Ptail)
         {
            if ((x1 <= Parea2->x2) && (x2 >= Parea2->x1) &&
               (y1 <= Parea2->y2) && (y2 >= Parea2->y1))
            {
               insert_area_after(Pdst_list, Pdst_list->Phead,
                  math::maximum(x1, Parea2->x1),
                  math::maximum(y1, Parea2->y1),
                  math::minimum(x2, Parea2->x2),
                  math::minimum(y2, Parea2->y2));
            }

            Parea2 = Parea2->Pnext;
         }

         Parea1 = Parea1->Pnext;
      }
   }
   #endif

   #if 1
   void Area_List_intersect_Area_List(Area_List *Pouter_list,
                                      Area_List *Pinner_list,
                                      Area_List *Pdst_list)
   {
      Area *Parea1 = Pouter_list->Phead->Pnext;

      while (Parea1 != Pouter_list->Ptail)
      {
         Area *Parea2 = Pinner_list->Phead->Pnext;
         int x1, y1, x2, y2;

         x1 = Parea1->x1; x2 = Parea1->x2;
         y1 = Parea1->y1; y2 = Parea1->y2;

         while (Parea2 != Pinner_list->Ptail)
         {
            if ((x1 <= Parea2->x2) && (x2 >= Parea2->x1) &&
               (y1 <= Parea2->y2) && (y2 >= Parea2->y1))
            {
               int nx1, ny1, nx2, ny2;

               nx1 = math::maximum(x1, Parea2->x1);
               ny1 = math::maximum(y1, Parea2->y1);
               nx2 = math::minimum(x2, Parea2->x2);
               ny2 = math::minimum(y2, Parea2->y2);

               if (Pdst_list->Phead->Pnext == Pdst_list->Ptail)
               {
                  insert_area_after(Pdst_list, Pdst_list->Phead,
                     nx1, ny1, nx2, ny2);
               }
               else
               {
                  Area_Ptr Ptemp = Pdst_list->Phead->Pnext;
                  if ((Ptemp->x1 == nx1) && (Ptemp->x2 == nx2))
                  {
                     if (Ptemp->y1 == (ny2+1))
                     {
                        Ptemp->y1 = ny1;
                        goto next;
                     }
                     else if (Ptemp->y2 == (ny1-1))
                     {
                        Ptemp->y2 = ny2;
                        goto next;
                     }
                  }
                  else if ((Ptemp->y1 == ny1) && (Ptemp->y2 == ny2))
                  {
                     if (Ptemp->x1 == (nx2+1))
                     {
                        Ptemp->x1 = nx1;
                        goto next;
                     }
                     else if (Ptemp->x2 == (nx1-1))
                     {
                        Ptemp->x2 = nx2;
                        goto next;
                     }
                  }

                  insert_area_after(Pdst_list, Pdst_list->Phead,
                     nx1, ny1, nx2, ny2);
               }
            }

   next:

            Parea2 = Parea2->Pnext;
         }

         Parea1 = Parea1->Pnext;
      }
   }
   #endif

   Area_List_Ptr Area_List_create_optimal(Area_List_Ptr Plist)
   {
      Area_Ptr Parea = Plist->Phead->Pnext, Parea_after;
      int num = 2;
      Area_List_Ptr Pnew_list;

      while (Parea != Plist->Ptail)
      {
         num++;
         Parea = Parea->Pnext;
      }

      Pnew_list = Area_List_init(num);

      Parea = Plist->Phead->Pnext;

      Parea_after = Pnew_list->Phead;

      while (Parea != Plist->Ptail)
      {
         Parea_after = insert_area_after(Pnew_list, Parea_after,
            Parea->x1, Parea->y1,
            Parea->x2, Parea->y2);

         Parea = Parea->Pnext;
      }

      return (Pnew_list);
   }

} // namespace crnlib
