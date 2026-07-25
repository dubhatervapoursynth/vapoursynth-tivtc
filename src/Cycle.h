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

#ifndef CYCLE_H
#define CYCLE_H

/*
** This class stores all the individual cycle
** info for TDecimate and provides some useful methods.
**
** For all of this class setting an int to -20 = nothing
** (not set), except for type where -1 = nothing
**
**		  VIDEO TYPES
**	-1 = nothing (not set)
**   0 = film
**   1 = film by ovr
**   2 = video by matches
**   3 = video by metrics
**   4 = video by matches/metrics
**   5 = video by ovr
**
**        Blend Codes
**  -20 = not set
**    0 = no blending
**    1 = cvr - blend video cycle down
**    2 = cvr - video cycle w/ scenechange
**    3 = cvr/vfr - 2 dup cycle workaround
**
*/

#include <stdio.h>
#include <limits.h>
//#include "profUtil.h"
#include "stdint.h"
#include <vector>

class Cycle
{
private:
  int cycleSize;
  void allocSpace();
  bool checkMatchDup(int mp, int mc);

public:
  int sdlim;
  int length;		// length of cycle
  int maxFrame;	// nfrms
  int frame;		// first frame of cycle
  int frameE;		// last frame of cycle (frame + length)
  int offE;		// end offset
  int cycleS;		// 0 + start offset
  int cycleE;		// length - offE
  int frameSO;	// frame + cycleS
  int frameEO;	// frame + cycleE
  int type;		// video or film and how
  // All of these hold cycleSize elements and are allocated, resized and copied as a group.
  std::vector<double> diffMetricsN;		// normalized metrics
  std::vector<uint64_t> diffMetricsU;	// unnormalized metrics
  std::vector<uint64_t> diffMetricsUF;	// frame metrics (scenechange detection)
  std::vector<uint64_t> tArray;			// used as temp storage when sorting
  std::vector<int> dupArray;	// duplicate marking
  std::vector<int> lowest;	// sorted list of metrics
  std::vector<int> decimate;	// position of frames to drop
  std::vector<int> decimate2;	// needed for some parts of longest string decimation
  std::vector<int> match;		// frame matches (used for 30p identification)
  std::vector<int> filmd2v;	// d2v trf flags indicate duplicate
  bool dupsSet;	// dups set
  bool mSet;		// metrics set
  bool lowSet;	// list sorted
  bool decSet;	// decimate array filled in
  bool isfilmd2v;	// d2v indicates duplicate in cycle
  int dupCount;	// tracks # of dups for longest string decimation
  int blend;		// 0, 1 (blending), 2 (mkv), others are hijacked for special handling
  std::vector<int> dect, dect2; // scratch copies of decimate/decimate2

  void setFrame(int frameIn);
  void setDecimateLow(int num);
  void setLowest(bool exludeD);
  void setDups(double thresh);
  void setDupsMatches(Cycle &p, const std::vector<uint8_t> &marray);
  void setDecimateLowP(int num);
  void setIsFilmD2V();
  int sceneDetect(uint64_t thresh);
  int sceneDetect(Cycle &prev, Cycle &next, uint64_t thresh);
  int getNonDec(int n);
  void clearAll();

  Cycle(int _size, int _sdlim);
  void setSize(int _size);
  Cycle& operator=(const Cycle& ob2);
};

#endif // CYCLE_H
