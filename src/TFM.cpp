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

enum _FieldBased {
    Progressive = 0,
    BottomFieldFirst = 1,
    TopFieldFirst = 2
};

// The order/field pair decides which physical field each match code names, so it has to be
// settled before any comparison happens. order == -1 means "take it from the frame properties".
bool TFM::resolveFieldOrder(FrameMatchState &st, VSFrameContext *frameCtx)
{
  const VSMap *props = vsapi->getFramePropertiesRO(st.src);
  if (order == -1)
  {
    int err;
    const int64_t field_based = vsapi->mapGetInt(props, "_FieldBased", 0, &err);
    if (err) // prop not present
    {
      vsapi->setFilterError("TFM: Couldn't find the '_FieldBased' frame property. The 'order' parameter must be used.", frameCtx);
      return false;
    }

    /// Pretend it's top field first when it says progressive?
    order = (field_based == TopFieldFirst || field_based == Progressive);
  }
  if (field == -1) field = order;
  st.frstT = field^order ? 2 : 0;
  st.scndT = (mode == 2 || mode == 6) ? (field^order ? 3 : 4) : (field^order ? 0 : 2);
  return true;
}

// Weave and measure whichever of the five matches have not been measured yet, so that the mic
// values written to the output file (and used by micmatching) are complete. Without allMatches
// only p/c/n are filled unless micout asks for all five.
void TFM::fillMissingMics(FrameMatchState &st, bool allMatches)
{
  for (int i = 0; i < 5; ++i)
  {
    if (st.mics[i] == -20 && (i < 3 || micout > 1 || allMatches))
    {
      createWeaveFrame(st.tmp, st.prv, st.src, st.nxt, i, st.tfrm);
      checkCombed(st.tmp, st.n, i, st.blockN, st.xblocks, st.mics, true);
    }
  }
}

// Common tail of both exits: report the decision, annotate the frame, and hand dst over to the
// caller. fromOvr distinguishes a match taken from the ovr file from one this frame worked out.
const VSFrame *TFM::finishFrame(FrameMatchState &st, bool d2vfilm, bool fromOvr)
{
  fileOut(st.fmatch, st.combed, d2vfilm, st.n, st.mics[st.fmatch], st.mics);
  if (display) writeDisplay(st.dst, st.n, st.fmatch, st.combed, fromOvr, st.blockN[st.fmatch],
    st.xblocks, st.d2vmatch, st.mics, st.prv, st.src, st.nxt);
  if (debug)
  {
    char buft[20];
    if (st.mics[st.fmatch] < 0) snprintf(buft, sizeof(buft), "N/A");
    else snprintf(buft, sizeof(buft), "%d", st.mics[st.fmatch]);
    logInfo(vsapi, vscore, "TFM:  frame {}  - final match = {} {}  MIC = {}", st.n, matchChar(st.fmatch),
      fromOvr && st.d2vmatch ? "(D2V)" : fromOvr ? "(OVR)" : "", buft);
    if (micout > 0 || (micmatching > 0 && st.mics[0] != -20 && st.mics[1] != -20 && st.mics[2] != -20
      && st.mics[3] != -20 && st.mics[4] != -20))
    {
      if (micout > 1 || micmatching > 0)
        logInfo(vsapi, vscore, "TFM:  frame {}  - mics: p = {}  c = {}  n = {}  b = {}  u = {}",
          st.n, st.mics[0], st.mics[1], st.mics[2], st.mics[3], st.mics[4]);
      else
        logInfo(vsapi, vscore, "TFM:  frame {}  - mics: p = {}  c = {}  n = {}",
          st.n, st.mics[0], st.mics[1], st.mics[2]);
    }
    logInfo(vsapi, vscore, "TFM:  frame {}  - mode = {}  field = {}  order = {}  d2vfilm = {}",
      st.n, mode, field, order, d2vfilm ? 'T' : 'F');
    if (st.combed != -1)
    {
      if (st.combed == 1) logInfo(vsapi, vscore, "TFM:  frame {}  - CLEAN FRAME  (forced!)", st.n);
      else if (st.combed == 5) logInfo(vsapi, vscore, "TFM:  frame {}  - COMBED FRAME  (forced!)", st.n);
      else if (st.combed == 0) logInfo(vsapi, vscore, "TFM:  frame {}  - CLEAN FRAME", st.n);
      else logInfo(vsapi, vscore, "TFM:  frame {}  - COMBED FRAME", st.n);
    }
  }
  if (usehints || PP >= 2) putFrameProperties(st.dst, st.fmatch, st.combed, d2vfilm, st.mics);
  lastMatch.frame = st.n;
  lastMatch.match = st.fmatch;
  lastMatch.field = field;
  lastMatch.combed = st.combed;

  vsapi->freeFrame(st.prv);
  vsapi->freeFrame(st.src);
  vsapi->freeFrame(st.nxt);
  vsapi->freeFrame(st.tmp);
  return st.dst;
}

// Mode 6 tries all four alternates in a fixed order and stops at the first one that is not
// combed, so a frame only stays combed if every candidate does.
void TFM::matchMode6(FrameMatchState &st)
{
  const int thrdT = field^order ? 0 : 2;
  const int frthT = field^order ? 4 : 3;
  int tcombed = 0;
  int nmatch1, nmatch2, mmatch1, mmatch2;
  bool isSC = true;

  if (!slow) st.fmatch = compareFields(st.prv, st.src, st.nxt, 1, st.frstT, nmatch1, nmatch2, mmatch1, mmatch2, st.n);
  else st.fmatch = compareFieldsSlow(st.prv, st.src, st.nxt, 1, st.frstT, nmatch1, nmatch2, mmatch1, mmatch2, st.n);
  if (micmatching > 0)
    checkmm(st.fmatch, 1, st.frstT, st.dst, st.dfrm, st.tmp, st.tfrm, st.prv, st.src, st.nxt, st.n,
      st.blockN, st.xblocks, st.mics);
  createWeaveFrame(st.dst, st.prv, st.src, st.nxt, st.fmatch, st.dfrm);
  if (checkCombed(st.dst, st.n, st.fmatch, st.blockN, st.xblocks, st.mics, false))
  {
    tcombed = 2;
    if (ubsco) isSC = checkSceneChange(st.prv, st.src, st.nxt, st.n);
    if (isSC) createWeaveFrame(st.tmp, st.prv, st.src, st.nxt, st.scndT, st.tfrm);
    if (isSC && !checkCombed(st.tmp, st.n, st.scndT, st.blockN, st.xblocks, st.mics, false))
    {
      st.fmatch = st.scndT;
      tcombed = 0;
      copyFrame(st.dst, st.tmp, vsapi);
      st.dfrm = st.fmatch;
    }
    else
    {
      createWeaveFrame(st.tmp, st.prv, st.src, st.nxt, thrdT, st.tfrm);
      if (!checkCombed(st.tmp, st.n, thrdT, st.blockN, st.xblocks, st.mics, false))
      {
        st.fmatch = thrdT;
        tcombed = 0;
        copyFrame(st.dst, st.tmp, vsapi);
        st.dfrm = st.fmatch;
      }
      else
      {
        if (isSC) createWeaveFrame(st.tmp, st.prv, st.src, st.nxt, frthT, st.tfrm);
        if (isSC && !checkCombed(st.tmp, st.n, frthT, st.blockN, st.xblocks, st.mics, false))
        {
          st.fmatch = frthT;
          tcombed = 0;
          copyFrame(st.dst, st.tmp, vsapi);
          st.dfrm = st.fmatch;
        }
      }
    }
  }
  if (st.combed == -1 && PP > 0) st.combed = tcombed;
}

// Mode 7 does not search: it weaves c and the parity alternate, and picks whichever is clean.
// If both are clean the field order is flipped for the next frame; if neither is, the frame keeps
// the previous frame's field and is reported combed.
void TFM::matchMode7(FrameMatchState &st)
{
  if (debug && lastMatch.frame != st.n && st.n != 0)
    logInfo(vsapi, vscore, "TFM:  mode 7 - non-linear access detected!");
  st.combed = 0;
  int nmatch1, nmatch2, mmatch1, mmatch2;
  if (!slow) st.fmatch = compareFields(st.prv, st.src, st.nxt, 1, st.frstT, nmatch1, nmatch2, mmatch1, mmatch2, st.n);
  else st.fmatch = compareFieldsSlow(st.prv, st.src, st.nxt, 1, st.frstT, nmatch1, nmatch2, mmatch1, mmatch2, st.n);
  createWeaveFrame(st.dst, st.prv, st.src, st.nxt, 1, st.dfrm);
  const bool combed1 = checkCombed(st.dst, st.n, 1, st.blockN, st.xblocks, st.mics, false);
  createWeaveFrame(st.dst, st.prv, st.src, st.nxt, st.frstT, st.dfrm);
  const bool combed2 = checkCombed(st.dst, st.n, st.frstT, st.blockN, st.xblocks, st.mics, false);
  if (!combed1 && !combed2)
  {
    createWeaveFrame(st.dst, st.prv, st.src, st.nxt, st.fmatch, st.dfrm);
    if (field == 0) mode7_field = 1;
    else mode7_field = 0;
  }
  else if (!combed2 && combed1)
  {
    createWeaveFrame(st.dst, st.prv, st.src, st.nxt, st.frstT, st.dfrm);
    mode7_field = 1;
    st.fmatch = st.frstT;
  }
  else if (!combed1 && combed2)
  {
    createWeaveFrame(st.dst, st.prv, st.src, st.nxt, 1, st.dfrm);
    mode7_field = 0;
    st.fmatch = 1;
  }
  else
  {
    createWeaveFrame(st.dst, st.prv, st.src, st.nxt, 1, st.dfrm);
    st.combed = 2;
    field = mode7_field;
    st.fmatch = 1;
  }
}

// Modes 0-5: match c against the parity alternate, then escalate through progressively wider
// candidate sets while the result is still combed and the mode allows another try.
bool TFM::matchModeNormal(FrameMatchState &st, VSFrameContext *frameCtx)
{
  int tcombed = -1;
  int nmatch1, nmatch2, mmatch1, mmatch2, tmatch;

  if (!slow)
    st.fmatch = compareFields(st.prv, st.src, st.nxt, 1, st.frstT, nmatch1, nmatch2, mmatch1, mmatch2, st.n);
  else
    st.fmatch = compareFieldsSlow(st.prv, st.src, st.nxt, 1, st.frstT, nmatch1, nmatch2, mmatch1, mmatch2, st.n);
  if (micmatching > 0)
    checkmm(st.fmatch, 1, st.frstT, st.dst, st.dfrm, st.tmp, st.tfrm, st.prv, st.src, st.nxt, st.n,
      st.blockN, st.xblocks, st.mics);
  createWeaveFrame(st.dst, st.prv, st.src, st.nxt, st.fmatch, st.dfrm);
  if (mode > 3 || (mode > 0 && checkCombed(st.dst, st.n, st.fmatch, st.blockN, st.xblocks, st.mics, false)))
  {
    if (mode < 4) tcombed = 2;
    if (mode != 2)
    {
      if (!slow)
        tmatch = compareFields(st.prv, st.src, st.nxt, st.fmatch, st.scndT, nmatch1, nmatch2, mmatch1, mmatch2, st.n);
      else
        tmatch = compareFieldsSlow(st.prv, st.src, st.nxt, st.fmatch, st.scndT, nmatch1, nmatch2, mmatch1, mmatch2, st.n);
      if (micmatching > 0)
        checkmm(tmatch, st.fmatch, st.scndT, st.dst, st.dfrm, st.tmp, st.tfrm, st.prv, st.src, st.nxt, st.n,
          st.blockN, st.xblocks, st.mics);
      createWeaveFrame(st.dst, st.prv, st.src, st.nxt, st.fmatch, st.dfrm);
    }
    else tmatch = st.scndT;
    if (tmatch == st.scndT)
    {
      if (mode > 3)
      {
        st.fmatch = tmatch;
        createWeaveFrame(st.dst, st.prv, st.src, st.nxt, st.fmatch, st.dfrm);
      }
      else if (mode != 2 || !ubsco || checkSceneChange(st.prv, st.src, st.nxt, st.n))
      {
        createWeaveFrame(st.tmp, st.prv, st.src, st.nxt, tmatch, st.tfrm);
        if (!checkCombed(st.tmp, st.n, tmatch, st.blockN, st.xblocks, st.mics, false))
        {
          st.fmatch = tmatch;
          tcombed = 0;
          copyFrame(st.dst, st.tmp, vsapi);
          st.dfrm = st.fmatch;
        }
      }
    }
    // modes 3 and 5 get one more try, against the two deinterlaced-c matches
    if ((mode == 3 && tcombed == 2) ||
      (mode == 5 && checkCombed(st.dst, st.n, st.fmatch, st.blockN, st.xblocks, st.mics, false)))
    {
      tcombed = 2;
      if (!ubsco || checkSceneChange(st.prv, st.src, st.nxt, st.n))
      {
        if (!slow)
          tmatch = compareFields(st.prv, st.src, st.nxt, 3, 4, nmatch1, nmatch2, mmatch1, mmatch2, st.n);
        else
          tmatch = compareFieldsSlow(st.prv, st.src, st.nxt, 3, 4, nmatch1, nmatch2, mmatch1, mmatch2, st.n);
        if (micmatching > 0)
          checkmm(tmatch, 3, 4, st.dst, st.dfrm, st.tmp, st.tfrm, st.prv, st.src, st.nxt, st.n,
            st.blockN, st.xblocks, st.mics);
        createWeaveFrame(st.tmp, st.prv, st.src, st.nxt, tmatch, st.tfrm);
        if (!checkCombed(st.tmp, st.n, tmatch, st.blockN, st.xblocks, st.mics, false))
        {
          st.fmatch = tmatch;
          tcombed = 0;
          copyFrame(st.dst, st.tmp, vsapi);
          st.dfrm = st.fmatch;
        }
        else
          createWeaveFrame(st.dst, st.prv, st.src, st.nxt, st.fmatch, st.dfrm);
      }
    }
    if (mode == 5 && tcombed == -1) tcombed = 0;
  }
  if ((mode == 1 || mode == 2 || mode == 3) && tcombed == -1) tcombed = 0;
  if (st.combed == -1 && PP > 0) st.combed = tcombed;
  if (PP > 0 && st.combed == -1)
  {
    if (checkCombed(st.dst, st.n, st.fmatch, st.blockN, st.xblocks, st.mics, false)) st.combed = 2;
    else st.combed = 0;
  }
  if (st.dfrm != st.fmatch)
  {
    vsapi->setFilterError("TFM: internal error (dfrm!=fmatch). Please report this.", frameCtx);
    return false;
  }
  return true;
}

// micmatching 1 (and the second half of 3): if one match is a clear mic winner over every other,
// switch to it, regardless of which candidate set the mode would normally consider. order1/order2
// are the mic values and their match codes, sorted ascending.
void TFM::micMatchBestOverall(FrameMatchState &st, const int *order1, const int *order2)
{
  if (order1[0] * 3 < order1[1] && abs(order1[0] - order1[1]) > 15 &&
    order1[0] < MI && order2[0] != st.fmatch &&
    (((field^order) && (order2[0] == 1 || order2[0] == 2 || order2[0] == 3)) ||
    (!(field^order) && (order2[0] == 0 || order2[0] == 1 || order2[0] == 4))))
  {
    // Only trust lastMatch when frames are guaranteed to arrive in order.
    const int lmatch = (linearAccess && lastMatch.frame == st.n - 1) ? lastMatch.match : -20;
    const bool xfield = (field^order) != 0;
    if (!((order2[0] == 4 && lmatch == 0 && !xfield && (order2[1] == 0 || order2[2] == 0)) ||
      (order2[0] == 3 && lmatch == 2 && xfield && (order2[1] == 2 || order2[2] == 2))))
    {
      micChange(st.n, st.fmatch, order2[0], st.dst, st.prv, st.src, st.nxt,
        st.fmatch, st.combed, st.dfrm);
    }
  }
  if (order1[0] * 4 < order1[1] && abs(order1[0] - order1[1]) > 30 &&
    order1[0] < MI && order1[1] >= MI && order2[0] != st.fmatch)
  {
    micChange(st.n, st.fmatch, order2[0], st.dst, st.prv, st.src, st.nxt,
      st.fmatch, st.combed, st.dfrm);
  }
}

// micmatching 2 and 3: consider only the matches the current mode would have searched, and switch
// to one whose mic is far below the best of the others.
void TFM::micMatchByMode(FrameMatchState &st)
{
  const int try1 = field^order ? 2 : 0;
  int try2, minm, mint, try3, try4;
  if (mode == 1) // p/c + n
  {
    try2 = try1 == 2 ? 0 : 2;
    minm = std::min(st.mics[1], st.mics[try1]);
    if (st.mics[try2] * 3 < minm && st.mics[try2] < MI && abs(st.mics[try2] - minm) >= 30 && try2 != st.fmatch)
      micChange(st.n, st.fmatch, try2, st.dst, st.prv, st.src, st.nxt,
        st.fmatch, st.combed, st.dfrm);
  }
  else if (mode == 2) // p/c + u
  {
    try2 = try1 == 2 ? 3 : 4;
    minm = std::min(st.mics[1], st.mics[try1]);
    if (st.mics[try2] * 3 < minm && st.mics[try2] < MI && abs(st.mics[try2] - minm) >= 30 && try2 != st.fmatch)
      micChange(st.n, st.fmatch, try2, st.dst, st.prv, st.src, st.nxt,
        st.fmatch, st.combed, st.dfrm);
  }
  else if (mode == 3) // p/c + n + u/b
  {
    try2 = try1 == 2 ? 0 : 2;
    minm = std::min(st.mics[1], st.mics[try1]);
    mint = std::min(st.mics[3], st.mics[4]);
    try3 = try1 == 2 ? (mint == st.mics[3] ? 3 : 4) : (mint == st.mics[4] ? 4 : 3);
    if (st.mics[try2] * 3 < minm && st.mics[try2] < MI && abs(st.mics[try2] - minm) >= 30 && try2 != st.fmatch &&
      st.fmatch != 3 && st.fmatch != 4)
    {
      micChange(st.n, st.fmatch, try2, st.dst, st.prv, st.src, st.nxt,
        st.fmatch, st.combed, st.dfrm);
      minm = st.mics[try2];
    }
    else if (st.fmatch == try2) minm = std::min(st.mics[try2], minm);
    if (mint * 3 < minm && mint < MI && abs(mint - minm) >= 30 && st.fmatch != 3 && st.fmatch != 4)
      micChange(st.n, st.fmatch, try3, st.dst, st.prv, st.src, st.nxt,
        st.fmatch, st.combed, st.dfrm);
  }
  else if (mode == 5) // p/c/n + u/b
  {
    minm = std::min(st.mics[0], std::min(st.mics[1], st.mics[2]));
    mint = std::min(st.mics[3], st.mics[4]);
    try3 = try1 == 2 ? (mint == st.mics[3] ? 3 : 4) : (mint == st.mics[4] ? 4 : 3);
    if (mint * 3 < minm && mint < MI && abs(mint - minm) >= 30 && st.fmatch != 3 && st.fmatch != 4)
      micChange(st.n, st.fmatch, try3, st.dst, st.prv, st.src, st.nxt,
        st.fmatch, st.combed, st.dfrm);
  }
  else if (mode == 6) // p/c + u + n + b
  {
    try2 = try1 == 2 ? 3 : 4;
    try3 = try1 == 2 ? 0 : 2;
    try4 = try2 == 3 ? 4 : 3;
    minm = std::min(st.mics[1], st.mics[try1]);
    if (st.mics[try2] * 3 < minm && st.mics[try2] < MI && abs(st.mics[try2] - minm) >= 30 && st.fmatch != try2 &&
      st.fmatch != try3 && st.fmatch != try4)
    {
      micChange(st.n, st.fmatch, try2, st.dst, st.prv, st.src, st.nxt,
        st.fmatch, st.combed, st.dfrm);
      minm = st.mics[try2];
    }
    else if (st.fmatch == try2) minm = std::min(st.mics[try2], minm);
    if (st.mics[try3] * 3 < minm && st.mics[try3] < MI && abs(st.mics[try3] - minm) >= 30 && st.fmatch != try3 &&
      st.fmatch != try4)
    {
      micChange(st.n, st.fmatch, try3, st.dst, st.prv, st.src, st.nxt,
        st.fmatch, st.combed, st.dfrm);
      minm = st.mics[try3];
    }
    else if (st.fmatch == try3) minm = std::min(st.mics[try3], minm);
    if (st.mics[try4] * 3 < minm && st.mics[try4] < MI && abs(st.mics[try4] - minm) >= 30 && st.fmatch != try4)
      micChange(st.n, st.fmatch, try4, st.dst, st.prv, st.src, st.nxt,
        st.fmatch, st.combed, st.dfrm);
  }
}

// Fill in any missing mic values, then let micmatching second-guess the match the search picked.
void TFM::applyMicMatching(FrameMatchState &st)
{
  if (!(micout > 0 || (micmatching > 0 && st.mics[st.fmatch] > 15 && mode != 7 &&
    !(micmatching == 2 && (mode == 0 || mode == 4)) &&
    (!mmsco || checkSceneChange(st.prv, st.src, st.nxt, st.n)))))
    return;

  fillMissingMics(st, micmatching > 0);

  if (!(micmatching > 0 && mode != 7 && st.mics[st.fmatch] > 15 &&
    (!mmsco || checkSceneChange(st.prv, st.src, st.nxt, st.n))))
    return;

  // insertion sort the five mics ascending, carrying their match codes along
  int order1[5], order2[5] = { 0, 1, 2, 3, 4 };
  for (int i = 0; i < 5; ++i) order1[i] = st.mics[i];
  for (int i = 1; i < 5; ++i)
  {
    int j = i;
    const int temp1 = order1[j];
    const int temp2 = order2[j];
    while (j > 0 && order1[j - 1] > temp1)
    {
      order1[j] = order1[j - 1];
      order2[j] = order2[j - 1];
      --j;
    }
    order1[j] = temp1;
    order2[j] = temp2;
  }

  // 3 is "by mode, then best overall"; the sort above is deliberately not redone in between.
  if (micmatching == 1) micMatchBestOverall(st, order1, order2);
  else if (micmatching == 2 || micmatching == 3)
  {
    micMatchByMode(st);
    if (micmatching == 3) micMatchBestOverall(st, order1, order2);
  }
}

const VSFrame *TFM::GetFrame(int n, int activationReason, VSFrameContext *frameCtx, VSCore *core)
{
  if (n < 0) n = 0;
  else if (n > nfrms) n = nfrms;

  if (activationReason == arInitial) {
      vsapi->requestFrameFilter(std::max(0, n - 1), child, frameCtx);
      vsapi->requestFrameFilter(n, child, frameCtx);
      vsapi->requestFrameFilter(std::min(n + 1, nfrms), child, frameCtx);
      return nullptr;
  } else if (activationReason != arAllFramesReady) {
      return nullptr;
  }

  FrameMatchState st;
  st.n = n;
  st.prv = vsapi->getFrameFilter(std::max(0, n - 1), child, frameCtx);
  st.src = vsapi->getFrameFilter(n, child, frameCtx);
  st.nxt = vsapi->getFrameFilter(std::min(n + 1, nfrms), child, frameCtx);

  order = order_origSaved;
  mode = mode_origSaved;
  field = field_origSaved;
  PP = PP_origSaved;
  MI = MI_origSaved;
  getSettingOvr(n); // process overrides

  if (!resolveFieldOrder(st, frameCtx))
  {
    vsapi->freeFrame(st.prv);
    vsapi->freeFrame(st.src);
    vsapi->freeFrame(st.nxt);
    return nullptr;
  }

  st.dst = vsapi->newVideoFrame(&vi->format, vi->width, vi->height, st.src, core);
  st.tmp = vsapi->newVideoFrame(&vi->format, vi->width, vi->height, nullptr, core);

  if (debug) logInfo(vsapi, vscore, "TFM:  ----------------------------------------");

  // An ovr/input file may name the match outright, in which case there is nothing to search for.
  // The one exception is a d2v-derived match that turns out to comb: that gets discarded and the
  // normal search runs instead.
  if (getMatchOvr(n, st.fmatch, st.combed, st.d2vmatch,
    flags == 5 ? checkSceneChange(st.prv, st.src, st.nxt, n) : false))
  {
    createWeaveFrame(st.dst, st.prv, st.src, st.nxt, st.fmatch, st.dfrm);
    bool useOvr = true;
    if (PP > 0 && st.combed == -1)
    {
      if (checkCombed(st.dst, n, st.fmatch, st.blockN, st.xblocks, st.mics, false))
      {
        if (st.d2vmatch)
        {
          st.d2vmatch = false;
          for (int j = 0; j < 5; ++j)
            st.mics[j] = -20;
          useOvr = false;
        }
        else st.combed = 2;
      }
      else st.combed = 0;
    }
    if (useOvr)
    {
      const bool d2vfilm = d2vduplicate(st.fmatch, st.combed, n);
      if (micout > 0) fillMissingMics(st, false);
      return finishFrame(st, d2vfilm, true);
    }
  }

  if (mode == 6) matchMode6(st);
  else if (mode == 7) matchMode7(st);
  else if (!matchModeNormal(st, frameCtx))
  {
    vsapi->freeFrame(st.prv);
    vsapi->freeFrame(st.src);
    vsapi->freeFrame(st.nxt);
    vsapi->freeFrame(st.dst);
    vsapi->freeFrame(st.tmp);
    return nullptr;
  }

  applyMicMatching(st);

  const bool d2vfilm = d2vduplicate(st.fmatch, st.combed, n);
  return finishFrame(st, d2vfilm, false);
}

void TFM::checkmm(int &cmatch, int m1, int m2, VSFrame *dst, int &dfrm, VSFrame *tmp, int &tfrm,
  const VSFrame *prv, const VSFrame *src, const VSFrame *nxt, int n,
  MicArray &blockN, int &xblocks, MicArray &mics)
{
  if (cmatch != m1)
  {
    int tx = m1;
    m1 = m2;
    m2 = tx;
  }
  if (dfrm == m1)
    checkCombed(dst, n, m1, blockN, xblocks, mics, false);
  else if (tfrm == m1)
    checkCombed(tmp, n, m1, blockN, xblocks, mics, false);
  else
  {
    if (tfrm != m2)
    {
      createWeaveFrame(tmp, prv, src, nxt, m1, tfrm);
      checkCombed(tmp, n, m1, blockN, xblocks, mics, false);
    }
    else
    {
      createWeaveFrame(dst, prv, src, nxt, m1, dfrm);
      checkCombed(dst, n, m1, blockN, xblocks, mics, false);
    }
  }
  if (mics[m1] < 30)
    return;
  if (dfrm == m2)
    checkCombed(dst, n, m2, blockN, xblocks, mics, false);
  else if (tfrm == m2)
    checkCombed(tmp, n, m2, blockN, xblocks, mics, false);
  else
  {
    if (tfrm != m1)
    {
      createWeaveFrame(tmp, prv, src, nxt, m2, tfrm);
      checkCombed(tmp, n, m2, blockN, xblocks, mics, false);
    }
    else
    {
      createWeaveFrame(dst, prv, src, nxt, m2, dfrm);
      checkCombed(dst, n, m2, blockN, xblocks, mics, false);
    }
  }
  if ((mics[m2] * 3 < mics[m1] || (mics[m2] * 2 < mics[m1] && mics[m1] > MI)) &&
    abs(mics[m2] - mics[m1]) >= 30 && mics[m2] < MI)
  {
    if (debug)
      logInfo(vsapi, vscore, "TFM:  frame {}  - micmatching override:  {} ({}) to {} ({})", n,
        matchChar(m1), mics[m1], matchChar(m2), mics[m2]);
    cmatch = m2;
  }
}

void TFM::micChange(int n, int m1, int m2, VSFrame *dst, const VSFrame *prv,
  const VSFrame *src, const VSFrame *nxt, int &fmatch,
  int &combed, int &cfrm) const
{
  if (debug)
    logInfo(vsapi, vscore, "TFM:  frame {}  - micmatching override:  {} to {}", n, matchChar(m1), matchChar(m2));
  fmatch = m2;
  combed = 0;
  createWeaveFrame(dst, prv, src, nxt, m2, cfrm);
}

void TFM::writeDisplay(VSFrame *dst, int n, int fmatch, int combed, bool over,
  [[maybe_unused]] int blockN, [[maybe_unused]] int xblocks, bool d2vmatch, const MicArray &mics, const VSFrame *prv,
  const VSFrame *src, const VSFrame *nxt)
{
    // Doesn't actually display anything, just sets a frame property which text.Text will display.


  if (combed > 1 && PP > 1) return; // TFMPP will display things instead

  /// TODO: draw the box
  std::string text = "TFM " VERSION " by tritical\n";

  if (PP > 0)
    text += std::format("order = {}  field = {}  mode = {}  MI = {}\n", order, field, mode, MI);
  else
    text += std::format("order = {}  field = {}  mode = {}\n", order, field, mode);

  if (!over && !d2vmatch)
    text += std::format("frame: {}  match = {} {}\n", n, matchChar(fmatch),
      ((ubsco || mmsco || flags == 5) && checkSceneChange(prv, src, nxt, n)) ? " (SC) " : "");
  else if (d2vmatch)
    text += std::format("frame: {}  match = {} (D2V) {}\n", n, matchChar(fmatch),
      ((ubsco || mmsco || flags == 5) && checkSceneChange(prv, src, nxt, n)) ? " (SC) " : "");
  else
    text += std::format("frame: {}  match = {} (OVR) {}\n", n, matchChar(fmatch),
      ((ubsco || mmsco || flags == 5) && checkSceneChange(prv, src, nxt, n)) ? " (SC) " : "");

  if (micout > 0 || (micmatching > 0 && mics[0] != -20 && mics[1] != -20 && mics[2] != -20
    && mics[3] != -20 && mics[4] != -20))
  {
    if (micout == 1 && mics[0] != -20 && mics[1] != -20 && mics[2] != -20 && micmatching == 0)
    {
      text += std::format("MICS:  p = {}  c = {}  n = {}\n", mics[0], mics[1], mics[2]);
    }
    else if ((micout == 2 && mics[0] != -20 && mics[1] != -20 && mics[2] != -20 &&
      mics[3] != -20 && mics[4] != -20) || micmatching > 0)
    {
      text += std::format("MICS:  p = {}  c = {}  n = {}\n", mics[0], mics[1], mics[2]);
      text += std::format("       b = {}  u = {}\n", mics[3], mics[4]);
    }
  }

  if (combed != -1)
  {
    if (combed == 1) text += std::format("PP = {}  CLEAN FRAME (forced!) ", PP);
    else if (combed == 5) text += std::format("PP = {}  COMBED FRAME  (forced!) ", PP);
    else if (combed == 0) text += std::format("PP = {}  CLEAN FRAME ", PP);
    else text += std::format("PP = {}  COMBED FRAME ", PP);
    if (mics[fmatch] >= 0)
    {
      text += " MIC = ";
      text += std::to_string(mics[fmatch]);
      text += ' ';
    }
    text += "\n";
  }

  if (d2vpercent >= 0.0)
  {
    text += std::format("{:3.1f}{} FILM (D2V)\n", d2vpercent, "%");
  }

  VSMap *props = vsapi->getFramePropertiesRW(dst);
  vsapi->mapSetData(props, PROP_TFMDisplay, text.c_str(), (int)text.size(), dtUtf8, maReplace);
}

// override from ovr file
// Validate an ovr settings override and append it to setArray as the 4-tuple
// {specifier, first frame, last frame, value}. 'f' field and 'o' order are tri-state, 'm' mode
// and 'P' PP are 0..7, and 'i' MI takes any value. Both the single-frame and the frame-range
// forms of the ovr syntax end up here; they differ only in whether last == first.
void TFM::appendSetting(int specifier, int first, int last, int value, int &i)
{
  switch (specifier)
  {
  case 'f':
    if (value != 0 && value != 1 && value != -1)
    {
      throw TIVTCError("TFM:  ovr input error (bad field value)!");
    }
    break;
  case 'o':
    if (value != 0 && value != 1 && value != -1)
    {
      throw TIVTCError("TFM:  ovr input error (bad order value)!");
    }
    break;
  case 'm':
    if (value < 0 || value > 7)
    {
      throw TIVTCError("TFM:  ovr input error (bad mode value)!");
    }
    break;
  case 'P':
    if (value < 0 || value > 7)
    {
      throw TIVTCError("TFM:  ovr input error (bad PP value)!");
    }
    break;
  }
  setArray[i] = specifier; ++i;
  setArray[i] = first; ++i;
  setArray[i] = last; ++i;
  setArray[i] = value; ++i;
}

// The ovr file can override `mode` and `PP` per frame, but two things are settled once, at filter
// creation: whether a TFMPP node is added to the graph (PP > 1), and whether this filter asks for
// the serialised delivery that mode 7's carried field choice relies on. An override that needs
// either cannot retroactively obtain it, so report it instead of quietly behaving differently.
void TFM::warnOvrOverrides() const
{
  if (setArray.size() == 0) return;

  int modeSevenAt = -1, ppRaiseAt = -1, ppRaiseTo = 0;
  for (int x = 0; x < (int)setArray.size(); x += 4)
  {
    const int spec = setArray[x], firstFrame = setArray[x + 1], value = setArray[x + 3];
    if (spec == 'm' && value == 7 && !linearAccess && modeSevenAt < 0)
      modeSevenAt = firstFrame;
    if (spec == 'P' && value > 1 && PP_origSaved <= 1 && ppRaiseAt < 0)
    {
      ppRaiseAt = firstFrame;
      ppRaiseTo = value;
    }
  }

  if (modeSevenAt >= 0)
    logWarning(vsapi, vscore, "TFM:  ovr file selects mode 7 (first at frame {}) but the filter was "
      "created with mode={}, so it did not request the serialised frame delivery mode 7 needs. "
      "Mode 7 carries its field choice over from the previously produced frame, so the output will "
      "depend on the order frames happen to be requested in. Pass mode=7 to TFM instead.",
      modeSevenAt, mode_origSaved);

  if (ppRaiseAt >= 0)
    logWarning(vsapi, vscore, "TFM:  ovr file raises PP to {} (first at frame {}) but the filter was "
      "created with PP={}, so no post-processing filter was added to the graph and the override "
      "cannot take effect. Pass PP={} (or higher) to TFM instead.",
      ppRaiseTo, ppRaiseAt, PP_origSaved, ppRaiseTo);
}

void TFM::getSettingOvr(int n)
{
  if (setArray.size() == 0) return;
  for (int x = 0; x < (int)setArray.size(); x += 4)
  {
    if (n >= setArray[x + 1] && n <= setArray[x + 2])
    {
      if (setArray[x] == 'o') order = setArray[x + 3]; // o
      else if (setArray[x] == 'm') mode = setArray[x + 3]; // m
      else if (setArray[x] == 'f') field = setArray[x + 3]; // f
      else if (setArray[x] == 'P') PP = setArray[x + 3]; // P
      else if (setArray[x] == 'i') MI = setArray[x + 3]; // i
    }
  }
}

bool TFM::getMatchOvr(int n, int &match, int &combed, bool &d2vmatch, bool isSC)
{
  bool combedset = false;
  d2vmatch = false;
  if (ovrArray.size() && ovrArray[n] != 255)
  {
    int value = ovrArray[n], temp;
    temp = value & 0x00000020;
    if (temp == 0 && PP > 0)
    {
      if (value & 0x00000010) combed = 5;
      else combed = 1;
      combedset = true;
    }
    temp = value & 0x00000007;
    if (temp >= 0 && temp <= 6)
    {
      match = temp;
      if (field != fieldO)
      {
        match = flipMatchFieldOrder(match);
      }
      if (match == 5) { combed = 5; match = 1; field = 0; }
      else if (match == 6) { combed = 5; match = 1; field = 1; }
      return true;
    }
  }
  if (flags != 0 && flags != 3 && d2vfilmarray.size() && (d2vfilmarray[n] & D2VARRAY_MATCH_MASK))
  {
    int ct = (flags == 4 || (flags == 5 && isSC)) ? -1 : 0;
    int temp = d2vfilmarray[n];
    if ((flags == 1 || flags == 4 || flags == 5) && !(temp&(0x1 << 6))) return false;
    temp = (temp&D2VARRAY_MATCH_MASK) >> 2;
    if (temp != 1 && temp != 2) return false;
    if (temp == 1) { match = 1; combed = combedset ? combed : ct; }
    else if (temp == 2) { match = field^order ? 2 : 0; combed = combedset ? combed : ct; }
    d2vmatch = true;
    return true;
  }
  return false;
}

bool TFM::d2vduplicate(int match, int combed, int n)
{
  if (d2vfilmarray.size() == 0 || d2vfilmarray[n] == 0) return false;
  // This decision depends on the previous frame's match, so it is only meaningful when frames
  // arrive in order. Without that guarantee, deliberately fall back to "not a duplicate"
  // instead of letting request scheduling decide the answer.
  if (!linearAccess || n - 1 != lastMatch.frame)
    lastMatch.field = lastMatch.frame = lastMatch.combed = lastMatch.match = -20;
  if ((d2vfilmarray[n] & D2VARRAY_DUP_MASK) == 0x3) // indicates possible top field duplicate
  {
    if (lastMatch.field == 1)
    {
      if ((lastMatch.combed > 1 || lastMatch.match != 3) && field == 1 &&
        (match != 4 || combed > 1)) return true;
      else if ((lastMatch.combed > 1 || lastMatch.match != 3) && field == 0 &&
        combed < 2 && match != 2) return true;
    }
    else if (lastMatch.field == 0)
    {
      if (lastMatch.combed < 2 && lastMatch.match != 0 && field == 1 &&
        (match != 4 || combed > 1)) return true;
      else if (lastMatch.combed < 2 && lastMatch.match != 0 && field == 0 &&
        combed < 2 && match != 2) return true;
    }
  }
  else if ((d2vfilmarray[n] & D2VARRAY_DUP_MASK) == 0x1) // indicates possible bottom field duplicate
  {
    if (lastMatch.field == 1)
    {
      if (lastMatch.combed < 2 && lastMatch.match != 0 && field == 0 &&
        (match != 4 || combed > 1)) return true;
      else if (lastMatch.combed < 2 && lastMatch.match != 0 && field == 1 &&
        combed < 2 && match != 2) return true;
    }
    else if (lastMatch.field == 0)
    {
      if ((lastMatch.combed > 1 || lastMatch.match != 3) && field == 0 &&
        (match != 4 || combed > 1)) return true;
      else if ((lastMatch.combed > 1 || lastMatch.match != 3) && field == 1 &&
        combed < 2 && match != 2) return true;
    }
  }
  return false;
}

void TFM::fileOut(int match, int combed, bool d2vfilm, int n, int MICount, const MicArray &mics)
{
  if (moutArray.size() && MICount != -1) moutArray[n] = MICount;
  if (micout > 0 && moutArrayE.size())
  {
    int sn = micout == 1 ? 3 : 5;
    for (int i = 0; i < sn; ++i)
      moutArrayE[n*sn + i] = mics[i];
  }
  if (outArray.size() == 0) return;
  if (output.size() || outputC.size())
  {
    if (field != fieldO)
    {
      match = flipMatchFieldOrder(match);
    }
    if (match == 1 && combed > 1 && field == 0) match = 5;
    else if (match == 1 && combed > 1 && field == 1) match = 6;
    unsigned char hint = 0;
    hint |= match;
    if (combed > 1) hint |= FILE_COMBED;
    else if (combed >= 0) hint |= FILE_NOTCOMBED;
    if (d2vfilm) hint |= FILE_D2V;
    hint |= FILE_ENTRY;
    outArray[n] = hint;
  }
}


bool TFM::checkCombed(const VSFrame *src, int n, int match,
  MicArray &blockN, int &xblocksi, MicArray &mics, bool ddebug)
{
    return checkCombedPlanar(src, n, match, blockN, xblocksi, mics, ddebug, vi->format.numPlanes > 1 && chroma);
}

// One plane's worth of resolved pointers for a compareFields pass: the two match fields, the
// current field, their immediate neighbours (and, for the slow 2 variant, the ones two rows out),
// the diff map rows, and the band-exclusion limits.
// A match code names one field of one frame:
//   0 (p) prev, 1 (c) current, 2 (n) next  -- the field with the same parity as `field`
//   3 (b) prev, 4 (u) next                 -- the opposite parity
// Both match1 and match2 use this same mapping, so the three compareFields variants all share it.
template<typename pixel_t>
static void selectMatchField(int match, int field,
  const pixel_t *prvp, const pixel_t *srcp, const pixel_t *nxtp,
  ptrdiff_t prv_pitch, ptrdiff_t src_pitch, ptrdiff_t nxt_pitch,
  const pixel_t *&fieldp, ptrdiff_t &field_pitch)
{
  const pixel_t *base;
  ptrdiff_t pitch;
  switch (match)
  {
  case 0:  base = prvp; pitch = prv_pitch; break;
  case 1:  base = srcp; pitch = src_pitch; break;
  case 2:  base = nxtp; pitch = nxt_pitch; break;
  case 3:  base = prvp; pitch = prv_pitch; break;
  default: base = nxtp; pitch = nxt_pitch; break; // match == 4; callers only pass 0..4
  }
  const int row = match < 3 ? (field == 1 ? 1 : 2) : (field == 1 ? 2 : 1);
  fieldp = base + row * pitch;
  field_pitch = pitch << 1;
}

// match1 additionally decides which parity the woven frame and the diff map start on.
static inline int curfRow(int match1, int field) { return match1 < 3 ? 3 - field : 2 + field; }
static inline int mapRow(int match1, int field)
{
  return match1 < 3 ? (field == 1 ? 1 : 2) : (field == 1 ? 2 : 1);
}

template<typename pixel_t>
struct TFM::MatchPlane
{
  const pixel_t *prvppf, *prvpf, *prvnf, *prvnnf;
  const pixel_t *curpf, *curf, *curnf;
  const pixel_t *nxtppf, *nxtpf, *nxtnf, *nxtnnf;
  ptrdiff_t prvf_pitch, curf_pitch, nxtf_pitch;
  uint8_t *mapp, *mapn;
  ptrdiff_t map_pitch;
  int Width, Height, startx, stopx;
  int y0a, y1a;
  bool noBandExclusion;
  ptrdiff_t tpitch_current;
};

// Resolve one plane for a compareFields pass. clearMap is set by the two slow variants, which
// build their diff map incrementally and need it zeroed first.
template<typename pixel_t>
void TFM::setupMatchPlane(const VSFrame *prv, const VSFrame *src, const VSFrame *nxt,
  int plane, int match1, int match2, bool clearMap, MatchPlane<pixel_t> &m)
{
  m.mapp = vsapi->getWritePtr(map.get(), plane);
  m.map_pitch = vsapi->getStride(map.get(), plane);

  const pixel_t *prvp = reinterpret_cast<const pixel_t*>(vsapi->getReadPtr(prv, plane));
  const ptrdiff_t prv_pitch = vsapi->getStride(prv, plane) / sizeof(pixel_t);
  const pixel_t *srcp = reinterpret_cast<const pixel_t*>(vsapi->getReadPtr(src, plane));
  const ptrdiff_t src_pitch = vsapi->getStride(src, plane) / sizeof(pixel_t);
  const pixel_t *nxtp = reinterpret_cast<const pixel_t*>(vsapi->getReadPtr(nxt, plane));
  const ptrdiff_t nxt_pitch = vsapi->getStride(nxt, plane) / sizeof(pixel_t);

  m.Width = vsapi->getFrameWidth(src, plane);
  m.Height = vsapi->getFrameHeight(src, plane);

  if (clearMap)
    memset(m.mapp, 0, m.Height * m.map_pitch);

  m.startx = 8 >> (plane ? vi->format.subSamplingW : 0);
  m.stopx = m.Width - m.startx;
  m.curf_pitch = src_pitch << 1;

  // exclusion area limits from parameters
  if (plane == 0)
  {
    m.y0a = y0;
    m.y1a = y1;
    m.tpitch_current = tpitchy;
  }
  else
  {
    const int ysubsampling = vi->format.subSamplingH;
    m.y0a = y0 >> ysubsampling;
    m.y1a = y1 >> ysubsampling;
    m.tpitch_current = tpitchuv;
  }
  m.noBandExclusion = (m.y0a == m.y1a);
  if (m.y0a >= 2) m.y0a = m.y0a - 2; // v18: real limit, since y goes only till Height-2
  if (m.y1a <= m.Height - 2) m.y1a = m.y1a + 2; // v18: real limit, since y goes only from 2

  m.curf = srcp + curfRow(match1, field) * src_pitch;
  m.mapp = m.mapp + mapRow(match1, field) * m.map_pitch;
  selectMatchField(match1, field, prvp, srcp, nxtp, prv_pitch, src_pitch, nxt_pitch,
    m.prvpf, m.prvf_pitch);
  selectMatchField(match2, field, prvp, srcp, nxtp, prv_pitch, src_pitch, nxt_pitch,
    m.nxtpf, m.nxtf_pitch);

  m.prvppf = m.prvpf - m.prvf_pitch;
  m.prvnf = m.prvpf + m.prvf_pitch;
  m.prvnnf = m.prvnf + m.prvf_pitch;
  m.curpf = m.curf - m.curf_pitch;
  m.curnf = m.curf + m.curf_pitch;
  m.nxtppf = m.nxtpf - m.nxtf_pitch;
  m.nxtnf = m.nxtpf + m.nxtf_pitch;
  m.nxtnnf = m.nxtnf + m.nxtf_pitch;

  m.map_pitch <<= 1;
  m.mapn = m.mapp + m.map_pitch;
}

int TFM::compareFields(const VSFrame *prv, const VSFrame *src, const VSFrame *nxt, int match1,
  int match2, int& norm1, int& norm2, int& mtn1, int& mtn2, int n)
{
  if (vi->format.bytesPerSample == 1)
    return compareFields_core<uint8_t>(prv, src, nxt, match1, match2, norm1, norm2, mtn1, mtn2, n);
  else
    return compareFields_core<uint16_t>(prv, src, nxt, match1, match2, norm1, norm2, mtn1, mtn2, n);
}


// Once the accumulators are in, the three compareFields variants pick the winner the same way.
// The "mtn" ladder: if either motion metric reaches `minimum` and the two differ by more than
// num:den, the smaller one wins. The variants use progressively longer prefixes of one ladder --
// slow=2 starts lowest and so is the most willing to trust the motion metrics over the plain ones.
namespace {
struct MtnRung { int minimum, num, den; };
const MtnRung kMtnLadder[] = {
  {  250, 4, 1 }, // slow=2 starts here
  {  375, 3, 1 }, // slow=1 starts here
  {  500, 2, 1 }, // plain compareFields starts here
  { 1000, 3, 2 },
  { 2000, 5, 4 },
};
constexpr int kMtnLadderSize = int(sizeof(kMtnLadder) / sizeof(kMtnLadder[0]));
} // namespace

// firstRung selects the variant: 2 = compareFields, 1 = slow 1, 0 = slow 2.
int TFM::decideMatch(int match1, int match2, uint64_t accumPc, uint64_t accumNc,
  uint64_t accumPm, uint64_t accumNm, int firstRung, int bits_per_pixel,
  int &norm1, int &norm2, int &mtn1, int &mtn2, int n) const
{
  // High bit depth: scale back to the 8 bit range rather than widening every threshold.
  const double factor = 1.0 / (1 << (bits_per_pixel - 8));

  norm1 = (int)((accumPc / 6.0 * factor) + 0.5);
  norm2 = (int)((accumNc / 6.0 * factor) + 0.5);
  mtn1 = (int)((accumPm / 6.0 * factor) + 0.5);
  mtn2 = (int)((accumNm / 6.0 * factor) + 0.5);

  const float c1 = float(std::max(norm1, norm2)) / float(std::max(std::min(norm1, norm2), 1));
  const float c2 = float(std::max(mtn1, mtn2)) / float(std::max(std::min(mtn1, mtn2), 1));
  const float mr = float(std::max(mtn1, mtn2)) / float(std::max(std::max(norm1, norm2), 1));

  // TODO:  improve this decision about whether to use the mtn metrics or
  //        the normal metrics.  mtn metrics give better recognition of
  //        small areas ("mouths")... the hard part is telling when they
  //        are reliable enough to use.
  bool useMotion = false;
  for (int i = firstRung; i < kMtnLadderSize && !useMotion; ++i)
  {
    const MtnRung &r = kMtnLadder[i];
    useMotion = (mtn1 >= r.minimum || mtn2 >= r.minimum) &&
      (mtn1 * r.num < mtn2 * r.den || mtn2 * r.num < mtn1 * r.den);
  }
  if (!useMotion)
    useMotion = (mtn1 >= 4000 || mtn2 >= 4000) && c2 > c1;
  if (!useMotion)
    useMotion = mr > 0.005 && std::max(mtn1, mtn2) > 150 &&
      (mtn1 * 2 < mtn2 * 1 || mtn2 * 2 < mtn1 * 1);

  if (debug)
  {
    // firstRung doubles as the variant name: the slower variants start lower on the ladder
    const char *variant = firstRung == 1 ? "  (SLOW 1)" : firstRung == 0 ? "  (SLOW 2)" : "";
    logInfo(vsapi, vscore, "TFM:  frame {}  - comparing {} to {}{}", n, matchChar(match1), matchChar(match2), variant);
    logInfo(vsapi, vscore, "TFM:  frame {}  - nmatches:  {} vs {} ({:3.1f})  mmatches:  {} vs {} ({:3.1f})", n,
      norm1, norm2, c1, mtn1, mtn2, c2);
  }

  if (useMotion)
    return mtn1 > mtn2 ? match2 : match1;
  return norm1 > norm2 ? match2 : match1;
}


template<typename pixel_t>
int TFM::compareFields_core(const VSFrame *prv, const VSFrame *src, const VSFrame *nxt, int match1,
  int match2, int &norm1, int &norm2, int &mtn1, int &mtn2, [[maybe_unused]] int n)
{
  const int bits_per_pixel = vi->format.bitsPerSample;

  int ret;

  const int stop = vi->format.numPlanes == 1 || !mChroma ? 1 : 3;
  const int incl = 1;  // pixel increment (1 for planar)

  uint64_t accumPc = 0, accumNc = 0;
  uint64_t accumPm = 0, accumNm = 0;
  norm1 = norm2 = mtn1 = mtn2 = 0;


  for (int b = 0; b < stop; ++b)
  {
    const int plane = b;

    MatchPlane<pixel_t> m;
    setupMatchPlane<pixel_t>(prv, src, nxt, plane, match1, match2, false, m);

    const int Width = m.Width, Height = m.Height;
    const int startx = m.startx, stopx = m.stopx;
    const int y0a = m.y0a, y1a = m.y1a;
    const bool noBandExclusion = m.noBandExclusion;
    const ptrdiff_t prvf_pitch = m.prvf_pitch, curf_pitch = m.curf_pitch, nxtf_pitch = m.nxtf_pitch;
    ptrdiff_t map_pitch = m.map_pitch;
    const pixel_t *prvpf = m.prvpf;
    const pixel_t *curf = m.curf;
    const pixel_t *nxtpf = m.nxtpf;
    uint8_t *mapp = m.mapp;

    // back to byte pointers
    if ((match1 >= 3 && field == 1) || (match1 < 3 && field != 1))
      buildDiffMapPlane2<pixel_t>(
        reinterpret_cast<const uint8_t*>(prvpf - prvf_pitch),
        reinterpret_cast<const uint8_t*>(nxtpf - nxtf_pitch),
        mapp - map_pitch,
        prvf_pitch * sizeof(pixel_t),
        nxtf_pitch * sizeof(pixel_t),
        map_pitch, Height >> 1, Width, bits_per_pixel);
    else
      buildDiffMapPlane2<pixel_t>(
        reinterpret_cast<const uint8_t*>(prvpf),
        reinterpret_cast<const uint8_t*>(nxtpf),
        mapp,
        prvf_pitch * sizeof(pixel_t),
        nxtf_pitch * sizeof(pixel_t),
        map_pitch, Height >> 1, Width, bits_per_pixel);

    const int Const23 = 23 << (bits_per_pixel - 8);
    const int Const42 = 42 << (bits_per_pixel - 8);

    // TFM 874
    for (int y = 2; y < Height - 2; y += 2) {
      if ((y < y0a) || noBandExclusion || (y > y1a))  // exclusion area check
      {
        for (int x = startx; x < stopx; x += incl)
        {
          int eax = (mapp[x] << 2) + mapp[x + map_pitch];
          if ((eax & 0xFF) == 0)
            continue;

          int a_curr = curf[x - curf_pitch] + (curf[x] << 2) + curf[x + curf_pitch];
          int a_prev = 3 * (prvpf[x] + prvpf[x + prvf_pitch]);
          int diff_p_c = abs(a_prev - a_curr);
          if (diff_p_c > Const23) {
            accumPc += diff_p_c;
            if (diff_p_c > Const42 && ((eax & 10) != 0))
              accumPm += diff_p_c;
          }
          int a_next = 3 * (nxtpf[x] + nxtpf[x + nxtf_pitch]);
          int diff_n_c = abs(a_next - a_curr);
          if (diff_n_c > Const23) {
            accumNc += diff_n_c;
            if (diff_n_c > Const42 && ((eax & 10) != 0))
              accumNm += diff_n_c;
          }
        }
      } // if

      mapp += map_pitch;
      prvpf += prvf_pitch;
      curf += curf_pitch;
      nxtpf += nxtf_pitch;
    }

  }

  ret = decideMatch(match1, match2, accumPc, accumNc, accumPm, accumNm, 2,
    bits_per_pixel, norm1, norm2, mtn1, mtn2, n);
  return ret;
}

int TFM::compareFieldsSlow(const VSFrame *prv, const VSFrame *src, const VSFrame *nxt, int match1,
  int match2, int& norm1, int& norm2, int& mtn1, int& mtn2, int n)
{
  if (slow == 2) {
    if (vi->format.bytesPerSample == 1)
      return compareFieldsSlow2_core<uint8_t>(prv, src, nxt, match1, match2, norm1, norm2, mtn1, mtn2, n);
    else
      return compareFieldsSlow2_core<uint16_t>(prv, src, nxt, match1, match2, norm1, norm2, mtn1, mtn2, n);
  }
  if (vi->format.bytesPerSample == 1)
    return compareFieldsSlow_core<uint8_t>(prv, src, nxt, match1, match2, norm1, norm2, mtn1, mtn2, n);
  else
    return compareFieldsSlow_core<uint16_t>(prv, src, nxt, match1, match2, norm1, norm2, mtn1, mtn2, n);
}

template<typename pixel_t>
int TFM::compareFieldsSlow_core(const VSFrame *prv, const VSFrame *src, const VSFrame *nxt, int match1,
  int match2, int &norm1, int &norm2, int &mtn1, int &mtn2, [[maybe_unused]] int n)
{
  const int bits_per_pixel = vi->format.bitsPerSample;

  int ret;

  ptrdiff_t tpitch_current;

  const int stop = vi->format.numPlanes == 1 || !mChroma ? 1 : 3;
  const int incl = 1;  // pixel increment (1 for planar)

  uint64_t accumPc = 0, accumNc = 0;
  uint64_t accumPm = 0, accumNm = 0;
  uint64_t accumPml = 0, accumNml = 0; // plus compared to CompareFields
  norm1 = norm2 = mtn1 = mtn2 = 0;

  for (int b = 0; b < stop; ++b)
  {
    const int plane = b;

    MatchPlane<pixel_t> m;
    setupMatchPlane<pixel_t>(prv, src, nxt, plane, match1, match2, true, m);

    const int Width = m.Width, Height = m.Height;
    const int startx = m.startx, stopx = m.stopx;
    const int y0a = m.y0a, y1a = m.y1a;
    const bool noBandExclusion = m.noBandExclusion;
    const ptrdiff_t prvf_pitch = m.prvf_pitch, curf_pitch = m.curf_pitch, nxtf_pitch = m.nxtf_pitch;
    ptrdiff_t map_pitch = m.map_pitch;
    const pixel_t *prvpf = m.prvpf;
    const pixel_t *curf = m.curf;
    const pixel_t *nxtpf = m.nxtpf;
    uint8_t *mapp = m.mapp;
    tpitch_current = m.tpitch_current;

    // back to byte pointers
      if ((match1 >= 3 && field == 1) || (match1 < 3 && field != 1))
        buildDiffMapPlane_Planar<pixel_t>(
          reinterpret_cast<const uint8_t*>(prvpf),
          reinterpret_cast<const uint8_t*>(nxtpf),
          mapp, 
          prvf_pitch * sizeof(pixel_t),
          nxtf_pitch * sizeof(pixel_t),
          map_pitch, Height, Width, tpitch_current, bits_per_pixel);
      else
        buildDiffMapPlane_Planar<pixel_t>(
          reinterpret_cast<const uint8_t*>(prvpf + prvf_pitch),
          reinterpret_cast<const uint8_t*>(nxtpf + nxtf_pitch),
          mapp + map_pitch, 
          prvf_pitch * sizeof(pixel_t),
          nxtf_pitch * sizeof(pixel_t),
          map_pitch, Height, Width, tpitch_current, bits_per_pixel);

    const int Const23 = 23 << (bits_per_pixel - 8);
    const int Const42 = 42 << (bits_per_pixel - 8);

    // TFM 1144
    // almost the same as in compareFields and buildDiffMapPlane2
    for (int y = 2; y < Height - 2; y += 2) {
      if ((y < y0a) || noBandExclusion || (y > y1a)) // exclusion area check
      {
        for (int x = startx; x < stopx; x += incl)
        {
          // diff from prev asm block (at buildDiffMapPlane2): <<3 instead of <<2
          int eax = (mapp[x] << 3) + mapp[x + map_pitch];
          if ((eax & 0xFF) == 0)
            continue;

          int a_curr = curf[x - curf_pitch] + (curf[x] << 2) + curf[x + curf_pitch];
          int a_prev = 3 * (prvpf[x] + prvpf[x + prvf_pitch]);
          int diff_p_c = abs(a_prev - a_curr);
          if (diff_p_c > Const23) {
            if((eax & 9) != 0) // diff from previous similar asm block: condition
              accumPc += diff_p_c;
            if (diff_p_c > Const42) {
              if ((eax & 18) != 0) // diff: &18 instead of &10
                accumPm += diff_p_c;
              if ((eax & 36) != 0) // diff: new condition and accumulator
                accumPml += diff_p_c;
            }
          }
          int a_next = 3 * (nxtpf[x] + nxtpf[x + nxtf_pitch]);
          int diff_n_c = abs(a_next - a_curr);
          if (diff_n_c > Const23) {
            if ((eax & 9) != 0) // diff from previous similar asm block: condition
              accumNc += diff_n_c;
            if (diff_n_c > Const42) {
              if ((eax & 18) != 0) // diff: &18 instead of &10
                accumNm += diff_n_c;
              if ((eax & 36) != 0) // diff: &18 instead of &10
                accumNml += diff_n_c;
            }
          }
        }
      } // if

      mapp += map_pitch;
      prvpf += prvf_pitch;
      curf += curf_pitch;
      nxtpf += nxtf_pitch;
    }

  }

  const unsigned int Const500 = 500 << (bits_per_pixel - 8);
  if (accumPm < Const500 && accumNm < Const500 && (accumPml >= Const500 || accumNml >= Const500) &&
    std::max(accumPml, accumNml) > 3 * std::min(accumPml, accumNml))
  {
    accumPm = accumPml;
    accumNm = accumNml;
  }

  ret = decideMatch(match1, match2, accumPc, accumNc, accumPm, accumNm, 1,
    bits_per_pixel, norm1, norm2, mtn1, mtn2, n);
  return ret;
}

template<typename pixel_t>
int TFM::compareFieldsSlow2_core(const VSFrame *prv, const VSFrame *src, const VSFrame *nxt, int match1,
  int match2, int &norm1, int &norm2, int &mtn1, int &mtn2, [[maybe_unused]] int n)
{
  const int bits_per_pixel = vi->format.bitsPerSample;

  int ret;

  ptrdiff_t tpitch_current;

  const int stop = vi->format.numPlanes == 1 || !mChroma ? 1 : 3;
  int incl = 1;  // pixel increment (1 for planar)

  uint64_t accumPc = 0, accumNc = 0;
  uint64_t accumPm = 0, accumNm = 0;
  uint64_t accumPml = 0, accumNml = 0; // plus compared to CompareFields
  norm1 = norm2 = mtn1 = mtn2 = 0;
  
  for (int b = 0; b < stop; ++b)
  {
    const int plane = b;

    MatchPlane<pixel_t> m;
    setupMatchPlane<pixel_t>(prv, src, nxt, plane, match1, match2, true, m);

    const int Width = m.Width, Height = m.Height;
    const int startx = m.startx, stopx = m.stopx;
    const int y0a = m.y0a, y1a = m.y1a;
    const bool noBandExclusion = m.noBandExclusion;
    const ptrdiff_t prvf_pitch = m.prvf_pitch, curf_pitch = m.curf_pitch, nxtf_pitch = m.nxtf_pitch;
    ptrdiff_t map_pitch = m.map_pitch;
    const pixel_t *prvpf = m.prvpf;
    const pixel_t *curf = m.curf;
    const pixel_t *nxtpf = m.nxtpf;
    uint8_t *mapp = m.mapp;
    tpitch_current = m.tpitch_current;

    // back to byte pointers
      if ((match1 >= 3 && field == 1) || (match1 < 3 && field != 1))
        buildDiffMapPlane_Planar<pixel_t>(
          reinterpret_cast<const uint8_t*>(prvpf),
          reinterpret_cast<const uint8_t*>(nxtpf),
          mapp,
          prvf_pitch * sizeof(pixel_t),
          nxtf_pitch * sizeof(pixel_t),
          map_pitch, Height, Width, tpitch_current, bits_per_pixel);
      else
        buildDiffMapPlane_Planar<pixel_t>(
          reinterpret_cast<const uint8_t*>(prvpf + prvf_pitch),
          reinterpret_cast<const uint8_t*>(nxtpf + nxtf_pitch),
          mapp + map_pitch,
          prvf_pitch * sizeof(pixel_t),
          nxtf_pitch * sizeof(pixel_t),
          map_pitch, Height, Width, tpitch_current, bits_per_pixel);

    const int Const23 = 23 << (bits_per_pixel - 8);
    const int Const42 = 42 << (bits_per_pixel - 8);

    if (field == 0) {
    // TFM 1436
    // almost the same as in TFM 1144
      for (int y = 2; y < Height - 2; y += 2) {
        if ((y < y0a) || noBandExclusion || (y > y1a))
        {
          for (int x = startx; x < stopx; x += incl)
          {
            int eax = (mapp[x] << 3) + mapp[x + map_pitch]; // diff from prev asm block (at buildDiffMapPlane2): <<3 instead of <<2
            if ((eax & 0xFF) == 0)
              continue;

            int a_curr = curf[x - curf_pitch] + (curf[x] << 2) + curf[x + curf_pitch];
            int a_prev = 3 * (prvpf[x] + prvpf[x + prvf_pitch]);
            int diff_p_c = abs(a_prev - a_curr);
            if (diff_p_c > Const23) {
              if ((eax & 9) != 0) // diff from previous similar asm block: condition
                accumPc += diff_p_c;
              if (diff_p_c > Const42) {
                if ((eax & 18) != 0) // diff: &18 instead of &10
                  accumPm += diff_p_c;
                if ((eax & 36) != 0) // diff: new condition and accumulator
                  accumPml += diff_p_c;
              }
            }
            int a_next = 3 * (nxtpf[x] + nxtpf[x + nxtf_pitch]);
            int diff_n_c = abs(a_next - a_curr);
            if (diff_n_c > Const23) {
              if ((eax & 9) != 0) // diff from previous similar asm block: condition
                accumNc += diff_n_c;
              if (diff_n_c > Const42) {
                if ((eax & 18) != 0) // diff: &18 instead of &10
                  accumNm += diff_n_c;
                if ((eax & 36) != 0) // diff: &18 instead of &10
                  accumNml += diff_n_c;
              }
            }

            // additional difference from TFM 1144
            if ((eax & 56) != 0) {

              a_prev = prvpf[x - prvf_pitch] + (prvpf[x] << 2) + prvpf[x + prvf_pitch];
              a_curr = 3 * (curf[x - curf_pitch] + curf[x]);
              diff_p_c = abs(a_prev - a_curr);
              if (diff_p_c > Const23) {
                if ((eax & 8) != 0) // diff from previous similar asm block: condition
                  accumPc += diff_p_c;
                if (diff_p_c > Const42) {
                  if ((eax & 16) != 0) // diff: &16 instead of &18
                    accumPm += diff_p_c;
                  if ((eax & 32) != 0) // diff: new condition and accumulator
                    accumPml += diff_p_c;
                }
              }
              a_next = nxtpf[x - nxtf_pitch] + (nxtpf[x] << 2) + nxtpf[x + nxtf_pitch]; // really! not 3*
              diff_n_c = abs(a_next - a_curr);
              if (diff_n_c > Const23) {
                if ((eax & 8) != 0) // diff: &8 instead of &9
                  accumNc += diff_n_c;
                if (diff_n_c > Const42) {
                  if ((eax & 16) != 0) // diff: &16 instead of &18
                    accumNm += diff_n_c;
                  if ((eax & 32) != 0) // diff: &32 instead of &36
                    accumNml += diff_n_c;
                }
              }
            }
          }
        } // if

        mapp += map_pitch;
        prvpf += prvf_pitch;
        curf += curf_pitch;
        nxtpf += nxtf_pitch;

      }
    }
    else {
      // TFM 1633
      // almost the same as in TFM 1436 (field==0= case)
      // differences are after eax&56 block, see later

      for (int y = 2; y < Height - 2; y += 2) {
        if ((y < y0a) || noBandExclusion || (y > y1a))
        {
          for (int x = startx; x < stopx; x += incl)
          {
            int eax = (mapp[x] << 3) + mapp[x + map_pitch]; // diff from prev asm block (at buildDiffMapPlane2): <<3 instead of <<2
            if ((eax & 0xFF) == 0)
              continue;

            int a_curr = curf[x - curf_pitch] + (curf[x] << 2) + curf[x + curf_pitch];
            int a_prev = 3 * (prvpf[x] + prvpf[x + prvf_pitch]);
            int diff_p_c = abs(a_prev - a_curr);
            if (diff_p_c > Const23) {
              if ((eax & 9) != 0) // diff from previous similar asm block: condition
                accumPc += diff_p_c;
              if (diff_p_c > Const42) {
                if ((eax & 18) != 0) // diff: &18 instead of &10
                  accumPm += diff_p_c;
                if ((eax & 36) != 0) // diff: new condition and accumulator
                  accumPml += diff_p_c;
              }
            }
            int a_next = 3 * (nxtpf[x] + nxtpf[x + nxtf_pitch]); // L2008
            int diff_n_c = abs(a_next - a_curr);
            if (diff_n_c > Const23) {
              if ((eax & 9) != 0) // diff from previous similar asm block: condition
                accumNc += diff_n_c;
              if (diff_n_c > Const42) {
                if ((eax & 18) != 0) // diff: &18 instead of &10
                  accumNm += diff_n_c;
                if ((eax & 36) != 0) // diff: &18 instead of &10
                  accumNml += diff_n_c;
              }
            }

            // difference from TFM 1436
            // prvpf  -> prvnf
            // prvppf -> prvpf
            // prvnf  -> prvnnf
            // curpf  -> curf
            // curf   -> curnf
            // nxtpf  -> nxtnf
            // nxtppf -> nxtpf
            // nxtnf  -> nxtnnf
            // mask 8/16/32 -> 1/2/4
            if ((eax & 7) != 0) { // 1.0.12: diff: &7 instead of &56 L2036

              a_prev = prvpf[x] + (prvpf[x + prvf_pitch] << 2) + prvpf[x + 2 * prvf_pitch];
              a_curr = 3 * (curf[x] + curf[x + curf_pitch]);
              diff_p_c = abs(a_prev - a_curr);
              if (diff_p_c > Const23) {
                if ((eax & 1) != 0) // diff: &1 instead of &8
                  accumPc += diff_p_c;
                if (diff_p_c > Const42) {
                  if ((eax & 2) != 0) // diff: &2 instead of &16
                    accumPm += diff_p_c;
                  if ((eax & 4) != 0) // diff: &4 instead of &32
                    accumPml += diff_p_c;
                }
              }
              //int a_next = *(nxtppf + ebx) + (*(nxtpf + ebx) << 2) + *(nxtnf + ebx); // really! not 3*
              a_next = nxtpf[x] + (nxtpf[x + nxtf_pitch] << 2) + nxtpf[x + 2 * nxtf_pitch]; // really! not 3* L2075
              diff_n_c = abs(a_next - a_curr);
              if (diff_n_c > Const23) { // L2088
                if ((eax & 1) != 0) // diff: &1 instead of &8
                  accumNc += diff_n_c;
                if (diff_n_c > Const42) { // L2094
                  if ((eax & 2) != 0) // diff: &2 instead of &16 // 1.0.12 really 2
                    accumNm += diff_n_c;
                  if ((eax & 4) != 0) // diff: &4 instead of &32
                    accumNml += diff_n_c;
                }
              }
            }
          }
        } // if

        mapp += map_pitch;
        prvpf += prvf_pitch;
        curf += curf_pitch;
        nxtpf += nxtf_pitch;

        // not used prvppf += prvf_pitch;
        // not used nxtppf += nxtf_pitch;

      }

    }

  }

  const unsigned int Const500 = 500 << (bits_per_pixel - 8);
  if (accumPm < Const500 && accumNm < Const500 && (accumPml >= Const500 || accumNml >= Const500) &&
    std::max(accumPml, accumNml) > 3 * std::min(accumPml, accumNml))
  {
    accumPm = accumPml;
    accumNm = accumNml;
  }

  ret = decideMatch(match1, match2, accumPc, accumNc, accumPm, accumNm, 0,
    bits_per_pixel, norm1, norm2, mtn1, mtn2, n);
  return ret;
}

template<typename pixel_t>
static void checkSceneChangePlanar_1_c(const pixel_t* srcp, const pixel_t* nxtp,
  int height, int width, ptrdiff_t src_pitch, ptrdiff_t nxt_pitch, uint64_t& diff)
{
  for (int y = 0; y < height; ++y)
  {
    uint32_t rowdiff = 0;
    for (int x = 0; x < width; x += 4)
    {
      rowdiff += abs(srcp[x + 0] - nxtp[x + 0]);
      rowdiff += abs(srcp[x + 1] - nxtp[x + 1]);
      rowdiff += abs(srcp[x + 2] - nxtp[x + 2]);
      rowdiff += abs(srcp[x + 3] - nxtp[x + 3]);
    }
    diff += rowdiff;
    srcp += src_pitch;
    nxtp += nxt_pitch;
  }
}

template<typename pixel_t>
static void checkSceneChangePlanar_2_c(const pixel_t* prvp, const pixel_t* srcp,
  const pixel_t* nxtp, int height, int width, ptrdiff_t prv_pitch, ptrdiff_t src_pitch,
  ptrdiff_t nxt_pitch, uint64_t& diffp, uint64_t& diffn)
{
  for (int y = 0; y < height; ++y)
  {
    uint32_t rowdiffp = 0;
    uint32_t rowdiffn = 0;
    for (int x = 0; x < width; x += 4)
    {
      rowdiffp += abs(srcp[x + 0] - prvp[x + 0]);
      rowdiffp += abs(srcp[x + 1] - prvp[x + 1]);
      rowdiffp += abs(srcp[x + 2] - prvp[x + 2]);
      rowdiffp += abs(srcp[x + 3] - prvp[x + 3]);
      rowdiffn += abs(srcp[x + 0] - nxtp[x + 0]);
      rowdiffn += abs(srcp[x + 1] - nxtp[x + 1]);
      rowdiffn += abs(srcp[x + 2] - nxtp[x + 2]);
      rowdiffn += abs(srcp[x + 3] - nxtp[x + 3]);
    }
    diffp += rowdiffp;
    diffn += rowdiffn;
    prvp += prv_pitch;
    srcp += src_pitch;
    nxtp += nxt_pitch;
  }
}

bool TFM::checkSceneChange(const VSFrame *prv, const VSFrame *src, const VSFrame *nxt, int n)
{
  const int bits_per_pixel = vi->format.bitsPerSample;
  if (bits_per_pixel == 8)
    return checkSceneChange_core<uint8_t>(prv, src, nxt, n, bits_per_pixel);
  else
    return checkSceneChange_core<uint16_t>(prv, src, nxt, n, bits_per_pixel);
}

template<typename pixel_t>
bool TFM::checkSceneChange_core(const VSFrame *prv, const VSFrame *src, const VSFrame *nxt,
  int n, int bits_per_pixel)
{
  // Memoize the result for frame n only. The old cache also reused the previous call's diffn as
  // this call's diffp, which silently assumed the previously processed frame was n-1. Under
  // fmParallelRequests VapourSynth serializes execution but not ordering, so that made the
  // scene change verdict for a given frame depend on request scheduling.
  if (sclast.frame == n) return sclast.sc;
  uint64_t diffp = 0;
  uint64_t diffn = 0;
  const uint8_t *prvp = vsapi->getReadPtr(prv, 0);
  const uint8_t *srcp = vsapi->getReadPtr(src, 0);
  const uint8_t *nxtp = vsapi->getReadPtr(nxt, 0);
  const int height = vsapi->getFrameHeight(src, 0) >> 1;
  int width = vsapi->getFrameWidth(src, 0);
  // this mod16 must be the same as in computing "diffmaxsc"
  
  // safe mod16 rounding
    width = ((width >> 4) << 4); // mod16

  // every 2nd line
  ptrdiff_t prv_pitch = vsapi->getStride(prv, 0) << 1;
  ptrdiff_t src_pitch = vsapi->getStride(src, 0) << 1;
  ptrdiff_t nxt_pitch = vsapi->getStride(nxt, 0) << 1;
  prvp += (1 - field)*(prv_pitch >> 1);
  srcp += (1 - field)*(src_pitch >> 1);
  nxtp += (1 - field)*(nxt_pitch >> 1);


  checkSceneChangePlanar_2_c<pixel_t>(
    reinterpret_cast<const pixel_t*>(prvp),
    reinterpret_cast<const pixel_t*>(srcp),
    reinterpret_cast<const pixel_t*>(nxtp),
    height, width,
    prv_pitch / sizeof(pixel_t),
    src_pitch / sizeof(pixel_t),
    nxt_pitch / sizeof(pixel_t),
    diffp, diffn);

  // scale back to 8 bit world
  diffn >>= (bits_per_pixel - 8);
  diffp >>= (bits_per_pixel - 8);
  
  sclast.frame = n;
  sclast.sc = (diffp > diffmaxsc || diffn > diffmaxsc);
  if (debug)
    logInfo(vsapi, vscore, "TFM:  frame {}  - diffp = {}   diffn = {}"
      "  diffmaxsc = {}  {}", n, diffp, diffn, diffmaxsc, sclast.sc ? 'T' : 'F');
  return sclast.sc;
}

// Copy one field of `from` -- every other row starting at `parity` -- into the same rows of dst.
void TFM::weaveField(VSFrame *dst, const VSFrame *from, int plane, int parity) const
{
  const ptrdiff_t dst_pitch = vsapi->getStride(dst, plane);
  const ptrdiff_t src_pitch = vsapi->getStride(from, plane);
  vsh::bitblt(vsapi->getWritePtr(dst, plane) + parity * dst_pitch, dst_pitch << 1,
    vsapi->getReadPtr(from, plane) + parity * src_pitch, src_pitch << 1,
    vsapi->getFrameWidth(from, plane) * vi->format.bytesPerSample,
    vsapi->getFrameHeight(from, plane) >> 1);
}

void TFM::createWeaveFrame(VSFrame *dst, const VSFrame *prv, const VSFrame *src,
  const VSFrame *nxt, int match, int &cfrm) const
{
  if (cfrm == match)
    return;

  const int np = vi->format.numPlanes;
  for (int b = 0; b < np; ++b)
  {
    const int plane = b;
    // Apart from 'c', every match weaves one field of the current frame together with the
    // opposite field of a neighbour. p/n take the current frame's !field, b/u take its field.
    switch (match)
    {
    case 1: // c: the current frame untouched
      vsh::bitblt(vsapi->getWritePtr(dst, plane), vsapi->getStride(dst, plane),
        vsapi->getReadPtr(src, plane), vsapi->getStride(src, plane),
        vsapi->getFrameWidth(src, plane) * vi->format.bytesPerSample,
        vsapi->getFrameHeight(src, plane));
      break;
    case 0: // p: current + previous
      weaveField(dst, src, plane, 1 - field);
      weaveField(dst, prv, plane, field);
      break;
    case 2: // n: current + next
      weaveField(dst, src, plane, 1 - field);
      weaveField(dst, nxt, plane, field);
      break;
    case 3: // b: current + previous, opposite parity to p
      weaveField(dst, src, plane, field);
      weaveField(dst, prv, plane, 1 - field);
      break;
    default: // 4, u: current + next, opposite parity to n
      weaveField(dst, src, plane, field);
      weaveField(dst, nxt, plane, 1 - field);
      break;
    }
  }
  cfrm = match;
}

void TFM::putFrameProperties(VSFrame *dst, int match, int combed, bool d2vfilm, const MicArray &mics) const
{
    VSMap *props = vsapi->getFramePropertiesRW(dst);

    vsapi->mapSetInt(props, PROP_TFMMATCH, match, maReplace);
    vsapi->mapSetInt(props, PROP_Combed, combed > 1, maReplace);
    vsapi->mapSetInt(props, PROP_TFMD2VFilm, d2vfilm, maReplace);
    vsapi->mapSetInt(props, PROP_TFMField, field, maReplace);
    for (int i = 0; i < 5; i++)
        vsapi->mapSetInt(props, PROP_TFMMics, mics[i], i ? maAppend : maReplace);
    vsapi->mapSetInt(props, PROP_TFMPP, PP, maReplace);
}


// check in TDeint, plus don't call with aligned width!
template<typename pixel_t>
void TFM::buildDiffMapPlane2(const uint8_t *prvp, const uint8_t *nxtp,
  uint8_t *dstp, ptrdiff_t prv_pitch, ptrdiff_t nxt_pitch, ptrdiff_t dst_pitch, int Height,
  int Width, int bits_per_pixel) const
{
  do_buildABSDiffMask2<pixel_t>(prvp, nxtp, dstp, prv_pitch, nxt_pitch, dst_pitch, Width, Height, bits_per_pixel);
}

// instantiate
template void TFM::buildDiffMapPlane2<uint8_t>(const uint8_t* prvp, const uint8_t* nxtp,
  uint8_t* dstp, ptrdiff_t prv_pitch, ptrdiff_t nxt_pitch, ptrdiff_t dst_pitch, int Height,
  int Width, int bits_per_pixel) const;
template void TFM::buildDiffMapPlane2<uint16_t>(const uint8_t* prvp, const uint8_t* nxtp,
  uint8_t* dstp, ptrdiff_t prv_pitch, ptrdiff_t nxt_pitch, ptrdiff_t dst_pitch, int Height,
  int Width, int bits_per_pixel) const;

template<typename pixel_t>
void TFM::buildABSDiffMask(const uint8_t *prvp, const uint8_t *nxtp,
  ptrdiff_t prv_pitch, ptrdiff_t nxt_pitch, ptrdiff_t tpitch, int width, int height)
{
  do_buildABSDiffMask<pixel_t>(prvp, nxtp, tbuffer.data(), prv_pitch, nxt_pitch, tpitch, width, height);
}

// instantiate
template void TFM::buildABSDiffMask<uint8_t>(const uint8_t* prvp, const uint8_t* nxtp,
  ptrdiff_t prv_pitch, ptrdiff_t nxt_pitch, ptrdiff_t tpitch, int width, int height);
template void TFM::buildABSDiffMask<uint16_t>(const uint8_t* prvp, const uint8_t* nxtp,
  ptrdiff_t prv_pitch, ptrdiff_t nxt_pitch, ptrdiff_t tpitch, int width, int height);


// Reject parameter combinations the rest of the filter assumes cannot occur.
void TFM::validateParameters()
{
  if (!vsh::isConstantVideoFormat(vi))
      throw TIVTCError("TFM: the input clip must have constant format and dimensions.");

  if (vi->format.bitsPerSample > 16)
    throw TIVTCError("TFM:  only 8-16 bit formats supported!");
  if (vi->format.sampleType != stInteger)
      throw TIVTCError("TFM: only integer formats supported!");
  if (vi->format.colorFamily != cfYUV)
    throw TIVTCError("TFM:  YUV data only!");
  if (vi->format.subSamplingW > 1 || vi->format.subSamplingH > 1 ||
    vi->format.subSamplingH > vi->format.subSamplingW)
    throw TIVTCError("TFM:  only 4:4:4, 4:2:2 and 4:2:0 subsampling is supported!");
  if (vi->height & 1 || vi->width & 1)
    throw TIVTCError("TFM:  height and width must be divisible by 2!");
  if (vi->height < 6 || vi->width < 64)
    throw TIVTCError("TFM:  frame dimensions too small!");
  // The combing analyzer and the postprocessing deinterlacer read a couple of rows
  // above/below each line and derive (planeHeight/2 - 3) / (planeHeight - 4) loop counts;
  // for subsampled chroma the smallest plane is height >> subSamplingH, so a short frame
  // would underflow those counts into out-of-bounds accesses. Require at least 8 lines per plane.
  if ((vi->height >> vi->format.subSamplingH) < 8)
    throw TIVTCError("TFM:  frame height too small (each plane needs at least 8 lines)!");
  if (mode < 0 || mode > 7)
    throw TIVTCError("TFM:  mode must be set to 0, 1, 2, 3, 4, 5, 6, or 7!");
  if (field < -1 || field > 1)
    throw TIVTCError("TFM:  field must be set to -1, 0, or 1!");
  if (PP < 0 || PP > 7)
    throw TIVTCError("TFM:  PP must be at least 0 and less than 8!");
  if (order < -1 || order > 1)
    throw TIVTCError("TFM:  order must be set to -1, 0, or 1!");
  if (blockx != 4 && blockx != 8 && blockx != 16 && blockx != 32 && blockx != 64 &&
    blockx != 128 && blockx != 256 && blockx != 512 && blockx != 1024 && blockx != 2048)
    throw TIVTCError("TFM:  illegal blockx size!");
  if (blocky != 4 && blocky != 8 && blocky != 16 && blocky != 32 && blocky != 64 &&
    blocky != 128 && blocky != 256 && blocky != 512 && blocky != 1024 && blocky != 2048)
    throw TIVTCError("TFM:  illegal blocky size!");
  if (y0 != y1 && (y0 < 0 || y1 < 0 || y0 > y1 || y1 > vi->height || y0 > vi->height))
    throw TIVTCError("TFM:  bad y0 and y1 exclusion band values!");
  if (ovrDefault < 0 || ovrDefault > 2)
    throw TIVTCError("TFM:  ovrDefault must be set to 0, 1, or 2!");
  if (flags < 0 || flags > 5)
    throw TIVTCError("TFM:  flags must be set to 0, 1, 2, 3, 4, or 5!");
  if (slow < 0 || slow > 2)
    throw TIVTCError("TFM:  slow must be set to 0, 1, or 2!");
  if (micout < 0 || micout > 2)
    throw TIVTCError("TFM:  micout must be set to 0, 1, or 2!");
  // Only 1, 2 and 3 are implemented; 4 used to be accepted but silently did nothing.
  if (micmatching < 0 || micmatching > 3)
    throw TIVTCError("TFM:  micmatching must be set to 0, 1, 2, or 3!");
  if (opt < 0 || opt > 4)
    throw TIVTCError("TFM:  opt must be set to 0, 1, 2, 3, or 4!");
  if (metric != 0 && metric != 1)
    throw TIVTCError("TFM:  metric must be set to 0 or 1!");
  if (scthresh < 0.0 || scthresh > 100.0)
    throw TIVTCError("TFM:  scthresh must be between 0.0 and 100.0 (inclusive)!");
}

// Parse the input= file produced by a previous run's output=: per-frame match codes, combed
// flags and mic values, plus the crc32 header line that ties it to this source clip.
void TFM::parseInputFile()
{
  int z, q = 0, fieldt, firstLine, qt;
  char linein[1024];
  char *linep, *linet;
  std::unique_ptr<FILE, decltype (&fclose)> f(nullptr, nullptr);
  if (input.size())
  {
    bool d2vmarked, micmarked;
    if ((f = decltype (f)(tivtc_fopen(input.c_str(), "r"), &fclose)) != nullptr)
    {
      ovrArray.resize(vi->numFrames, 255);

      if (d2vfilmarray.size() == 0)
      {
        d2vfilmarray.resize(vi->numFrames + 1, 0);
      }
      fieldt = fieldO;
      if (debug)
        logInfo(vsapi, vscore, "TFM:  successfully opened input file.  Field defaulting to - {}.",
          fieldt == 0 ? "bottom" : "top");
      firstLine = 0;
      while (fgets(linein, 1024, f.get()) != nullptr)
      {
        if (isBlankOrCommentLine(linein))
          continue;
        ++firstLine;
        linep = linein;
        while (*linep != 'f' && *linep != 'F' && *linep != 0 && *linep != ' ' && *linep != 'c') linep++;
        if (*linep == 'f' || *linep == 'F')
        {
          if (firstLine == 1)
          {
            if (_strnicmp(linein, "field = top", 11) == 0) { fieldt = 1; }
            else if (_strnicmp(linein, "field = bottom", 14) == 0) { fieldt = 0; }
            if (debug)
              logInfo(vsapi, vscore, "TFM:  detected field for input file - {}.",
                fieldt == 0 ? "bottom" : "top");
          }
        }
        else if (*linep == 'c')
        {
          if (_strnicmp(linein, "crc32 = ", 8) == 0)
          {
            linet = linein;
            while (*linet != ' ') linet++;
            linet++;
            while (*linet != ' ') linet++;
            linet++;
            unsigned int m, tempCrc;
            if (sscanf(linet, "%x", &m) != 1)
              throw TIVTCError("TFM:  input file error (malformed crc32 line)!");
            calcCRC(child, 15, tempCrc, vsapi);
            if (tempCrc != m && !batch)
            {
              throw TIVTCError("TFM:  crc32 in input file does not match that of the current clip!");
            }
          }
        }
        else if (*linep == ' ')
        {
          linet = linein;
          while (*linet != 0)
          {
            if (*linet != ' ' && *linet != 10) break;
            linet++;
          }
          if (*linet == 0) { --firstLine; continue; }
          z = -1; // a failed parse must fail the range check below, not reuse a previous line's value
          sscanf(linein, "%d", &z);
          linep = linein;
          linep = skipToMatchChar(linep);
          if (*linep != 0)
          {
            if (z<0 || z>nfrms)
            {
              throw TIVTCError("TFM:  input file error (out of range or non-ascending frame #)!");
            }
            linep = linein;
            while (*linep != ' ' && *linep != 0) linep++;
            if (*linep != 0)
            {
              qt = -1;
              d2vmarked = micmarked = false;
              linep++;
              q = decodeMatchChar(*linep);
              if (q < 0)
              {
                throw TIVTCError("TFM:  input file error (invalid match specifier)!");
              }
              linep++;
              linep++;
              if (*linep != 0)
              {
                qt = decodeCombedChar(*linep);
                if (qt < 0)
                {
                  // not a combed specifier; the only other things that may follow are the
                  // d2v and mic annotations, both of which leave qt as "nothing recorded"
                  if (*linep == '1') d2vmarked = true;
                  else if (*linep == '[') micmarked = true;
                  else
                  {
                    throw TIVTCError("TFM:  input file error (invalid specifier)!");
                  }
                }
              }
              if (fieldt != fieldO)
              {
                q = flipMatchFieldOrder(q);
              }
              if (!d2vmarked && !micmarked && qt != -1)
              {
                linep++;
                linep++;
                if (*linep == '1') d2vmarked = true;
                else if (*linep == '[') micmarked = true;
              }
              if (d2vmarked)
              {
                d2vfilmarray[z] &= ~0x03;
                d2vfilmarray[z] |= fieldt == 1 ? 0x3 : 0x1;
                if (!micmarked)
                {
                  linep++;
                  linep++;
                  if (*linep == '[') micmarked = true;
                }
              }
              if (micmarked)
              {
                // add mic input handling in the future
              }
              ovrArray[z] |= 0x07;
              ovrArray[z] &= (q | 0xF8);
              if (qt != -1)
              {
                ovrArray[z] &= 0xDF;
                ovrArray[z] |= 0x10;
                ovrArray[z] &= (qt | 0xEF);
              }
            }
          }
        }
      }
    }
    else throw TIVTCError("TFM:  input file error (could not open file)!");
  }
}

// Parse the ovr= override file: two passes, one to count the entries so the arrays can be
// sized and one to fill them. Returns early when the file turned out to hold no usable
// entries (this was a `goto emptyovr` out of the middle of the constructor).
void TFM::parseOvrFile()
{
  if (!ovr.size()) return;

  int z, w, q = 0, b, i, count, last, fieldt, firstLine;
  int countOvrS, countOvrM;
  char linein[1024];
  char *linep, *linet;
  std::unique_ptr<FILE, decltype (&fclose)> f(nullptr, nullptr);
  {
    if ((f = decltype (f)(tivtc_fopen(ovr.c_str(), "r"), &fclose)) != nullptr)
    {
      countOvrS = countOvrM = 0;
      while (fgets(linein, 1024, f.get()) != nullptr)
      {
        if (isBlankOrCommentLine(linein))
          continue;
        // Classify by the specifier (first non-space char after the leading frame number / range):
        // f/m/o/P/i are per-frame settings lines (written into setArray by pass 2); everything else
        // is a match/combed line. Keying on the specifier -- rather than scanning the whole line for
        // '-'/'+' -- is essential: a settings value can legitimately be negative (e.g. "100 f -1"),
        // and the '-' would otherwise misclassify it as a match line, leaving setArray undersized
        // and causing pass 2 to write out of bounds.
        linep = linein;
        while (*linep != ' ' && *linep != 0) linep++;
        const char specifier = (*linep == ' ') ? *(linep + 1) : 0;
        if (specifier == 'f' || specifier == 'm' || specifier == 'o' || specifier == 'P' || specifier == 'i')
          ++countOvrS;
        else
          ++countOvrM;
      }
      if (ovrDefault != 0 && ovrArray.size())
      {
        if (ovrDefault == 1) q = 0;
        else if (ovrDefault == 2) q = COMBED;
        for (int h = 0; h < vi->numFrames; ++h)
        {
          ovrArray[h] &= 0xDF;
          ovrArray[h] |= 0x10;
          ovrArray[h] &= (q | 0xEF);
          if (q == 0 && ((ovrArray[h] & 7) == 6 ||
            (ovrArray[h] & 7) == 5))
          {
            ovrArray[h] |= 0x07;
            ovrArray[h] &= (1 | 0xF8);
          }
        }
      }
      if (countOvrS == 0 && countOvrM == 0) return;
      if (countOvrS > 0)
      {
        ++countOvrS;
        countOvrS *= 4;
        setArray.resize(countOvrS, 0xffffffff);
      }
      if (countOvrM > 0 && ovrArray.size() == 0)
      {
        ovrArray.resize(vi->numFrames, 255);
        if (ovrDefault != 0)
        {
          if (ovrDefault == 1) q = 0;
          else if (ovrDefault == 2) q = COMBED;
          for (int h = 0; h < vi->numFrames; ++h)
          {
            ovrArray[h] &= 0xDF;
            ovrArray[h] |= 0x10;
            ovrArray[h] &= (q | 0xEF);
          }
        }
      }
      last = -1;
      fieldt = fieldO;
      firstLine = 0;
      i = 0;
      if ((f = decltype (f)(tivtc_fopen(ovr.c_str(), "r"), &fclose)) != nullptr)
      {
        if (debug)
          logInfo(vsapi, vscore, "TFM:  successfully opened ovr file.  Field defaulting to - {}.",
            fieldt == 0 ? "bottom" : "top");
        while (fgets(linein, 1024, f.get()) != nullptr)
        {
          if (isBlankOrCommentLine(linein))
            continue;
          ++firstLine;
          linep = linein;
          while (*linep != 'f' && *linep != 'F' && *linep != 0 && *linep != ' ' && *linep != ',') linep++;
          if (*linep == 'f' || *linep == 'F')
          {
            if (firstLine == 1)
            {
              if (_strnicmp(linein, "field = top", 11) == 0) { fieldt = 1; }
              else if (_strnicmp(linein, "field = bottom", 14) == 0) { fieldt = 0; }
              if (debug)
                logInfo(vsapi, vscore, "TFM:  detected field for ovr file - {}.",
                  fieldt == 0 ? "bottom" : "top");
            }
          }
          else if (*linep == ' ')
          {
            linet = linein;
            while (*linet != 0)
            {
              if (*linet != ' ' && *linet != 10) break;
              linet++;
            }
            if (*linet == 0) { --firstLine; continue; }
            linep++;
            if (*linep == 'p' || *linep == 'c' || *linep == 'n' || *linep == 'b' || *linep == 'u' || *linep == 'l' || *linep == 'h')
            {
              z = -1; // a failed parse must fail the range check below, not reuse a previous line's value
              sscanf(linein, "%d", &z);
              if (z<0 || z>nfrms || z <= last)
              {
                throw TIVTCError("TFM:  ovr file error (out of range or non-ascending frame #)!");
              }
              linep = linein;
              while (*linep != ' ' && *linep != 0) linep++;
              if (*linep != 0)
              {
                linep++;
                q = decodeMatchChar(*linep);
                if (q < 0)
                {
                  throw TIVTCError("TFM:  ovr file error (invalid match specifier)!");
                }
                if (fieldt != fieldO)
                {
                  q = flipMatchFieldOrder(q);
                }
                ovrArray[z] |= 0x07;
                ovrArray[z] &= (q | 0xF8);
                last = z;
              }
            }
            else if (*linep == '-' || *linep == '+')
            {
              z = -1; // a failed parse must fail the range check below, not reuse a previous line's value
              sscanf(linein, "%d", &z);
              if (z<0 || z>nfrms)
              {
                throw TIVTCError("TFM:  ovr file error (out of range or non-ascending frame #)!");
              }
              linep = linein;
              while (*linep != ' ' && *linep != 0) linep++;
              if (*linep != 0)
              {
                linep++;
                q = decodeCombedChar(*linep);
                if (q < 0)
                {
                  throw TIVTCError("TFM:  ovr file error (invalid symbol)!");
                }
                ovrArray[z] &= 0xDF;
                ovrArray[z] |= 0x10;
                ovrArray[z] &= (q | 0xEF);
                if (q == 0 && ((ovrArray[z] & 7) == 6 ||
                  (ovrArray[z] & 7) == 5))
                {
                  ovrArray[z] |= 0x07;
                  ovrArray[z] &= (1 | 0xF8);
                }
              }
            }
            else
            {
              z = -1; // a failed parse must fail the range check below, not reuse a previous line's value
              sscanf(linein, "%d", &z);
              if (z<0 || z>nfrms)
              {
                throw TIVTCError("TFM:  ovr input error (out of range frame #)!");
              }
              linep = linein;
              while (*linep != ' ' && *linep != 0) linep++;
              if (*linep != 0)
              {
                linep++;
                if (*linep == 'f' || *linep == 'm' || *linep == 'o' || *linep == 'P' || *linep == 'i')
                {
                  q = *linep;
                  linep++;
                  linep++;
                  if (*linep == 0) continue;
                  if (sscanf(linep, "%d", &b) != 1) continue;
                  appendSetting(q, z, z, b, i);
                }
              }
            }
          }
          else if (*linep == ',')
          {
            while (*linep != ' ' && *linep != 0) linep++;
            if (*linep == 0) continue;
            linep++;
            if (*linep == 'p' || *linep == 'c' || *linep == 'n' || *linep == 'u' || *linep == 'b' || *linep == 'l' || *linep == 'h')
            {
              z = -1; w = -1; // ditto: a partial parse must not leave a previous line's value in place
              sscanf(linein, "%d,%d", &z, &w);
              if (w == 0) w = nfrms;
              if (z<0 || z>nfrms || w<0 || w>nfrms || w < z || z <= last)
              {
                throw TIVTCError("TFM:  input file error (out of range or non-ascending frame #)!");
              }
              linep = linein;
              while (*linep != ' ' && *linep != 0) linep++;
              if (*linep != 0)
              {
                linep++;
                if (*(linep + 1) == 'p' || *(linep + 1) == 'c' || *(linep + 1) == 'n' || *(linep + 1) == 'b' || *(linep + 1) == 'u' || *(linep + 1) == 'l' || *(linep + 1) == 'h')
                {
                  count = 0;
                  while ((*linep == 'p' || *linep == 'c' || *linep == 'n' || *linep == 'b' || *linep == 'u' || *linep == 'l' || *linep == 'h') && (z + count <= w))
                  {
                    q = decodeMatchChar(*linep);
                    if (q < 0)
                    {
                      throw TIVTCError("TFM:  input file error (invalid match specifier)!");
                    }
                    if (fieldt != fieldO)
                    {
                      q = flipMatchFieldOrder(q);
                    }
                    ovrArray[z + count] |= 0x07;
                    ovrArray[z + count] &= (q | 0xF8);
                    ++count;
                    linep++;
                  }
                  while (z + count <= w)
                  {
                    ovrArray[z + count] |= 0x07;
                    ovrArray[z + count] &= (ovrArray[z] | 0xF8);
                    ++z;
                  }
                  last = w;
                }
                else
                {
                  q = decodeMatchChar(*linep);
                  if (q < 0)
                  {
                    throw TIVTCError("TFM:  input file error (invalid match specifier)!");
                  }
                  if (fieldt != fieldO)
                  {
                    q = flipMatchFieldOrder(q);
                  }
                  while (z <= w)
                  {
                    ovrArray[z] |= 0x07;
                    ovrArray[z] &= (q | 0xF8);
                    ++z;
                  }
                  last = w;
                }
              }
            }
            else if (*linep == '-' || *linep == '+')
            {
              z = -1; w = -1; // ditto: a partial parse must not leave a previous line's value in place
              sscanf(linein, "%d,%d", &z, &w);
              if (w == 0) w = nfrms;
              if (z<0 || z>nfrms || w<0 || w>nfrms || w < z)
              {
                throw TIVTCError("TFM:  input file error (out of range or non-ascending frame #)!");
              }
              linep = linein;
              while (*linep != ' ' && *linep != 0) linep++;
              if (*linep != 0)
              {
                linep++;
                if (*(linep + 1) == '-' || *(linep + 1) == '+')
                {
                  count = 0;
                  while ((*linep == '-' || *linep == '+') && (z + count <= w))
                  {
                    q = decodeCombedChar(*linep);
                    if (q < 0)
                    {
                      throw TIVTCError("TFM:  input file error (invalid symbol)!");
                    }
                    ovrArray[z + count] &= 0xDF;
                    ovrArray[z + count] |= 0x10;
                    ovrArray[z + count] &= (q | 0xEF);
                    if (q == 0 && ((ovrArray[z + count] & 7) == 6 ||
                      (ovrArray[z + count] & 7) == 5))
                    {
                      ovrArray[z + count] |= 0x07;
                      ovrArray[z + count] &= (1 | 0xF8);
                    }
                    ++count;
                    linep++;
                  }
                  while (z + count <= w)
                  {
                    ovrArray[z + count] &= 0xDF;
                    ovrArray[z + count] |= 0x10;
                    ovrArray[z + count] &= (ovrArray[z] | 0xEF);
                    if ((ovrArray[z] & 0x10) == 0 && ((ovrArray[z + count] & 7) == 6 ||
                      (ovrArray[z + count] & 7) == 5))
                    {
                      ovrArray[z + count] |= 0x07;
                      ovrArray[z + count] &= (1 | 0xF8);
                    }
                    ++z;
                  }
                }
                else
                {
                  q = decodeCombedChar(*linep);
                  if (q < 0)
                  {
                    throw TIVTCError("TFM:  input file error (invalid symbol)!");
                  }
                  while (z <= w)
                  {
                    ovrArray[z] &= 0xDF;
                    ovrArray[z] |= 0x10;
                    ovrArray[z] &= (q | 0xEF);
                    if (q == 0 && ((ovrArray[z] & 7) == 6 ||
                      (ovrArray[z] & 7) == 5))
                    {
                      ovrArray[z] |= 0x07;
                      ovrArray[z] &= (1 | 0xF8);
                    }
                    ++z;
                  }
                }
              }
            }
            else
            {
              z = -1; w = -1; // ditto: a partial parse must not leave a previous line's value in place
              sscanf(linein, "%d,%d", &z, &w);
              if (w == 0) w = nfrms;
              if (z<0 || z>nfrms || w<0 || w>nfrms || w < z)
              {
                throw TIVTCError("TFM: ovr input error (invalid frame range)!");
              }
              linep = linein;
              while (*linep != ' ' && *linep != 0) linep++;
              if (*linep != 0)
              {
                linep++;
                if (*linep == 'f' || *linep == 'm' || *linep == 'o' || *linep == 'P' || *linep == 'i')
                {
                  q = *linep;
                  linep++;
                  linep++;
                  if (*linep == 0) continue;
                  if (sscanf(linep, "%d", &b) != 1) continue;
                  appendSetting(q, z, w, b, i);
                }
              }
            }
          }
        }
      }
      else {
          throw TIVTCError("TFM:  ovr file error (could not open file)!");
      }
    }
    else {
        throw TIVTCError("TFM:  ovr input error (could not open ovr file)!");
    }
  }
}

// Open the match/combed output files, resolve their full paths and size the per-frame arrays
// they will be flushed from in the destructor.
void TFM::setupOutputFiles()
{
  std::unique_ptr<FILE, decltype (&fclose)> f(nullptr, nullptr);
  if (output.size())
  {
    if ((f = decltype (f)(tivtc_fopen(output.c_str(), "w"), &fclose)) != nullptr)
    {
      if (_fullpath(outputFull, output.c_str(), MAX_PATH) == nullptr)
        throw TIVTCError("TFM:  output file error (could not resolve the full path)!");
      calcCRC(child, 15, outputCrc, vsapi);
      outArray.resize(vi->numFrames, 0);
      moutArray.resize(vi->numFrames, -1);
      if (micout > 0)
      {
        int sn = micout == 1 ? 3 : 5;
        moutArrayE.resize(vi->numFrames * sn, -20);
      }
    }
    else {
        throw TIVTCError("TFM:  output file error (cannot create file)!");
    }
  }
  if (outputC.size())
  {
    if ((f = decltype (f)(tivtc_fopen(outputC.c_str(), "w"), &fclose)) != nullptr)
    {
      if (_fullpath(outputCFull, outputC.c_str(), MAX_PATH) == nullptr)
        throw TIVTCError("TFM:  outputC file error (could not resolve the full path)!");
      if (outArray.size() == 0)
      {
        outArray.resize(vi->numFrames, 0);
      }
    }
    else {
        throw TIVTCError("TFM:  outputC file error (cannot create file)!");
    }
  }
  /// attach the value of PP to the first frame? TDecimate uses this to do something in the constructor while processing the tfmIn file.
  ///
}

TFM::TFM(VSNode *_child, int _order, int _field, int _mode, int _PP, const char* _ovr,
  const char* _input, const char* _output, const char * _outputC, bool _debug, bool _display,
  int _slow, bool _mChroma, int _cNum, int _cthresh, int _MI, bool _chroma, int _blockx,
  int _blocky, int _y0, int _y1, const char* _d2v, int _ovrDefault, int _flags, double _scthresh,
  int _micout, int _micmatching, const char* _trimIn, bool _usehints, int _metric, bool _batch,
  bool _ubsco, bool _mmsco, int _opt, const VSAPI *_vsapi, VSCore *core)
    : vsapi(_vsapi), child(_child),
  order(_order), field(_field), mode(_mode), PP(_PP), ovr(_ovr), input(_input), output(_output),
  outputC(_outputC), debug(_debug), display(_display), vscore(core), slow(_slow), mChroma(_mChroma), cNum(_cNum),
  cthresh(_cthresh), MI(_MI), chroma(_chroma), blockx(_blockx), blocky(_blocky), y0(_y0),
  y1(_y1), d2v(_d2v), ovrDefault(_ovrDefault), flags(_flags), scthresh(_scthresh), micout(_micout),
  micmatching(_micmatching), trimIn(_trimIn), usehints(_usehints), metric(_metric),
  batch(_batch), ubsco(_ubsco), mmsco(_mmsco), opt(_opt),
  map(nullptr, nullptr), cmask(nullptr, nullptr)
{
    vi = vsapi->getVideoInfo(child);




  if (debug) logInfo(vsapi, vscore, "TFM:  {} by tritical", VERSION);

  validateParameters();

//  child->SetCacheHints(CACHE_GENERIC, 3);  // fixed to diameter (07/30/2005)

  lastMatch.frame = lastMatch.field = lastMatch.combed = lastMatch.match = -20;
  nfrms = vi->numFrames - 1;
  mode_origSaved = mode;
  PP_origSaved = PP;
  MI_origSaved = MI;
  d2vpercent = -20.00f;
  vidCount = 0;

  xhalf = blockx >> 1;
  yhalf = blocky >> 1;
  
  xshift = blockx == 4 ? 2 : blockx == 8 ? 3 : blockx == 16 ? 4 : blockx == 32 ? 5 :
    blockx == 64 ? 6 : blockx == 128 ? 7 : blockx == 256 ? 8 : blockx == 512 ? 9 :
    blockx == 1024 ? 10 : 11;
  yshift = blocky == 4 ? 2 : blocky == 8 ? 3 : blocky == 16 ? 4 : blocky == 32 ? 5 :
    blocky == 64 ? 6 : blocky == 128 ? 7 : blocky == 256 ? 8 : blocky == 512 ? 9 :
    blocky == 1024 ? 10 : 11;

  
  // no high bit depth scaling here
  // Warning: this mod16 must match with the calculation in "checkSceneChange"
  // Keep the pixel count in floating point: width*height*219 overflows a 32 bit int above
  // roughly 9.8 megapixels (5K and up).
  diffmaxsc = (uint64_t)((double((vi->width >> 4) << 4) * vi->height * (235 - 16) * scthresh * 0.5) / 100.0);

  // These modes depend on the previously processed frame. PluginInit must keep this condition in
  // sync with the filter mode it picks; GetFrame enforces the ordering.
  linearAccess = (mode == 7) || d2v.size() > 0;

  sclast.frame = -20;
  sclast.sc = true;

  if (mode == 1 || mode == 2 || mode == 3 || mode == 5 || mode == 6 || mode == 7 ||
    PP > 0 || micout > 0 || micmatching > 0)
  {
    cArray.resize((size_t)(((vi->width + xhalf) >> xshift) + 1) * (((vi->height + yhalf) >> yshift) + 1) * 4);
    cmask = decltype(cmask) (vsapi->newVideoFrame(&vi->format, vi->width, vi->height, nullptr, core), vsapi->freeFrame);
  }

  // prepare map format: always 8 bits
  VSVideoFormat map_format;
  if (!vsapi->queryVideoFormat(&map_format, vi->format.colorFamily, vi->format.sampleType, 8, vi->format.subSamplingW, vi->format.subSamplingH, core))
      throw TIVTCError("TFM:  could not create the 8 bit mask format!");
  map = decltype(map) (vsapi->newVideoFrame(&map_format, vi->width, vi->height, nullptr, core), vsapi->freeFrame);

  if (d2v.size())
  {
    parseD2V();

    trimArray.resize(0);
  }
  order_origSaved = order;
  field_origSaved = fieldO = field;
  if (fieldO == -1)
  {
    if (order == -1) {
        char error[512] = "TFM: Couldn't fetch the first frame from the input clip to determine the clip's field order. Reason: ";
        size_t len = strlen(error);

        const VSFrame *first_frame = vsapi->getFrame(0, child, error + len, (int)(512 - len));
        if (first_frame == nullptr) {
            throw TIVTCError(error);
        }
        const VSMap *props = vsapi->getFramePropertiesRO(first_frame);

        int err;
        int64_t field_based = vsapi->mapGetInt(props, "_FieldBased", 0, &err);
        vsapi->freeFrame(first_frame);
        if (err) {
            throw TIVTCError("TFM: Couldn't find the '_FieldBased' frame property. The 'order' parameter must be used.");
        }

        /// Pretend it's top field first when it says progressive?
        fieldO = (field_based == TopFieldFirst || field_based == Progressive);

    }
    else fieldO = order;
  }
  tpitchy = tpitchuv = -20;
  
  const int ALIGN_BUF = 64;


  {
    // tbuffer is 8 or 16 bits wide
    const int pixelsize = vi->format.bytesPerSample;
    tpitchy = alignUp(vi->width * pixelsize, ALIGN_BUF);
    const int widthUV = vi->format.numPlanes > 1 ? vi->width >> vi->format.subSamplingW : 0;
    tpitchuv = alignUp(widthUV * pixelsize, ALIGN_BUF);
  }

  tbuffer.resize((size_t)(vi->height >> 1) * tpitchy);
  // Seed from the resolved field order, not the raw `field` parameter: that is still -1 whenever
  // the user left it at the default, and mode 7 hands mode7_field straight to `field` for any
  // frame where both candidate matches comb. A -1 there reaches TFMPP as the TFMField property,
  // where PP=3 walks copyField() one row off the end of the plane.
  mode7_field = fieldO;
  parseInputFile();
  parseOvrFile();
  warnOvrOverrides();
  setupOutputFiles();
}
TFM::~TFM()
{
  if (outArray.size())
  {
    FILE *f = nullptr;
    if (output.size())
    {
      if ((f = tivtc_fopen(outputFull, "w")) != nullptr)
      {
        char tb2[256];
        int match, sn = micout == 1 ? 3 : 5;
        if (moutArrayE.size())
        {
          for (int i = 0; i < sn * vi->numFrames; ++i)
          {
            if (moutArrayE[i] == -20) moutArrayE[i] = -1;
          }
        }
        fprintf(f, "#TFM %s by tritical\n", VERSION);
        fprintf(f, "field = %s\n", fieldO == 1 ? "top" : "bottom");
        fprintf(f, "crc32 = %x\n", outputCrc);
        for (int h = 0; h <= nfrms; ++h)
        {
          if (outArray[h] & FILE_ENTRY)
          {
            match = (outArray[h] & 0x07);
            std::string line = std::to_string(h);
            line += ' ';
            line += (char)(matchChar(match));
            if (outArray[h] & 0x20)
              line += (outArray[h] & 0x10) ? " +" : " -";
            if (outArray[h] & FILE_D2V) line += " 1";
            if (moutArray.size() && moutArray[h] != -1)
            {
              line += " [";
              line += std::to_string(moutArray[h]);
              line += ']';
            }
            if (moutArrayE.size())
            {
              int th = h*sn;
              if (sn == 3) snprintf(tb2, sizeof(tb2), " (%d %d %d)", moutArrayE[th + 0],
                moutArrayE[th + 1], moutArrayE[th + 2]);
              else snprintf(tb2, sizeof(tb2), " (%d %d %d %d %d)", moutArrayE[th + 0],
                moutArrayE[th + 1], moutArrayE[th + 2], moutArrayE[th + 3],
                moutArrayE[th + 4]);
              line += tb2;
            }
            line += '\n';
            fputs(line.c_str(), f);
          }
        }
        generateOvrHelpOutput(f);
        fclose(f);
        f = nullptr;
      }
    }
    if (outputC.size())
    {
      if ((f = tivtc_fopen(outputCFull, "w")) != nullptr)
      {
        int count = 0, match;
        fprintf(f, "#TFM %s by tritical\n", VERSION);
        for (int h = 0; h <= nfrms; ++h)
        {
          if (outArray[h] & FILE_ENTRY) match = (outArray[h] & 0x07);
          else match = 0;
          if (match == 1 || match == 5 || match == 6) ++count;
          else
          {
            if (count > cNum) fprintf(f, "%d,%d\n", h - count, h - 1);
            count = 0;
          }
        }
        if (count > cNum) fprintf(f, "%d,%d\n", nfrms - count + 1, nfrms);
        fclose(f);
        f = nullptr;
      }
    }
    if (f != nullptr) fclose(f);
  }

  vsapi->freeNode(child);
}

void TFM::generateOvrHelpOutput(FILE *f) const
{
  int ccount = 0, mcount = 0, acount = 0;
  int ordert = /*order == -1 ? child->GetParity(0) :*/ order; /// can order be -1 at this point? I think not, but test it
  int ao = fieldO^ordert ? 0 : 2;
  for (int i = 0; i < vi->numFrames; ++i)
  {
    if (!(outArray[i] & FILE_ENTRY)) return;
    const int temp = outArray[i] & 0x07;
    if (temp == 3 || temp == 4 || temp == ao) ++acount;
    if (moutArray[i] != -1) ++mcount;
    if ((outArray[i] & 0x30) == 0x30) ++ccount;
  }
  fprintf(f, "#\n#\n# OVR HELP INFORMATION:\n#\n");
  fprintf(f, "# [COMBED FRAMES]\n#\n");
  fprintf(f, "#   [Individual Frames]\n");
  fprintf(f, "#   FORMAT:  frame_number (mic_value)\n#\n");
  if (PP == 0) fprintf(f, "#   none detected (PP=0)\n");
  else if (ccount)
  {
    for (int i = 0; i < vi->numFrames; ++i)
    {
      if ((outArray[i] & 0x30) == 0x30)
      {
        if (moutArray[i] < 0) fprintf(f, "#   %d\n", i);
        else fprintf(f, "#   %d (%d)\n", i, moutArray[i]);
      }
    }
  }
  else fprintf(f, "#   none detected\n");
  fprintf(f, "#\n#   [Grouped Ranges Allowing Small Breaks]\n");
  fprintf(f, "#   FORMAT:  frame_start, frame_end (percentage combed)\n#\n");
  if (PP == 0) fprintf(f, "#   none detected (PP=0)\n");
  else if (ccount)
  {
    int icount = 0, pcount = 0, rcount = 0, i = 0;
    for (; i < vi->numFrames; ++i)
    {
      if ((outArray[i] & 0x30) == 0x30)
      {
        ++icount;
        ++rcount;
        pcount = 0;
      }
      else
      {
        ++pcount;
        if (rcount > 0) ++rcount;
        if (pcount > 12)
        {
          if (icount > 1)
            fprintf(f, "#   %d,%d (%3.1f%c)\n", i - rcount + 1, i - pcount,
              icount*100.0 / double(rcount - pcount), '%');
          rcount = icount = 0;
        }
      }
    }
    if (icount > 1)
      fprintf(f, "#   %d,%d (%3.1f%c)\n", i - rcount, i - pcount,
        icount*100.0 / double(rcount - pcount), '%');
  }
  else fprintf(f, "#   none detected\n");
  fprintf(f, "#\n#\n# [POSSIBLE MISSED COMBED FRAMES]\n#\n");
  fprintf(f, "#   FORMAT:  frame_number (mic_value)\n#\n");
  if (PP == 0) fprintf(f, "#   none detected (PP=0)\n");
  else if (mcount)
  {
    int maxcp = int(MI*0.85), count = 0;
    int mt = std::max(int(MI*0.1875), 5);
    for (int i = 0; i < vi->numFrames; ++i)
    {
      if ((outArray[i] & 0x30) == 0x30)
        continue;
      const int prev = i > 0 ? moutArray[i - 1] : 0;
      const int curr = moutArray[i];
      const int next = i < vi->numFrames - 1 ? moutArray[i + 1] : 0;
      if (curr <= MI && ((curr >= mt && curr > next * 2 && curr > prev * 2 &&
        curr - next > mt && curr - prev > mt) || (curr > maxcp) ||
        (prev > MI && next > MI && curr > MI*0.5) ||
        ((prev > MI || next > MI) && curr > MI*0.75)))
      {
        fprintf(f, "#   %d (%d)\n", i, moutArray[i]);
        ++count;
      }
    }
    if (!count) fprintf(f, "#   none detected\n");
  }
  else fprintf(f, "#   none detected\n");
  fprintf(f, "#\n#\n# [u, b, AND AGAINST ORDER (%c) MATCHES]\n#\n", matchChar(ao));
  fprintf(f, "#   FORMAT:  frame_number match  or  range_start,range_end match\n#\n");
  if (acount)
  {
    int lastf = -1, count = 0, i = 0;
    for (; i < vi->numFrames; ++i)
    {
      const int temp = outArray[i] & 0x07;
      if (temp == 3 || temp == 4 || temp == ao)
      {
        if (lastf == -1) lastf = temp;
        else if (temp != lastf)
        {
          if (count == 1) fprintf(f, "#   %d %c\n", i - 1, matchChar(lastf));
          else fprintf(f, "#   %d,%d %c\n", i - count, i - 1, matchChar(lastf));
          count = 0;
          lastf = temp;
        }
        ++count;
      }
      else if (count)
      {
        if (count == 1) fprintf(f, "#   %d %c\n", i - 1, matchChar(lastf));
        else fprintf(f, "#   %d,%d %c\n", i - count, i - 1, matchChar(lastf));
        count = 0;
        lastf = -1;
      }
    }
    if (count == 1) fprintf(f, "#   %d %c\n", i - 1, matchChar(lastf));
    else if (count > 1) fprintf(f, "#   %d,%d %c\n", i - count, i - 1, matchChar(lastf));
  }
  else fprintf(f, "#   none detected\n");
}
