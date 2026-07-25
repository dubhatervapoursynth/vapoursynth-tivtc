/*
**                    TIVTC for AviSynth 2.6 interface
**
**   TIVTC includes a field matching filter (TFM) and a decimation
**   filter (TDecimate) which can be used together to achieve an
**   IVTC or for other uses. TIVTC supports 8-16 bit planar YUV
**   (4:4:4, 4:2:2 and 4:2:0).
**
**   Copyright (C) 2004-2008 Kevin Stone, additional work (C) 2020 pinterf
**
**   This program is free software; you can redistribute it and/or modify
**   it under the terms of the GNU General Public License as published by
**   the Free Software Foundation; either version 2 of the License, or
**   (at your option) any later version.
**
**   This program is distributed in the hope that it will be useful,
**   but WITHOUT ANY WARRANTY; without even the implied warranty of
**   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**   GNU General Public License for more details.
**
**   You should have received a copy of the GNU General Public License
**   along with this program; if not, write to the Free Software
**   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "TDecimate.h"
#include "TDecimateASM.h"
#include "TCommonASM.h"
#include <assert.h>

static void blend_uint8_c(uint8_t* dstp, const uint8_t* srcp1,
  const uint8_t* srcp2, int width, int height, ptrdiff_t dst_pitch,
  ptrdiff_t src1_pitch, ptrdiff_t src2_pitch, int weight_i)
{
  // weight_i is 16 bit scaled
  assert(weight_i != 0 && weight_i != 65536);

  const int invweight_i = 65536 - weight_i;

  for (int y = 0; y < height; ++y)
  {
    for (int x = 0; x < width; ++x)
    {
      dstp[x] = (weight_i * srcp1[x] + invweight_i * srcp2[x] + 32768) >> 16;
    }
    srcp1 += src1_pitch;
    srcp2 += src2_pitch;
    dstp += dst_pitch;
  }
}

static void blend_uint16_c(uint8_t* dstp, const uint8_t* srcp1,
  const uint8_t* srcp2, int width, int height, ptrdiff_t dst_pitch,
  ptrdiff_t src1_pitch, ptrdiff_t src2_pitch, int weight_i, int bits_per_pixel)
{
  // weight_i is 15 bit scaled
  // min and max cases handled earlier
  assert(weight_i != 0 && weight_i != 32768);

  const int max_pixel_value = (1 << bits_per_pixel) - 1;
  for (int y = 0; y < height; ++y)
  {
    for (int x = 0; x < width; ++x)
    {
      const int src1 = reinterpret_cast<const uint16_t*>(srcp1)[x];
      const int src2 = reinterpret_cast<const uint16_t*>(srcp2)[x];
      const int result = src2 + (((src1 - src2) * weight_i + 16384) >> 15);
      reinterpret_cast<uint16_t*>(dstp)[x] = std::max(std::min(result, max_pixel_value), 0);
      //  (reinterpret_cast<const uint16_t*>(srcp1)[x] * weight_i + reinterpret_cast<const uint16_t*>(srcp2)[x] * invweight_i + 16384) >> 15;
    }
    srcp1 += src1_pitch;
    srcp2 += src2_pitch;
    dstp += dst_pitch;
  }
}




// handles 50% special case as well
// hbd ready
void dispatch_blend(uint8_t* dstp, const uint8_t* srcp1, const uint8_t* srcp2, int width, int height,
  ptrdiff_t dst_pitch, ptrdiff_t src1_pitch, ptrdiff_t src2_pitch, int weight_i, int bits_per_pixel)
{

  // weight_i 0 and max --> copy is already handled!
  // weight_i is of 15 bit scale

  // special 50% case
  if (weight_i == 32768 / 2) {
    if (bits_per_pixel == 8)
      blend_5050_c<uint8_t>(dstp, srcp1, srcp2, width, height, dst_pitch, src1_pitch, src2_pitch);
    else
      blend_5050_c<uint16_t>(dstp, srcp1, srcp2, width, height, dst_pitch, src1_pitch, src2_pitch);
    return;
  }

  // arbitrary blend
  if (bits_per_pixel == 8) {
    // using 16 bit scaled values inside instead of 15 bit scaled
    blend_uint8_c(dstp, srcp1, srcp2, width, height, dst_pitch, src1_pitch, src2_pitch, weight_i * 2);
    return;
  }

  // 10-16 bits
  blend_uint16_c(dstp, srcp1, srcp2, width, height, dst_pitch, src1_pitch, src2_pitch, weight_i, bits_per_pixel);
}






// only 411 uses
template<int blkSizeY>
void calcSAD_C_2xN(const uint8_t* ptr1, const uint8_t* ptr2,
  ptrdiff_t pitch1, ptrdiff_t pitch2, int& sad)
{
  int tmpsum = 0;
  for (int i = 0; i < blkSizeY; i++) {
    tmpsum += abs(ptr1[0] - ptr2[0]);
    tmpsum += abs(ptr1[1] - ptr2[1]);
    ptr1 += pitch1;
    ptr2 += pitch2;
  }

  sad = tmpsum;
}

template<int blkSizeY>
void calcSSD_C_2xN(const uint8_t* ptr1, const uint8_t* ptr2,
  ptrdiff_t pitch1, ptrdiff_t pitch2, int& sad)
{
  int tmpsum = 0;
  for (int i = 0; i < blkSizeY; i++) {
    const int tmp0 = ptr1[0] - ptr2[0];
    const int tmp1 = ptr1[1] - ptr2[1];
    tmpsum += tmp0 * tmp0 + tmp1 * tmp1;
    ptr1 += pitch1;
    ptr2 += pitch2;
  }

  sad = tmpsum;
}




// new








// instantiate





// mod 8 always, unaligned




//-------- helpers

// true SAD false SSD




// true: SAD, false: SSD




// true: SAD, false: SSD
template<typename pixel_t, bool SAD, int inc>
void calcDiff_SADorSSD_Generic_c(const pixel_t* prvp, const pixel_t* curp,
  ptrdiff_t prv_pitch, ptrdiff_t cur_pitch, int width, int height, int plane, int xblocks4, uint64_t* diff,
  [[maybe_unused]] bool chroma, int xshiftS, int yshiftS, int xhalfS, int yhalfS, int nt,
  const VSVideoInfo *vi)
{

  int temp1, temp2, u;

  // 16 bits SSD requires int64 intermediate
  typedef typename std::conditional<sizeof(pixel_t) == 1 && !SAD, int, int64_t> ::type safeint_t;

  safeint_t difft; // int or 64 bits
  int64_t diffs; // per-block SSD sum can exceed 32 bits for large blocks (e.g. ssd=1, 512x512); use 64-bit
  int box1, box2;
  int yshift, yhalf, xshift, xhalf;
  int heighta, widtha;
  const pixel_t* prvpT, * curpT;

  const int bits_per_pixel = vi->format.bitsPerSample;
  const int shift_count = SAD ? (bits_per_pixel - 8) : 2 * (bits_per_pixel - 8);

  {
    const int ysubsampling = plane == 0 ? 0 : vi->format.subSamplingH;
    const int xsubsampling = plane == 0 ? 0 : vi->format.subSamplingW;
    yshift = yshiftS - ysubsampling;
    yhalf = yhalfS >> ysubsampling;
    xshift = xshiftS - xsubsampling;
    xhalf = xhalfS >> xsubsampling;
  }

  heighta = (height >> (yshift - 1)) << (yshift - 1);
  widtha = (width >> (xshift - 1)) << (xshift - 1);
  // whole blocks
  for (int y = 0; y < heighta; y += yhalf)
  {
    temp1 = (y >> yshift) * xblocks4;
    temp2 = ((y + yhalf) >> yshift) * xblocks4;
    for (int x = 0; x < widtha; x += xhalf)
    {
      prvpT = prvp;
      curpT = curp;
      for (diffs = 0, u = 0; u < yhalf; ++u)
      {
        for (int v = 0; v < xhalf; v += inc)
        {
          if constexpr (SAD) {
            difft = abs(prvpT[x + v] - curpT[x + v]);
          }
          else {
            difft = prvpT[x + v] - curpT[x + v];
            difft *= difft;
          }
          if constexpr (sizeof(pixel_t) == 2) difft >>= shift_count; // back to 8 bit range

          if (difft > nt) diffs += static_cast<int>(difft);
        }
        prvpT += prv_pitch;
        curpT += cur_pitch;
      }
      if (diffs > nt)
      {
        box1 = (x >> xshift) << 2;
        box2 = ((x + xhalf) >> xshift) << 2;
        diff[temp1 + box1 + 0] += diffs;
        diff[temp1 + box2 + 1] += diffs;
        diff[temp2 + box1 + 2] += diffs;
        diff[temp2 + box2 + 3] += diffs;
      }
    }
    // rest non - whole block on the right
    for (int x = widtha; x < width; x += inc)
    {
      prvpT = prvp;
      curpT = curp;
      for (diffs = 0, u = 0; u < yhalf; ++u)
      {
        if constexpr (SAD) {
          difft = abs(prvpT[x] - curpT[x]);
        }
        else {
          difft = prvpT[x] - curpT[x];
          difft *= difft;
        }
        if constexpr (sizeof(pixel_t) == 2) difft >>= shift_count; // back to 8 bit range
        if (difft > nt) diffs += static_cast<int>(difft);
        prvpT += prv_pitch;
        curpT += cur_pitch;
      }
      if (diffs > nt)
      {
        box1 = (x >> xshift) << 2;
        box2 = ((x + xhalf) >> xshift) << 2;
        diff[temp1 + box1 + 0] += diffs;
        diff[temp1 + box2 + 1] += diffs;
        diff[temp2 + box1 + 2] += diffs;
        diff[temp2 + box2 + 3] += diffs;
      }
    }
    prvp += prv_pitch * yhalf;
    curp += cur_pitch * yhalf;
  }
  // rest non-whole block at the bottom
  for (int y = heighta; y < height; ++y)
  {
    temp1 = (y >> yshift) * xblocks4;
    temp2 = ((y + yhalf) >> yshift) * xblocks4;
    for (int x = 0; x < width; x += inc)
    {
      if constexpr (SAD) {
        difft = abs(prvp[x] - curp[x]);
      }
      else {
        difft = prvp[x] - curp[x];
        difft *= difft;
      }
      if constexpr (sizeof(pixel_t) == 2) difft >>= shift_count; // back to 8 bit range
      if (difft > nt)
      {
        box1 = (x >> xshift) << 2;
        box2 = ((x + xhalf) >> xshift) << 2;
        diff[temp1 + box1 + 0] += difft;
        diff[temp1 + box2 + 1] += difft;
        diff[temp2 + box1 + 2] += difft;
        diff[temp2 + box2 + 3] += difft;
      }
    }
    prvp += prv_pitch;
    curp += cur_pitch;
  }
}

// instantiate
template void calcDiff_SADorSSD_Generic_c<uint8_t, false, 1>(const uint8_t* prvp, const uint8_t* curp,
  ptrdiff_t prv_pitch, ptrdiff_t cur_pitch, int width, int height, int plane, int xblocks4, uint64_t* diff, bool chroma, int xshiftS, int yshiftS, int xhalfS, int yhalfS, int nt, const VSVideoInfo *vi);
template void calcDiff_SADorSSD_Generic_c<uint8_t, false, 2>(const uint8_t* prvp, const uint8_t* curp,
  ptrdiff_t prv_pitch, ptrdiff_t cur_pitch, int width, int height, int plane, int xblocks4, uint64_t* diff, bool chroma, int xshiftS, int yshiftS, int xhalfS, int yhalfS, int nt, const VSVideoInfo *vi);
template void calcDiff_SADorSSD_Generic_c<uint8_t, true, 1>(const uint8_t* prvp, const uint8_t* curp,
  ptrdiff_t prv_pitch, ptrdiff_t cur_pitch, int width, int height, int plane, int xblocks4, uint64_t* diff, bool chroma, int xshiftS, int yshiftS, int xhalfS, int yhalfS, int nt, const VSVideoInfo *vi);
template void calcDiff_SADorSSD_Generic_c<uint8_t, true, 2>(const uint8_t* prvp, const uint8_t* curp,
  ptrdiff_t prv_pitch, ptrdiff_t cur_pitch, int width, int height, int plane, int xblocks4, uint64_t* diff, bool chroma, int xshiftS, int yshiftS, int xhalfS, int yhalfS, int nt, const VSVideoInfo *vi);

template void calcDiff_SADorSSD_Generic_c<uint16_t, false, 1>(const uint16_t* prvp, const uint16_t* curp,
  ptrdiff_t prv_pitch, ptrdiff_t cur_pitch, int width, int height, int plane, int xblocks4, uint64_t* diff, bool chroma, int xshiftS, int yshiftS, int xhalfS, int yhalfS, int nt, const VSVideoInfo *vi);
template void calcDiff_SADorSSD_Generic_c<uint16_t, true, 1>(const uint16_t* prvp, const uint16_t* curp,
  ptrdiff_t prv_pitch, ptrdiff_t cur_pitch, int width, int height, int plane, int xblocks4, uint64_t* diff, bool chroma, int xshiftS, int yshiftS, int xhalfS, int yhalfS, int nt, const VSVideoInfo *vi);


