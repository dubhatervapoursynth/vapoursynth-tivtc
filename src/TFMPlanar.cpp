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

#include <cstring>
#include "TFM.h"
#include "TCommonASM.h"
#include <algorithm>


template<int planarType>
void FillCombedPlanarUpdateCmaskByUV(VSFrame* cmask, const VSAPI *vsapi)
{
  uint8_t* cmkp = vsapi->getWritePtr(cmask, 0);
  uint8_t* cmkpU = vsapi->getWritePtr(cmask, 1);
  uint8_t* cmkpV = vsapi->getWritePtr(cmask, 2);
  const int Width = vsapi->getFrameWidth(cmask, 2); // chroma!
  const int Height = vsapi->getFrameHeight(cmask, 2);
  const ptrdiff_t cmk_pitch = vsapi->getStride(cmask, 0);
  const ptrdiff_t cmk_pitchUV = vsapi->getStride(cmask, 2);
  do_FillCombedPlanarUpdateCmaskByUV<planarType>(cmkp, cmkpU, cmkpV, Width, Height, cmk_pitch, cmk_pitchUV);
}

// templatize
template void FillCombedPlanarUpdateCmaskByUV<411>(VSFrame* cmask, const VSAPI *vsapi);
template void FillCombedPlanarUpdateCmaskByUV<420>(VSFrame* cmask, const VSAPI *vsapi);
template void FillCombedPlanarUpdateCmaskByUV<422>(VSFrame* cmask, const VSAPI *vsapi);
template void FillCombedPlanarUpdateCmaskByUV<444>(VSFrame* cmask, const VSAPI *vsapi);

//FIXME: once to make it common with TDeInterlace::CheckedCombedPlanar
//similar, but cmask is real PVideoFrame there
template<typename pixel_t>
void checkCombedPlanarAnalyze_core(const VSVideoInfo *vi, int cthresh, bool chroma, int metric, const VSFrame *src, VSFrame* cmask, const VSAPI *vsapi)
{
  const int bits_per_pixel = vi->format.bitsPerSample;

  // cthresh: Area combing threshold used for combed frame detection.
  // This essentially controls how "strong" or "visible" combing must be to be detected.
  // Good values are from 6 to 12. If you know your source has a lot of combed frames set 
  // this towards the low end(6 - 7). If you know your source has very few combed frames set 
  // this higher(10 - 12). Going much lower than 5 to 6 or much higher than 12 is not recommended.

  const int scaled_cthresh = cthresh << (bits_per_pixel - 8);

  const int cthresh6 = scaled_cthresh * 6;

  const int np = vi->format.numPlanes;
  const int stop = chroma ? np : 1;

  for (int b = 0; b < stop; ++b)
  {
    const int plane = b;

    const pixel_t* srcp = reinterpret_cast<const pixel_t*>(vsapi->getReadPtr(src, plane));
    const ptrdiff_t src_pitch = vsapi->getStride(src, plane) / sizeof(pixel_t);

    const int Width = vsapi->getFrameWidth(src, plane);
    const int Height = vsapi->getFrameHeight(src, plane);

    const pixel_t* srcpp = srcp - src_pitch;
    const pixel_t* srcppp = srcpp - src_pitch;
    const pixel_t* srcpn = srcp + src_pitch;
    const pixel_t* srcpnn = srcpn + src_pitch;

    uint8_t* cmkp = vsapi->getWritePtr(cmask, b);
    const ptrdiff_t cmk_pitch = vsapi->getStride(cmask, b);

    if (scaled_cthresh < 0) {
      memset(cmkp, 255, Height * cmk_pitch); // mask. Always 8 bits 
      continue;
    }
    memset(cmkp, 0, Height * cmk_pitch);

    if (metric == 0)
    {
      // top 1 
      for (int x = 0; x < Width; ++x)
      {
        const int sFirst = srcp[x] - srcpn[x];
        if (sFirst > scaled_cthresh || sFirst < -scaled_cthresh)
        {
          if (abs(srcpnn[x] + (srcp[x] << 2) + srcpnn[x] - (3 * (srcpn[x] + srcpn[x]))) > cthresh6)
            cmkp[x] = 0xFF;
        }
      }
      srcppp += src_pitch;
      srcpp += src_pitch;
      srcp += src_pitch;
      srcpn += src_pitch;
      srcpnn += src_pitch;
      cmkp += cmk_pitch;
      // top #2
      for (int x = 0; x < Width; ++x)
      {
        const int sFirst = srcp[x] - srcpp[x];
        const int sSecond = srcp[x] - srcpn[x];
        if ((sFirst > scaled_cthresh && sSecond > scaled_cthresh) || (sFirst < -scaled_cthresh && sSecond < -scaled_cthresh))
        {
          if (abs(srcpnn[x] + (srcp[x] << 2) + srcpnn[x] - (3 * (srcpp[x] + srcpn[x]))) > cthresh6)
            cmkp[x] = 0xFF;
        }
      }
      srcppp += src_pitch;
      srcpp += src_pitch;
      srcp += src_pitch;
      srcpn += src_pitch;
      srcpnn += src_pitch;
      cmkp += cmk_pitch;
      // middle Height - 4
      const int lines_to_process = Height - 4;
      check_combing_c<pixel_t>(srcp, cmkp, Width, lines_to_process, src_pitch, cmk_pitch, scaled_cthresh);
      srcppp += src_pitch * lines_to_process;
      srcpp += src_pitch * lines_to_process;
      srcp += src_pitch * lines_to_process;
      srcpn += src_pitch * lines_to_process;
      srcpnn += src_pitch * lines_to_process;
      cmkp += cmk_pitch * lines_to_process;
      // bottom #-2
      for (int x = 0; x < Width; ++x)
      {
        const int sFirst = srcp[x] - srcpp[x];
        const int sSecond = srcp[x] - srcpn[x];
        if ((sFirst > scaled_cthresh && sSecond > scaled_cthresh) || (sFirst < -scaled_cthresh && sSecond < -scaled_cthresh))
        {
          if (abs(srcppp[x] + (srcp[x] << 2) + srcppp[x] - (3 * (srcpp[x] + srcpn[x]))) > cthresh6)
            cmkp[x] = 0xFF;
        }
      }
      srcppp += src_pitch;
      srcpp += src_pitch;
      srcp += src_pitch;
      srcpn += src_pitch;
      srcpnn += src_pitch;
      cmkp += cmk_pitch;
      // bottom #-1
      for (int x = 0; x < Width; ++x)
      {
        const int sFirst = srcp[x] - srcpp[x];
        if (sFirst > scaled_cthresh || sFirst < -scaled_cthresh)
        {
          if (abs(srcppp[x] + (srcp[x] << 2) + srcppp[x] - (3 * (srcpp[x] + srcpp[x]))) > cthresh6)
            cmkp[x] = 0xFF;
        }
      }
    }
    else
    {
      // metric == 1: squared
      typedef typename std::conditional<sizeof(pixel_t) == 1, int, int64_t> ::type safeint_t;
      const safeint_t cthreshsq = (safeint_t)scaled_cthresh * scaled_cthresh;
      // top #1
      for (int x = 0; x < Width; ++x)
      {
        if ((safeint_t)(srcp[x] - srcpn[x]) * (srcp[x] - srcpn[x]) > cthreshsq)
          cmkp[x] = 0xFF;
      }
      srcpp += src_pitch;
      srcp += src_pitch;
      srcpn += src_pitch;
      cmkp += cmk_pitch;
      // middle Height - 2
      const int lines_to_process = Height - 2;
      check_combing_c_Metric1<pixel_t, safeint_t>(srcp, cmkp, Width, lines_to_process, src_pitch, cmk_pitch, cthreshsq);
      srcpp += src_pitch * lines_to_process;
      srcp += src_pitch * lines_to_process;
      srcpn += src_pitch * lines_to_process;
      cmkp += cmk_pitch * lines_to_process;
      // Bottom
      for (int x = 0; x < Width; ++x)
      {
        if ((safeint_t)(srcp[x] - srcpp[x]) * (srcp[x] - srcpp[x]) > cthreshsq)
          cmkp[x] = 0xFF;
      }
    }
  }

  // next block is for mask, no hbd needed
  // Includes chroma combing in the decision about whether a frame is combed.
  if (chroma)
  {
    if (vi->format.subSamplingW == 1 && vi->format.subSamplingH == 1) FillCombedPlanarUpdateCmaskByUV<420>(cmask, vsapi);
    else if (vi->format.subSamplingW == 1 && vi->format.subSamplingH == 0) FillCombedPlanarUpdateCmaskByUV<422>(cmask, vsapi);
    else if (vi->format.subSamplingW == 0 && vi->format.subSamplingH == 0) FillCombedPlanarUpdateCmaskByUV<444>(cmask, vsapi);
    else if (vi->format.subSamplingW == 2 && vi->format.subSamplingH == 0) FillCombedPlanarUpdateCmaskByUV<411>(cmask, vsapi);
  }
  // till now now it's the same as in TFMPlanar::checkCombedPlanar
}

// instantiate
template void checkCombedPlanarAnalyze_core<uint8_t>(const VSVideoInfo *vi, int cthresh, bool chroma, int metric, const VSFrame *src, VSFrame* cmask, const VSAPI *vsapi);
template void checkCombedPlanarAnalyze_core<uint16_t>(const VSVideoInfo *vi, int cthresh, bool chroma, int metric, const VSFrame *src, VSFrame* cmask, const VSAPI *vsapi);


bool TFM::checkCombedPlanar(const VSFrame *src, int n, int match,
  int *blockN, int &xblocksi, int *mics, bool ddebug, bool _chroma)
{
  if (mics[match] != -20)
  {
    if (mics[match] > MI)
    {
//      if (debug && !ddebug)
//      {
//        sprintf(buf, "TFM:  frame %d  - match %c:  Detected As Combed  (ReCheck - not processed)! (%d > %d)\n",
//          n, MTC(match), mics[match], MI);
//        OutputDebugString(buf);
//      }
      return true;
    }
//    if (debug && !ddebug)
//    {
//      sprintf(buf, "TFM:  frame %d  - match %c:  Detected As NOT Combed  (ReCheck - not processed)! (%d <= %d)\n",
//        n, MTC(match), mics[match], MI);
//      OutputDebugString(buf);
//    }
    return false;
  }

  const int bits_per_pixel = vi->format.bitsPerSample;
  if (vi->format.bytesPerSample == 1) {
    checkCombedPlanarAnalyze_core<uint8_t>(vi, cthresh, _chroma, metric, src, cmask.get(), vsapi);
    return checkCombedPlanar_core<uint8_t>(src, n, match, blockN, xblocksi, mics, ddebug, bits_per_pixel);
  }
  else {
    checkCombedPlanarAnalyze_core<uint16_t>(vi, cthresh, _chroma, metric, src, cmask.get(), vsapi);
    return checkCombedPlanar_core<uint16_t>(src, n, match, blockN, xblocksi, mics, ddebug, bits_per_pixel);
  }
}

template<typename pixel_t>
bool TFM::checkCombedPlanar_core([[maybe_unused]] const VSFrame *src, [[maybe_unused]] int n, int match,
  int* blockN, int& xblocksi, int* mics, [[maybe_unused]] bool ddebug, [[maybe_unused]] int bits_per_pixel)
{

  const ptrdiff_t cmk_pitch = vsapi->getStride(cmask.get(), 0);
  const uint8_t *cmkp = vsapi->getWritePtr(cmask.get(), 0) + cmk_pitch;
  const uint8_t *cmkpp = cmkp - cmk_pitch;
  const uint8_t *cmkpn = cmkp + cmk_pitch;
  const int Width = vsapi->getFrameWidth(cmask.get(), 0);
  const int Height = vsapi->getFrameHeight(cmask.get(), 0);
  const int xblocks = ((Width + xhalf) >> xshift) + 1;
  const int xblocks4 = xblocks << 2;
  xblocksi = xblocks4;
  const int yblocks = ((Height + yhalf) >> yshift) + 1;
  const int arraysize = (xblocks*yblocks) << 2;
  std::fill(cArray.begin(), cArray.end(), 0);

  int Heighta = (Height >> (yshift - 1)) << (yshift - 1);
  if (Heighta == Height) Heighta = Height - yhalf;
  const int Widtha = (Width >> (xshift - 1)) << (xshift - 1);
  for (int y = 1; y < yhalf; ++y)
  {
    const int temp1 = (y >> yshift)*xblocks4;
    const int temp2 = ((y + yhalf) >> yshift)*xblocks4;
    for (int x = 0; x < Width; ++x)
    {
      if (cmkpp[x] == 0xFF && cmkp[x] == 0xFF && cmkpn[x] == 0xFF)
      {
        const int box1 = (x >> xshift) << 2;
        const int box2 = ((x + xhalf) >> xshift) << 2;
        ++cArray[temp1 + box1 + 0];
        ++cArray[temp1 + box2 + 1];
        ++cArray[temp2 + box1 + 2];
        ++cArray[temp2 + box2 + 3];
      }
    }
    cmkpp += cmk_pitch;
    cmkp += cmk_pitch;
    cmkpn += cmk_pitch;
  }
  for (int y = yhalf; y < Heighta; y += yhalf)
  {
    const int temp1 = (y >> yshift)*xblocks4;
    const int temp2 = ((y + yhalf) >> yshift)*xblocks4;
    {
      for (int x = 0; x < Widtha; x += xhalf)
      {
        const uint8_t *cmkppT = cmkpp;
        const uint8_t *cmkpT = cmkp;
        const uint8_t *cmkpnT = cmkpn;
        int sum = 0;
        for (int u = 0; u < yhalf; ++u)
        {
          for (int v = 0; v < xhalf; ++v)
          {
            if (cmkppT[x + v] == 0xFF && cmkpT[x + v] == 0xFF &&
              cmkpnT[x + v] == 0xFF) ++sum;
          }
          cmkppT += cmk_pitch;
          cmkpT += cmk_pitch;
          cmkpnT += cmk_pitch;
        }
        if (sum)
        {
          const int box1 = (x >> xshift) << 2;
          const int box2 = ((x + xhalf) >> xshift) << 2;
          cArray[temp1 + box1 + 0] += sum;
          cArray[temp1 + box2 + 1] += sum;
          cArray[temp2 + box1 + 2] += sum;
          cArray[temp2 + box2 + 3] += sum;
        }
      }
    }
    // rest
    for (int x = Widtha; x < Width; ++x)
    {
      const uint8_t *cmkppT = cmkpp;
      const uint8_t *cmkpT = cmkp;
      const uint8_t *cmkpnT = cmkpn;
      int sum = 0;
      for (int u = 0; u < yhalf; ++u)
      {
        if (cmkppT[x] == 0xFF && cmkpT[x] == 0xFF &&
          cmkpnT[x] == 0xFF) ++sum;
        cmkppT += cmk_pitch;
        cmkpT += cmk_pitch;
        cmkpnT += cmk_pitch;
      }
      if (sum)
      {
        const int box1 = (x >> xshift) << 2;
        const int box2 = ((x + xhalf) >> xshift) << 2;
        cArray[temp1 + box1 + 0] += sum;
        cArray[temp1 + box2 + 1] += sum;
        cArray[temp2 + box1 + 2] += sum;
        cArray[temp2 + box2 + 3] += sum;
      }
    }
    cmkpp += cmk_pitch*yhalf;
    cmkp += cmk_pitch*yhalf;
    cmkpn += cmk_pitch*yhalf;
  }
  for (int y = Heighta; y < Height - 1; ++y)
  {
    const int temp1 = (y >> yshift)*xblocks4;
    const int temp2 = ((y + yhalf) >> yshift)*xblocks4;
    for (int x = 0; x < Width; ++x)
    {
      if (cmkpp[x] == 0xFF && cmkp[x] == 0xFF && cmkpn[x] == 0xFF)
      {
        const int box1 = (x >> xshift) << 2;
        const int box2 = ((x + xhalf) >> xshift) << 2;
        ++cArray[temp1 + box1 + 0];
        ++cArray[temp1 + box2 + 1];
        ++cArray[temp2 + box1 + 2];
        ++cArray[temp2 + box2 + 3];
      }
    }
    cmkpp += cmk_pitch;
    cmkp += cmk_pitch;
    cmkpn += cmk_pitch;
  }
  for (int x = 0; x < arraysize; ++x)
  {
    if (cArray[x] > mics[match])
    {
      mics[match] = cArray[x];
      blockN[match] = x;
    }
  }
  if (mics[match] > MI)
  {
//    if (debug && !ddebug)
//    {
//      sprintf(buf, "TFM:  frame %d  - match %c:  Detected As Combed! (%d > %d)\n",
//        n, MTC(match), mics[match], MI);
//      OutputDebugString(buf);
//    }
    return true;
  }
//  if (debug && !ddebug)
//  {
//    sprintf(buf, "TFM:  frame %d  - match %c:  Detected As NOT Combed! (%d <= %d)\n",
//      n, MTC(match), mics[match], MI);
//    OutputDebugString(buf);
//  }
  return false;
}

template<typename pixel_t>
void TFM::buildDiffMapPlane_Planar(const uint8_t *prvp, const uint8_t *nxtp,
  uint8_t *dstp, ptrdiff_t prv_pitch, ptrdiff_t nxt_pitch, ptrdiff_t dst_pitch, int Height,
  int Width, ptrdiff_t tpitch, int bits_per_pixel)
{
  buildABSDiffMask<pixel_t>(prvp - prv_pitch, nxtp - nxt_pitch, prv_pitch, nxt_pitch, tpitch, Width, Height >> 1);
  switch (bits_per_pixel) {
  case 8: AnalyzeDiffMask_Planar<uint8_t, 8>(dstp, dst_pitch, tbuffer.data(), tpitch, Width, Height); break;
  case 10: AnalyzeDiffMask_Planar<uint16_t, 10>(dstp, dst_pitch, tbuffer.data(), tpitch, Width, Height); break;
  case 12: AnalyzeDiffMask_Planar<uint16_t, 12>(dstp, dst_pitch, tbuffer.data(), tpitch, Width, Height); break;
  case 14: AnalyzeDiffMask_Planar<uint16_t, 14>(dstp, dst_pitch, tbuffer.data(), tpitch, Width, Height); break;
  case 16: AnalyzeDiffMask_Planar<uint16_t, 16>(dstp, dst_pitch, tbuffer.data(), tpitch, Width, Height); break;
  }
}

// instantiate
template void TFM::buildDiffMapPlane_Planar<uint8_t>(const uint8_t* prvp, const uint8_t* nxtp,
  uint8_t* dstp, ptrdiff_t prv_pitch, ptrdiff_t nxt_pitch, ptrdiff_t dst_pitch, int Height,
  int Width, ptrdiff_t tpitch, int bits_per_pixel);
template void TFM::buildDiffMapPlane_Planar<uint16_t>(const uint8_t* prvp, const uint8_t* nxtp,
  uint8_t* dstp, ptrdiff_t prv_pitch, ptrdiff_t nxt_pitch, ptrdiff_t dst_pitch, int Height,
  int Width, ptrdiff_t tpitch, int bits_per_pixel);


