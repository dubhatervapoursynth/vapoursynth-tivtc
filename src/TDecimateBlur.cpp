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

// hbd ready
void blurFrame(const VSFrame *src, VSFrame *dst, int iterations,
  bool bchroma, VSCore *core, const VSAPI *vsapi)
{
    const VSVideoFormat *format = vsapi->getVideoFrameFormat(src);
    int width = vsapi->getFrameWidth(src, 0);
    int height = vsapi->getFrameHeight(src, 0);

  VSFrame *tmp = vsapi->newVideoFrame(format, width, height, nullptr, core);
  HorizontalBlur(src, tmp, bchroma, vsapi);
  VerticalBlur(tmp, dst, bchroma, vsapi);
  for (int i = 1; i < iterations; ++i)
  {
    HorizontalBlur(dst, tmp, bchroma, vsapi);
    VerticalBlur(tmp, dst, bchroma, vsapi);
  }
  vsapi->freeFrame(tmp);
}

void HorizontalBlur(const VSFrame *src, VSFrame *dst, bool bchroma,
  const VSAPI *vsapi)
{
    const VSVideoFormat *format = vsapi->getVideoFrameFormat(src);

  const int np = !bchroma ? 1 : format->numPlanes;


  const int pixelsize = format->bytesPerSample;

  for (int b = 0; b < np; ++b)
  {
    const int plane = b;
    const uint8_t *srcp = vsapi->getReadPtr(src, plane);
    ptrdiff_t src_pitch = vsapi->getStride(src, plane);
    int width = vsapi->getFrameWidth(src, plane);
    int height = vsapi->getFrameHeight(src, plane);
    uint8_t *dstp = vsapi->getWritePtr(dst, plane);
    ptrdiff_t dst_pitch = vsapi->getStride(dst, plane);

      if(pixelsize == 1)
        HorizontalBlur_Planar_c<uint8_t>(srcp, dstp, src_pitch, dst_pitch, width, height, false);
      else // 10-16 bits
        HorizontalBlur_Planar_c<uint16_t>(srcp, dstp, src_pitch, dst_pitch, width, height, false);
  }
}

template<typename pixel_t>
void VerticalBlur_c(const uint8_t* srcp0, uint8_t* dstp0, ptrdiff_t src_pitch,
  ptrdiff_t dst_pitch, int width, int height)
{
  if (width == 0) return;

  pixel_t* __restrict dstp = reinterpret_cast<pixel_t *>(dstp0);
  const pixel_t* srcp = reinterpret_cast<const pixel_t*>(srcp0);
  src_pitch /= sizeof(pixel_t);
  dst_pitch /= sizeof(pixel_t);

  // the row above and below are addressed off srcp rather than tracked separately
  // top line
  for (int x = 0; x < width; x++)
    dstp[x] = (srcp[x] + srcp[x + src_pitch] + 1) >> 1;
  srcp += src_pitch;
  dstp += dst_pitch;
  // height - 2 lines in between
  for (int y = 1; y < height - 1; ++y)
  {
    for (int x = 0; x < width; x++)
      dstp[x] = (srcp[x - src_pitch] + (srcp[x] << 1) + srcp[x + src_pitch] + 2) >> 2;
    srcp += src_pitch;
    dstp += dst_pitch;
  }
  // bottom line
  for (int x = 0; x < width; x++)
    dstp[x] = (srcp[x - src_pitch] + srcp[x] + 1) >> 1;
}

void VerticalBlur(const VSFrame *src, VSFrame *dst, bool bchroma,
  const VSAPI *vsapi)
{
    const VSVideoFormat *format = vsapi->getVideoFrameFormat(src);

  const int np = !bchroma ? 1 : format->numPlanes;


  const int pixelsize = format->bytesPerSample;

  for (int b = 0; b < np; ++b)
  {
    const int plane = b;
    const uint8_t* srcp = vsapi->getReadPtr(src, plane);
    ptrdiff_t src_pitch = vsapi->getStride(src, plane);
    int width = vsapi->getFrameWidth(src, plane);
    int height = vsapi->getFrameHeight(src, plane);
    uint8_t* dstp = vsapi->getWritePtr(dst, plane);
    ptrdiff_t dst_pitch = vsapi->getStride(dst, plane);

      if(pixelsize == 1)
        VerticalBlur_c<uint8_t>(srcp, dstp, src_pitch, dst_pitch, width, height);
      else // 10-16 bits
        VerticalBlur_c<uint16_t>(srcp, dstp, src_pitch, dst_pitch, width, height);

  }
}

template<typename pixel_t>
void HorizontalBlur_Planar_c(const uint8_t* srcp0, uint8_t* __restrict dstp0, ptrdiff_t src_pitch,
  ptrdiff_t dst_pitch, int width, int height, bool allow_leftminus1)
{
  if (width == 0)
    return;

  pixel_t* dstp = reinterpret_cast<pixel_t*>(dstp0);
  const pixel_t* srcp = reinterpret_cast<const pixel_t*>(srcp0);
  src_pitch /= sizeof(pixel_t);
  dst_pitch /= sizeof(pixel_t);

  if (width >= 2) {
    const int startx = allow_leftminus1 ? 0 : 1;
    for (int y = 0; y < height; ++y)
    {
      if (!allow_leftminus1)
        dstp[0] = (srcp[0] + srcp[1] + 1) >> 1;
      int x;
      for (x = startx; x < width - 1; ++x)
        dstp[x] = (srcp[x - 1] + (srcp[x] << 1) + srcp[x + 1] + 2) >> 2;
      dstp[x] = (srcp[x - 1] + srcp[x] + 1) >> 1;
      srcp += src_pitch;
      dstp += dst_pitch;
    }
    return;
  }

  // width == 1
  for (int y = 0; y < height; ++y)
  {
    if (allow_leftminus1)
      dstp[0] = (srcp[-1] + srcp[0] + 1) >> 1;
    else
      dstp[0] = srcp[0];
    srcp += src_pitch;
    dstp += dst_pitch;
  }
}
