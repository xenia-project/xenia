// File: crn_intersect.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_ray.h"

namespace crnlib
{
   namespace intersection
   {
      enum result 
      {
         cBackfacing = -1,
         cFailure = 0,
         cSuccess,
         cParallel,
         cInside,
      };
      
      // Returns cInside, cSuccess, or cFailure.
      // Algorithm: Graphics Gems 1
      template<typename vector_type, typename scalar_type, typename ray_type, typename aabb_type>
      result ray_aabb(vector_type& coord, scalar_type& t, const ray_type& ray, const aabb_type& box)
      {
         enum 
         { 
            cNumDim = vector_type::num_elements, 
            cRight = 0, 
            cLeft = 1, 
            cMiddle = 2 
         };

         bool inside = true;
         int quadrant[cNumDim];
         scalar_type candidate_plane[cNumDim];

         for (int i = 0; i < cNumDim; i++)
         {
            if (ray.get_origin()[i] < box[0][i])
            {
               quadrant[i] = cLeft;
               candidate_plane[i] = box[0][i];
               inside = false;
            }
            else if (ray.get_origin()[i] > box[1][i])
            {
               quadrant[i] = cRight;
               candidate_plane[i] = box[1][i];
               inside = false;
            }
            else
            {
               quadrant[i] = cMiddle;
            }
         }

         if (inside)
         {
            coord = ray.get_origin();
            t = 0.0f;
            return cInside;
         }

         scalar_type max_t[cNumDim];
         for (int i = 0; i < cNumDim; i++)
         {
            if ((quadrant[i] != cMiddle) && (ray.get_direction()[i] != 0.0f))
               max_t[i] = (candidate_plane[i] - ray.get_origin()[i]) / ray.get_direction()[i];
            else
               max_t[i] = -1.0f;
         }         

         int which_plane = 0;
         for (int i = 1; i < cNumDim; i++)
            if (max_t[which_plane] < max_t[i])
               which_plane = i;

         if (max_t[which_plane] < 0.0f)
            return cFailure;               

         for (int i = 0; i < cNumDim; i++)
         {
            if (i != which_plane)
            {
               coord[i] = ray.get_origin()[i] + max_t[which_plane] * ray.get_direction()[i];

               if ( (coord[i] < box[0][i]) || (coord[i] > box[1][i]) )
                  return cFailure;
            }
            else
            {
               coord[i] = candidate_plane[i];
            }

            CRNLIB_ASSERT(coord[i] >= box[0][i] && coord[i] <= box[1][i]); 
         }               

         t = max_t[which_plane];
         return cSuccess;
      }
      
      template<typename vector_type, typename scalar_type, typename ray_type, typename aabb_type>
      result ray_aabb(bool& started_within, vector_type& coord, scalar_type& t, const ray_type& ray, const aabb_type& box)
      {
         if (!box.contains(ray.get_origin()))
         {
            started_within = false;
            return ray_aabb(coord, t, ray, box);
         }
         
         started_within = true;
         
         float diag_dist = box.diagonal_length() * 1.5f;
         ray_type outside_ray(ray.eval(diag_dist), -ray.get_direction());
         
         result res(ray_aabb(coord, t, outside_ray, box));
         if (res != cSuccess)
            return res;
         
         t = math::maximum(0.0f, diag_dist - t);
         return cSuccess;
      }
   }
}
