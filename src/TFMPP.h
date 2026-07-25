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

#include <string>
#include <vector>
#include <math.h>
#include <VapourSynth4.h>
#ifdef VERSION
#undef VERSION
#endif
#define VERSION "v1.0.3"

template<typename pixel_t>
void maskClip2_C(const uint8_t* srcp, const uint8_t* dntp,
  const uint8_t* maskp, uint8_t* dstp, ptrdiff_t src_pitch, ptrdiff_t dnt_pitch,
  ptrdiff_t msk_pitch, ptrdiff_t dst_pitch, int width, int height);

template<typename pixel_t, bool with_mask>
void blendDeintMask_C(const pixel_t* srcp, pixel_t* dstp,
  const uint8_t* maskp, ptrdiff_t src_pitch, ptrdiff_t dst_pitch, ptrdiff_t msk_pitch,
  int width, int height);

template<typename pixel_t, int bits_per_pixel, bool with_mask>
void cubicDeintMask_C(const pixel_t* srcp, pixel_t* dstp,
  const uint8_t* maskp, ptrdiff_t src_pitch, ptrdiff_t dst_pitch, ptrdiff_t msk_pitch,
  int width, int height);

class TFMPP
{
private:
    const VSAPI *vsapi;
    VSNode *child;


  int PP, mthresh;
  std::string ovr;
  bool display;
  VSNode *clip2;
  int opt;
  bool uC2; // use clip2
  int PP_origSaved;
  int mthresh_origSaved;
  int nfrms;
  std::vector<int> setArray;
  VSFrame *mmask;

  void buildMotionMask(const VSFrame *prv, const VSFrame *src, const VSFrame *nxt,
    VSFrame *mask, int use) const;
  template<typename pixel_t>
  void buildMotionMask_core(const VSFrame *prv, const VSFrame *src, const VSFrame *nxt,
    VSFrame* mask, int use) const;
  void maskClip2(const VSFrame *src, const VSFrame *deint, const VSFrame *mask,
    VSFrame *dst) const;

  void getProperties(const VSFrame *src, int& field, bool& combed) const;
  void getSetOvr(int n);
  int getEffectivePP(int n) const; // effective PP for frame n (base + P overrides), no side effects

  void denoisePlanar(VSFrame *mask) const;

  template<int planarType>
  void linkPlanar(VSFrame *mask) const;

  void BlendDeint(const VSFrame *src, const VSFrame *mask, VSFrame *dst,
    bool nomask) const;
  template<typename pixel_t>
  void BlendDeint_core(const VSFrame *src, const VSFrame* mask, VSFrame *dst,
    bool nomask) const;

  void CubicDeint(const VSFrame *src, const VSFrame *mask, VSFrame *dst, bool nomask,
    int field) const;
  template<typename pixel_t, int bits_per_pixel>
  void CubicDeint_core(const VSFrame *src, const VSFrame* mask, VSFrame *dst, bool nomask,
    int field) const;

  void elaDeint(VSFrame *dst, const VSFrame *mask, const VSFrame *src, bool nomask, int field) const;
  // not the same as in tdeinterlace.
  template<typename pixel_t, int bits_per_pixel>
  void elaDeintPlanar(VSFrame *dst, const VSFrame *mask, const VSFrame *src, bool nomask, int field) const;

  void copyField(VSFrame *dst, const VSFrame *src, int field) const;

  void writeDisplay(VSFrame *dst, int n, int field) const;

public:
  const VSVideoInfo *vi;

  void parseOvrFile();
  const VSFrame *GetFrame(int n, int activationReason, VSFrameContext *frameCtx, VSCore *core);
  TFMPP(VSNode *_child, int _PP, int _mthresh, const char* _ovr, bool _display, VSNode *_clip2,
    bool _usehints, int _opt, const VSAPI *_vsapi, VSCore *core);
  ~TFMPP();
};
