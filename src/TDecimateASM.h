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

#ifndef __TDECIMATEASM_H__
#define __TDECIMATEASM_H__

//#include <windows.h>
#include <VapourSynth.h>
#include "internal.h"
#include "TDecimate.h"


// generic



//-- helpers




template<typename pixel_t, bool SAD, int inc>
void calcDiff_SADorSSD_Generic_c(const pixel_t* prvp, const pixel_t* curp,
  int prv_pitch, int cur_pitch, int width, int height, int plane, int xblocks4, uint64_t* diff, bool chroma, int xshiftS, int yshiftS, int xhalfS, int yhalfS, int nt, const VSVideoInfo *vi);

void CalcMetricsExtracted(const VSFrameRef *prevt, const VSFrameRef *currt, CalcMetricData& d, VSCore *core, const VSAPI *vsapi);

template<typename pixel_t>
void HorizontalBlur_Planar_c(const uint8_t* srcp, uint8_t* dstp, int src_pitch,
  int dst_pitch, int width, int height, bool allow_leftminus1);

void HorizontalBlur(const VSFrameRef *src, VSFrameRef *dst, bool bchroma,
  const VSAPI *vsapi);

template<typename pixel_t>
void VerticalBlur_c(const uint8_t* srcp, uint8_t* dstp, int src_pitch,
  int dst_pitch, int width, int height);

void VerticalBlur(const VSFrameRef *src, VSFrameRef *dst, bool bchroma, const VSAPI *vsapi);


// handles 50% special case as well
void dispatch_blend(uint8_t* dstp, const uint8_t* srcp1, const uint8_t* srcp2, int width, int height,
  int dst_pitch, int src1_pitch, int src2_pitch, int weight_i, int bits_per_pixel);

#endif // __TDECIMATEASM_H__
