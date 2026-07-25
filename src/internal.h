#ifndef __Internal_H__
#define __Internal_H__

#include <stdexcept>
#include <cstring>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#endif



#ifdef _WIN32
#define AVS_FORCEINLINE __forceinline
#else
#define AVS_FORCEINLINE __attribute__((always_inline)) inline
#endif


// Frame properties set by TFM:
#define PROP_TFMDisplay "TFMDisplay"
#define PROP_TFMMATCH "TFMMatch"
#define PROP_TFMMics "TFMMics"
#define PROP_Combed "_Combed"
#define PROP_TFMD2VFilm "TFMD2VFilm"
#define PROP_TFMField "TFMField"
#define PROP_TFMPP "TFMPP"

// Frame properties set by TDecimate:
#define PROP_TDecimateDisplay "TDecimateDisplay"
#define PROP_TDecimateCycleStart "TDecimateCycleStart"
#define PROP_TDecimateCycleMaxBlockDiff "TDecimateCycleMaxBlockDiff" // uint64_t[]
#define PROP_TDecimateOriginalFrame "TDecimateOriginalFrame"
#define PROP_DurationNum "_DurationNum"
#define PROP_DurationDen "_DurationDen"

class TIVTCError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};


constexpr int ISP = 0x00000000; // p
constexpr int ISC = 0x00000001; // c
constexpr int ISN = 0x00000002; // n
constexpr int ISB = 0x00000003; // b
constexpr int ISU = 0x00000004; // u
constexpr int ISDB = 0x00000005; // l = (deinterlaced c bottom field)
constexpr int ISDT = 0x00000006; // h = (deinterlaced c top field)

constexpr int TOP_FIELD = 0x00000008;
constexpr int COMBED = 0x00000010;
constexpr int D2VFILM = 0x00000020;

#define MTC(n) n == 0 ? 'p' : n == 1 ? 'c' : n == 2 ? 'n' : n == 3 ? 'b' : n == 4 ? 'u' : \
               n == 5 ? 'l' : n == 6 ? 'h' : 'x'

// Decode an override/input file match specifier into a match code, or -1 if the character is
// not one. (The parsers used to spell these out as decimal character codes.)
static inline int decodeMatchChar(int c)
{
  switch (c)
  {
  case 'p': return ISP;
  case 'c': return ISC;
  case 'n': return ISN;
  case 'b': return ISB;
  case 'u': return ISU;
  case 'l': return ISDB;
  case 'h': return ISDT;
  default:  return -1;
  }
}

// Blank lines and lines opening with ';' or '#' carry no data in any of the ovr/input file formats.
static inline bool isBlankOrCommentLine(const char *line)
{
  return line[0] == 0 || line[0] == '\n' || line[0] == '\r' || line[0] == ';' || line[0] == '#';
}

// Advance to the first match specifier on the line, or to the terminating nul if there is none.
static inline char *skipToMatchChar(char *p)
{
  while (*p != 0 && decodeMatchChar(*p) < 0) ++p;
  return p;
}

// Decode an override/input file combed specifier: '-' clean, '+' combed, -1 if neither.
static inline int decodeCombedChar(int c)
{
  if (c == '-') return 0;
  if (c == '+') return COMBED;
  return -1;
}

// A match code is relative to the field order it was recorded with. Reading one back under the
// opposite order swaps the two neighbour pairs: p<->b (prev frame, other parity) and
// n<->u (next frame, other parity). c/l/h name the current frame and are unaffected.
static inline int flipMatchFieldOrder(int match)
{
  switch (match)
  {
  case ISP: return ISB;
  case ISN: return ISU;
  case ISB: return ISP;
  case ISU: return ISN;
  default:  return match;
  }
}

constexpr int FILE_COMBED = 0x00000030;
constexpr int FILE_NOTCOMBED = 0x00000020;
constexpr int FILE_ENTRY = 0x00000080;
constexpr int FILE_D2V = 0x00000008;
constexpr int D2VARRAY_DUP_MASK = 0x03;
constexpr int D2VARRAY_MATCH_MASK = 0x3C;

constexpr int DROP_FRAME = 0x00000001; // ovr array - bit 1
constexpr int KEEP_FRAME = 0x00000002; // ovr array - 2
constexpr int FILM = 0x00000004; // ovr array - bit 3
constexpr int VIDEO = 0x00000008; // ovr array - bit 4
constexpr int ISMATCH = 0x00000070; // ovr array - bits 5-7
constexpr int ISD2VFILM = 0x00000080; // ovr array - bit 8

#define cfps(n) n == 1 ? "119.880120" : n == 2 ? "59.940060" : n == 3 ? "39.960040" : \
                n == 4 ? "29.970030" : n == 5 ? "23.976024" : "unknown"


#ifdef VERSION
#undef VERSION
#endif
#define VERSION "v1.0.7"


static FILE *tivtc_fopen(const char *name, const char *mode) {
#ifdef _WIN32
    int len = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
    std::wstring wname(len, 0);

    int ret = MultiByteToWideChar(CP_UTF8, 0, name, -1, wname.data(), len);
    if (ret == len) {
        std::wstring wmode(mode, mode + strlen(mode));
        return _wfopen(wname.c_str(), wmode.c_str());
    } else
        throw TIVTCError("Failed to convert file name to wide char.");
#else
    return std::fopen(name, mode);
#endif
}


#endif  // __Internal_H__
