/*
**   Helper methods for TIVTC and TDeint
**
**
**   Copyright (C) 2004-2007 Kevin Stone, additional work (C) 2020 pinterf
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

#include "TCommonASM.h"
#include <algorithm>




template<typename pixel_t>
void buildABSDiffMask_c(const uint8_t* prvp, const uint8_t* nxtp,
  uint8_t* dstp, int prv_pitch, int nxt_pitch, int dst_pitch, int width, int height)
{
  if (width <= 0)
    return;

 {
    for (int y = 0; y < height; ++y)
    {
      for (int x = 0; x < width; x++)
      {
        reinterpret_cast<pixel_t *>(dstp)[x] = abs(reinterpret_cast<const pixel_t*>(prvp)[x] - reinterpret_cast<const pixel_t*>(nxtp)[x]);
      }
      prvp += prv_pitch;
      nxtp += nxt_pitch;
      dstp += dst_pitch;
    }
  }
}

template<typename pixel_t>
void do_buildABSDiffMask(const uint8_t* prvp, const uint8_t* nxtp, uint8_t* tbuffer,
  int prv_pitch, int nxt_pitch, int tpitch, int width, int height)
{
  buildABSDiffMask_c<pixel_t>(prvp, nxtp, tbuffer, prv_pitch, nxt_pitch, tpitch, width, height);
}
// instantiate
template void do_buildABSDiffMask<uint8_t>(const uint8_t* prvp, const uint8_t* nxtp, uint8_t* tbuffer,
  int prv_pitch, int nxt_pitch, int tpitch, int width, int height);
template void do_buildABSDiffMask<uint16_t>(const uint8_t* prvp, const uint8_t* nxtp, uint8_t* tbuffer,
  int prv_pitch, int nxt_pitch, int tpitch, int width, int height);


template<typename pixel_t>
void do_buildABSDiffMask2(const uint8_t* prvp, const uint8_t* nxtp, uint8_t* dstp,
  int prv_pitch, int nxt_pitch, int dst_pitch, int width, int height, int bits_per_pixel)
{
  buildABSDiffMask2_c<pixel_t>(prvp, nxtp, dstp, prv_pitch, nxt_pitch, dst_pitch, width, height, bits_per_pixel);
}
// instantiate
template void do_buildABSDiffMask2<uint8_t>(const uint8_t* prvp, const uint8_t* nxtp, uint8_t* dstp,
  int prv_pitch, int nxt_pitch, int dst_pitch, int width, int height, int bits_per_pixel);
template void do_buildABSDiffMask2<uint16_t>(const uint8_t* prvp, const uint8_t* nxtp, uint8_t* dstp,
  int prv_pitch, int nxt_pitch, int dst_pitch, int width, int height, int bits_per_pixel);

// Finally this is common for TFM and TDeint, planar (luma, luma+chroma))
// This C code replaces some thousand line of copy pasted original inline asm lines
// (plus handles 10+bits)

// distance of neighboring pixels (always 1 for planar)
template<typename pixel_t, int bits_per_pixel, int DIST>
static AVS_FORCEINLINE void AnalyzeOnePixel(uint8_t* dstp,
  const pixel_t* dppp, const pixel_t* dpp,
  const pixel_t* dp,
  const pixel_t* dpn, const pixel_t* dpnn,
  int& x, int& y, int& Width, int lastyy)
{
  constexpr int Const3 = 3 << (bits_per_pixel - 8);
  constexpr int Const19 = 19 << (bits_per_pixel - 8);

  if (dp[x] <= Const3)
    return;

  if (dp[x - DIST] <= Const3 && dp[x + DIST] <= Const3 &&
    dpp[x - DIST] <= Const3 && dpp[x] <= Const3 && dpp[x + DIST] <= Const3 &&
    dpn[x - DIST] <= Const3 && dpn[x] <= Const3 && dpn[x + DIST] <= Const3)
    return;

  dstp[x]++;

  if (dp[x] <= Const19)
    return;

  int edi = 0;
  int lower = 0;
  int upper = 0;

  if (dpp[x - DIST] > Const19) edi++;
  if (dpp[x] > Const19) edi++;
  if (dpp[x + DIST] > Const19) edi++;

  if (edi != 0) upper = 1;

  if (dp[x - DIST] > Const19) edi++;
  if (dp[x + DIST] > Const19) edi++;

  int esi = edi;

  if (dpn[x - DIST] > Const19) edi++;
  if (dpn[x] > Const19) edi++;
  if (dpn[x + DIST] > Const19) edi++;

  if (edi <= 2)
    return;

  int count = edi;
  if (count != esi) {
    lower = 1;
    if (upper != 0) {
      dstp[x] += 2;
      return;
    }
  }

  int lower2 = 0;
  int upper2 = 0;

  int startx, stopx;

  startx = x < 4 * DIST ? 0 : x - 4 * DIST;
  stopx = x + 4 * DIST + DIST > Width ? Width : x + 4 * DIST + DIST;

  if (y != 2) {
    for (esi = startx; esi < stopx; esi += DIST) {
      if (dppp[esi] > Const19) {
        upper2 = 1;
        break;
      }
    }
  }

  for (esi = startx; esi < stopx; esi += DIST)
  {
    if (dpp[esi] > Const19)
      upper = 1;
    if (dpn[esi] > Const19)
      lower = 1;
    if (upper != 0 && lower != 0)
      break;
  }

  if (y <= lastyy) {
    for (esi = startx; esi < stopx; esi += DIST)
    {
      if (dpnn[esi] > Const19) {
        lower2 = 1;
        break;
      }
    }
  }

  if (upper == 0) {
    if (lower == 0 || lower2 == 0) {
      if (count > 4)
        dstp[x] += 4;
    }
    else {
      dstp[x] += 2;
    }
  }
  else {
    if (lower != 0 || upper2 != 0) {
      dstp[x] += 2;
    }
    else {
      if (count > 4)
        dstp[x] += 4;
    }
  }
}

// Common TDeint and TFM version
template<typename pixel_t, int bits_per_pixel>
void AnalyzeDiffMask_Planar(uint8_t* dstp, int dst_pitch, uint8_t* tbuffer8, int tpitch, int Width, int Height)
{
  tpitch /= sizeof(pixel_t);
  const pixel_t* tbuffer = reinterpret_cast<const pixel_t*>(tbuffer8);
  const pixel_t* dppp = tbuffer - tpitch;
  const pixel_t* dpp = tbuffer;
  const pixel_t* dp = tbuffer + tpitch;
  const pixel_t* dpn = tbuffer + tpitch * 2;
  const pixel_t* dpnn = tbuffer + tpitch * 3;

  // buildABSDiffMask() only fills (Height >> 1) rows of tbuffer. For an even plane height that
  // is exactly what the walk below needs, but for an odd height (4:2:0 chroma of a clip whose
  // height is 2 mod 4) deriving the limits from Height instead reads one/two rows of
  // uninitialised scratch. Derive them from the rows that actually exist.
  const int rows = Height >> 1;
  const int lasty = 2 * rows - 4;  // largest y whose dpn row exists
  const int lastyy = 2 * rows - 6; // largest y whose dpnn row exists

  for (int y = 2; y <= lasty; y += 2) {
    for (int x = 1; x < Width - 1; x++) {
      AnalyzeOnePixel<pixel_t, bits_per_pixel, 1>(dstp, dppp, dpp, dp, dpn, dpnn, x, y, Width, lastyy);
    }
    dppp += tpitch;
    dpp += tpitch;
    dp += tpitch;
    dpn += tpitch;
    dpnn += tpitch;
    dstp += dst_pitch;
  }
}
// instantiate
template void AnalyzeDiffMask_Planar<uint8_t,8>(uint8_t* dstp, int dst_pitch, uint8_t* tbuffer8, int tpitch, int Width, int Height);
template void AnalyzeDiffMask_Planar<uint16_t, 10>(uint8_t* dstp, int dst_pitch, uint8_t* tbuffer8, int tpitch, int Width, int Height);
template void AnalyzeDiffMask_Planar<uint16_t, 12>(uint8_t* dstp, int dst_pitch, uint8_t* tbuffer8, int tpitch, int Width, int Height);
template void AnalyzeDiffMask_Planar<uint16_t, 14>(uint8_t* dstp, int dst_pitch, uint8_t* tbuffer8, int tpitch, int Width, int Height);
template void AnalyzeDiffMask_Planar<uint16_t, 16>(uint8_t* dstp, int dst_pitch, uint8_t* tbuffer8, int tpitch, int Width, int Height);

// HBD ready
template<typename pixel_t>
void buildABSDiffMask2_c(const uint8_t* prvp, const uint8_t* nxtp,
  uint8_t* dstp, int prv_pitch, int nxt_pitch, int dst_pitch, int width, int height, int bits_per_pixel)
{
  if (width <= 0)
    return;

  constexpr int inc = 1;
  const int Const19 = 19 << (bits_per_pixel - 8);
  const int Const3 = 3 << (bits_per_pixel - 8);
  for (int y = 0; y < height; ++y)
  {
    for (int x = 0; x < width; x += inc)
    {
      const int diff = abs(reinterpret_cast<const pixel_t *>(prvp)[x] - reinterpret_cast<const pixel_t*>(nxtp)[x]);
      if (diff > Const19) dstp[x] = 3;
      else if (diff > Const3) dstp[x] = 1;
      else dstp[x] = 0;
    }
    prvp += prv_pitch;
    nxtp += nxt_pitch;
    dstp += dst_pitch;
  }
}





template<typename pixel_t>
void check_combing_c(const pixel_t* srcp, uint8_t* cmkp, int width, int height, int src_pitch, int cmk_pitch, int cthresh)
{
  // cthresh is scaled to actual bit depth
  const pixel_t* srcppp = srcp - src_pitch * 2;
  const pixel_t* srcpp = srcp - src_pitch;
  const pixel_t* srcpn = srcp + src_pitch;
  const pixel_t* srcpnn = srcp + src_pitch * 2;

  int increment = 1;

  const int cthresh6 = cthresh * 6;
  // no luma masking
  for (int y = 0; y < height; ++y)
  {
    for (int x = 0; x < width; x += increment)
    {
      const int sFirst = srcp[x] - srcpp[x];
      const int sSecond = srcp[x] - srcpn[x];
      if ((sFirst > cthresh && sSecond > cthresh) || (sFirst < -cthresh && sSecond < -cthresh))
      {
        if (abs(srcppp[x] + (srcp[x] << 2) + srcpnn[x] - (3 * (srcpp[x] + srcpn[x]))) > cthresh6)
          cmkp[x] = 0xFF;
      }
    }
    srcppp += src_pitch;
    srcpp += src_pitch;
    srcp += src_pitch;
    srcpn += src_pitch;
    srcpnn += src_pitch;
    cmkp += cmk_pitch;
  }
}
// instantiate
template void check_combing_c<uint8_t>(const uint8_t* srcp, uint8_t* cmkp, int width, int height, int src_pitch, int cmk_pitch, int cthresh);
template void check_combing_c<uint16_t>(const uint16_t* srcp, uint8_t* cmkp, int width, int height, int src_pitch, int cmk_pitch, int cthresh);

template<typename pixel_t, typename safeint_t>
void check_combing_c_Metric1(const pixel_t* srcp, uint8_t* cmkp, int width, int height, int src_pitch, int cmk_pitch, safeint_t cthreshsq)
{
  // cthresh is scaled to actual bit depth
  const pixel_t* srcpp = srcp - src_pitch;
  const pixel_t* srcpn = srcp + src_pitch;

  for (int y = 0; y < height; ++y)
  {
    for (int x = 0; x < width; ++x)
    {
      if ((safeint_t)(srcp[x] - srcpp[x]) * (srcp[x] - srcpn[x]) > cthreshsq)
        cmkp[x] = 0xFF;
    }
    srcpp += src_pitch;
    srcp += src_pitch;
    srcpn += src_pitch;
    cmkp += cmk_pitch;
  }
}
// instantiate
template void check_combing_c_Metric1<uint8_t, int>(const uint8_t* srcp, uint8_t* cmkp, int width, int height, int src_pitch, int cmk_pitch, int cthreshsq);
template void check_combing_c_Metric1<uint16_t, int64_t>(const uint16_t* srcp, uint8_t* cmkp, int width, int height, int src_pitch, int cmk_pitch, int64_t cthreshsq);













// instantiate for 8x8

void copyFrame(VSFrame *dst, const VSFrame *src, const VSAPI *vsapi)
{
  // bit depth independent
    const VSVideoFormat *format = vsapi->getVideoFrameFormat(src);
  const int np = format->numPlanes;
  for (int b = 0; b < np; ++b)
  {
    const int plane = b;
    vsh::bitblt(vsapi->getWritePtr(dst, plane), vsapi->getStride(dst, plane), vsapi->getReadPtr(src, plane),
      vsapi->getStride(src, plane), vsapi->getFrameWidth(src, plane) * format->bytesPerSample, vsapi->getFrameHeight(src, plane));
  }
}

// fast blend routine for 50:50 case
// instantiate

template<typename pixel_t>
void blend_5050_c(uint8_t* dstp, const uint8_t* srcp1, const uint8_t* srcp2, int width, int height, int dst_pitch, int src1_pitch, int src2_pitch)
{
  for (int y = 0; y < height; ++y)
  {
    for (int x = 0; x < width; ++x)
      reinterpret_cast<pixel_t*>(dstp)[x] = (reinterpret_cast<const pixel_t*>(srcp1)[x] + reinterpret_cast<const pixel_t*>(srcp2)[x] + 1) >> 1;
    srcp1 += src1_pitch;
    srcp2 += src2_pitch;
    dstp += dst_pitch;
  }
}

// instantiate
template void blend_5050_c<uint8_t>(uint8_t* dstp, const uint8_t* srcp1, const uint8_t* srcp2, int width, int height, int dst_pitch, int src1_pitch, int src2_pitch);
template void blend_5050_c<uint16_t>(uint8_t* dstp, const uint8_t* srcp1, const uint8_t* srcp2, int width, int height, int dst_pitch, int src1_pitch, int src2_pitch);

// like HandleChromaCombing in TDeinterlace
// used by isCombedTIVTC as well
// mask only, no hbd needed
template<int planarType>
void do_FillCombedPlanarUpdateCmaskByUV(uint8_t* cmkp, uint8_t* cmkpU, uint8_t* cmkpV, int Width, int Height, ptrdiff_t cmk_pitch, ptrdiff_t cmk_pitchUV)
{
  // 420 only
  uint8_t* cmkpn = cmkp + cmk_pitch;
  uint8_t* cmkpp = cmkp - cmk_pitch;
  uint8_t* cmkpnn = cmkpn + cmk_pitch;

  uint8_t* cmkppU = cmkpU - cmk_pitchUV;
  uint8_t* cmkpnU = cmkpU + cmk_pitchUV;

  uint8_t* cmkppV = cmkpV - cmk_pitchUV;
  uint8_t* cmkpnV = cmkpV + cmk_pitchUV;
  for (int y = 1; y < Height - 1; ++y)
  {
    if (planarType == 420) {
      cmkp += cmk_pitch * 2;
      cmkpn += cmk_pitch * 2;
      cmkpp += cmk_pitch * 2;
      cmkpnn += cmk_pitch * 2;
    }
    else {
      cmkp += cmk_pitch;
    }
    cmkppV += cmk_pitchUV;
    cmkpV += cmk_pitchUV;
    cmkpnV += cmk_pitchUV;
    cmkppU += cmk_pitchUV;
    cmkpU += cmk_pitchUV;
    cmkpnU += cmk_pitchUV;
    for (int x = 1; x < Width - 1; ++x)
    {
      if (
        (cmkpV[x] == 0xFF &&
          (cmkpV[x - 1] == 0xFF || cmkpV[x + 1] == 0xFF ||
            cmkppV[x - 1] == 0xFF || cmkppV[x] == 0xFF || cmkppV[x + 1] == 0xFF ||
            cmkpnV[x - 1] == 0xFF || cmkpnV[x] == 0xFF || cmkpnV[x + 1] == 0xFF
            )
          ) ||
        (cmkpU[x] == 0xFF &&
          (cmkpU[x - 1] == 0xFF || cmkpU[x + 1] == 0xFF ||
            cmkppU[x - 1] == 0xFF || cmkppU[x] == 0xFF || cmkppU[x + 1] == 0xFF ||
            cmkpnU[x - 1] == 0xFF || cmkpnU[x] == 0xFF || cmkpnU[x + 1] == 0xFF
            )
          )
        )
      {
        if (planarType == 420) {
          ((uint16_t*)cmkp)[x] = (uint16_t)0xFFFF;
          ((uint16_t*)cmkpn)[x] = (uint16_t)0xFFFF;
          if (y & 1)
            ((uint16_t*)cmkpp)[x] = (uint16_t)0xFFFF;
          else
            ((uint16_t*)cmkpnn)[x] = (uint16_t)0xFFFF;
        }
        else if (planarType == 422) {
          ((uint16_t*)cmkp)[x] = (uint16_t)0xFFFF;
        }
        else if (planarType == 444) {
          cmkp[x] = 0xFF;
        }
        else if (planarType == 411) {
          ((uint32_t*)cmkp)[x] = (uint32_t)0xFFFFFFFF;
        }
      }
    }
  }
}

template void do_FillCombedPlanarUpdateCmaskByUV<411>(uint8_t* cmkp, uint8_t* cmkpU, uint8_t* cmkpV, int Width, int Height, ptrdiff_t cmk_pitch, ptrdiff_t cmk_pitchUV);
template void do_FillCombedPlanarUpdateCmaskByUV<420>(uint8_t* cmkp, uint8_t* cmkpU, uint8_t* cmkpV, int Width, int Height, ptrdiff_t cmk_pitch, ptrdiff_t cmk_pitchUV);
template void do_FillCombedPlanarUpdateCmaskByUV<422>(uint8_t* cmkp, uint8_t* cmkpU, uint8_t* cmkpV, int Width, int Height, ptrdiff_t cmk_pitch, ptrdiff_t cmk_pitchUV);
template void do_FillCombedPlanarUpdateCmaskByUV<444>(uint8_t* cmkp, uint8_t* cmkpU, uint8_t* cmkpV, int Width, int Height, ptrdiff_t cmk_pitch, ptrdiff_t cmk_pitchUV);
