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

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <VapourSynth4.h>
#include <VSHelper4.h>

#include "TFM.h"
#include "TFMPP.h"
#include "TDecimate.h"


static const VSFrame *VS_CC tfmGetFrame(int n, int activationReason, void *instanceData, [[maybe_unused]] void **frameData, VSFrameContext *frameCtx, VSCore *core, [[maybe_unused]] const VSAPI *vsapi) {
    TFM *d = (TFM *)instanceData;

    return d->GetFrame(n, activationReason, frameCtx, core);
}


static void VS_CC tfmFree(void *instanceData, [[maybe_unused]] VSCore *core, [[maybe_unused]] const VSAPI *vsapi) {
    TFM *d = (TFM *)instanceData;

    delete d;
}


static const VSFrame *VS_CC tfmppGetFrame(int n, int activationReason, void *instanceData, [[maybe_unused]] void **frameData, VSFrameContext *frameCtx, VSCore *core, [[maybe_unused]] const VSAPI *vsapi) {
    TFMPP *d = (TFMPP *)instanceData;

    return d->GetFrame(n, activationReason, frameCtx, core);
}


static void VS_CC tfmppFree(void *instanceData, [[maybe_unused]] VSCore *core, [[maybe_unused]] const VSAPI *vsapi) {
    TFMPP *d = (TFMPP *)instanceData;

    delete d;
}


enum DisplayFilters {
    DisplayTFM,
    DisplayTDecimate
};

template <DisplayFilters filter>
static void VS_CC tivtcDisplayFunc(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    VSNode *clip = (VSNode *)userData;

    const char *display_prop = filter == DisplayTFM ? PROP_TFMDisplay : PROP_TDecimateDisplay;

    int err;

    const VSFrame *f = vsapi->mapGetFrame(in, "f", 0, &err);
    if (err) {
        // Nothing to annotate; hand the clip back untouched.
        vsapi->mapSetNode(out, "val", clip, maReplace);
        return;
    }
    const VSMap *props = vsapi->getFramePropertiesRO(f);
    const char *text = vsapi->mapGetData(props, display_prop, 0, &err);
    int text_size = err ? 0 : vsapi->mapGetDataSize(props, display_prop, 0, nullptr);
    if (err) {
        // Not every frame carries the property (e.g. TDecimate mode 3's end-of-clip notice, or
        // a per-frame PP override that makes TFM defer to TFMPP when TFMPP isn't in the chain).
        // Reading it with a null error pointer made VapourSynth abort the process instead.
        vsapi->freeFrame(f);
        vsapi->mapSetNode(out, "val", clip, maReplace);
        return;
    }

    VSMap *params = vsapi->createMap();
    vsapi->mapSetNode(params, "clip", clip, maReplace); // clip is freed by vapoursynth somewhere. We don't free it here.
    vsapi->mapSetData(params, "text", text, text_size, dtUtf8, maReplace);
    vsapi->freeFrame(f);

    VSPlugin *text_plugin = vsapi->getPluginByID("com.vapoursynth.text", core);
    VSMap *ret = vsapi->invoke(text_plugin, "Text", params);
    vsapi->freeMap(params);
    if (vsapi->mapGetError(ret)) {
        char error[512] = { 0 };
        snprintf(error, 512, "%s: failed to invoke text.Text: %s", filter == DisplayTFM ? "TFM" : "TDecimate", vsapi->mapGetError(ret));
        vsapi->freeMap(ret);
        vsapi->mapSetError(out, error);
        return;
    }
    clip = vsapi->mapGetNode(ret, "clip", 0, nullptr);
    vsapi->freeMap(ret);
    vsapi->mapSetNode(out, "val", clip, maReplace);
    vsapi->freeNode(clip);
}


// Every filter parameter here is optional with a fixed default, and the API reports "absent" via
// the err out-parameter rather than the return value. These three collapse that three-line dance
// into one expression.
static int optInt(const VSMap *in, const char *name, int def, const VSAPI *vsapi)
{
  int err;
  const int v = vsh::int64ToIntS(vsapi->mapGetInt(in, name, 0, &err));
  return err ? def : v;
}

static bool optBool(const VSMap *in, const char *name, bool def, const VSAPI *vsapi)
{
  int err;
  const bool v = !!vsapi->mapGetInt(in, name, 0, &err);
  return err ? def : v;
}

static double optFloat(const VSMap *in, const char *name, double def, const VSAPI *vsapi)
{
  int err;
  const double v = vsapi->mapGetFloat(in, name, 0, &err);
  return err ? def : v;
}

static const char *optData(const VSMap *in, const char *name, const char *def, const VSAPI *vsapi)
{
  int err;
  const char *v = vsapi->mapGetData(in, name, 0, &err);
  return err ? def : v;
}

static void VS_CC tfmCreate(const VSMap *in, VSMap *out, [[maybe_unused]] void *userData, VSCore *core, const VSAPI *vsapi) {
    int err;

    const int order = optInt(in, "order", -1, vsapi);

    const int field = optInt(in, "field", -1, vsapi);

    const int mode = optInt(in, "mode", 1, vsapi);

    const int PP = optInt(in, "PP", 6, vsapi);

    const char *ovr = optData(in, "ovr", "", vsapi);

    const char *input = optData(in, "input", "", vsapi);

    const char *output = optData(in, "output", "", vsapi);

    const char *outputC = optData(in, "outputC", "", vsapi);

    bool debug = !!vsapi->mapGetInt(in, "debug", 0, &err); /// not used for anything at the moment. maybe use logMessage ?
    if (err)
        debug = false;

    const bool display = optBool(in, "display", false, vsapi);

    const int slow = optInt(in, "slow", 1, vsapi);

    const bool mChroma = optBool(in, "mChroma", true, vsapi);

    const int cNum = optInt(in, "cNum", 15, vsapi);

    const int cthresh = optInt(in, "cthresh", 9, vsapi);

    const int MI = optInt(in, "MI", 80, vsapi);

    bool chroma = optBool(in, "chroma", false, vsapi);  // forced off for Y-only clips below

    const int blockx = optInt(in, "blockx", 16, vsapi);

    const int blocky = optInt(in, "blocky", 16, vsapi);

    const int y0 = optInt(in, "y0", 0, vsapi);

    const int y1 = optInt(in, "y1", 0, vsapi);

    const int mthresh = optInt(in, "mthresh", 5, vsapi);

    const char *d2v = optData(in, "d2v", "", vsapi);

    const int ovrDefault = optInt(in, "ovrDefault", 0, vsapi);

    const int flags = optInt(in, "flags", 4, vsapi);

    const double scthresh = optFloat(in, "scthresh", 12.0, vsapi);

    const int micout = optInt(in, "micout", 0, vsapi);

    const int micmatching = optInt(in, "micmatching", 1, vsapi);

    const char *trimIn = optData(in, "trimIn", "", vsapi);

    const bool hint = optBool(in, "hint", true, vsapi);

    const int metric = optInt(in, "metric", 0, vsapi);

    const bool batch = optBool(in, "batch", false, vsapi);

    const bool ubsco = optBool(in, "ubsco", true, vsapi);

    const bool mmsco = optBool(in, "mmsco", true, vsapi);



    VSNode *clip = vsapi->mapGetNode(in, "clip", 0, nullptr);

    TFM *tfm_data;

    try {
        tfm_data = new TFM(clip, order, field, mode, PP, ovr, input, output, outputC, debug, display, slow, mChroma, cNum, cthresh,
                       MI, chroma, blockx, blocky, y0, y1, d2v, ovrDefault, flags, scthresh, micout, micmatching, trimIn, hint,
                       metric, batch, ubsco, mmsco, vsapi, core);
    } catch (const TIVTCError& e) {
        vsapi->mapSetError(out, e.what());

        vsapi->freeNode(clip);

        return;
    }

    // mode 7 carries state between frames and so does d2v duplicate detection: both decide frame
    // n partly from the match chosen for frame n-1. API 3 could ask the core for in-order
    // requests with nfMakeLinear; API 4 has no equivalent (setLinearFilter only enables the
    // cacheFrame API for filters that *produce* linearly), so the best available is fmUnordered,
    // which at least serializes the state. The carried state is only consulted when it really is
    // frame n-1 -- see TFM::linearAccess, which this condition must stay in sync with.
    const bool needLinear = (mode == 7 || d2v[0] != '\0');

    // TFM reads prv/src/nxt, so any given source frame feeds three output frames: rpGeneral.
    VSFilterDependency tfm_deps[] = {{ clip, rpGeneral }};
    VSNode *tfm_node = vsapi->createVideoFilter2("TFM", tfm_data->vi, tfmGetFrame, tfmFree,
        needLinear ? fmUnordered : fmParallelRequests, tfm_deps, 1, tfm_data, core);
    if (!tfm_node) {
        // On failure the core neither takes the dependency references nor calls the free
        // callback, so the instance (which owns the clip reference) is ours to destroy.
        delete tfm_data;
        vsapi->mapSetError(out, "TFM: failed to create the filter node.");
        return;
    }

    // No std.Cache here: API 4 inserts and sizes caches automatically.

    if (PP > 1) {
        VSNode *clip2 = vsapi->mapGetNode(in, "clip2", 0, &err);

        TFMPP *tfmpp_data;

        try {
            tfmpp_data = new TFMPP(tfm_node, PP, mthresh, ovr, display, clip2, hint, vsapi, core);
        } catch (const TIVTCError& e) {
            vsapi->mapSetError(out, e.what());

            vsapi->freeNode(tfm_node);
            vsapi->freeNode(clip2);

            return;
        }

        // TFMPP reads n-1/n/n+1 from TFM depending on the effective PP, and frame n from clip2.
        VSFilterDependency pp_deps[2];
        int numDeps = 0;
        pp_deps[numDeps++] = { tfm_node, rpGeneral };
        if (clip2)
            pp_deps[numDeps++] = { clip2, rpGeneral };

        VSNode *pp_node = vsapi->createVideoFilter2("TFMPP", tfmpp_data->vi, tfmppGetFrame, tfmppFree,
            fmParallelRequests, pp_deps, numDeps, tfmpp_data, core);
        if (!pp_node) {
            delete tfmpp_data; // owns and frees tfm_node and clip2
            vsapi->mapSetError(out, "TFM: failed to create the TFMPP filter node.");
            return;
        }
        tfm_node = pp_node;
    }

    vsapi->mapConsumeNode(out, "clip", tfm_node, maReplace);

    if (display) {
        // text.FrameProps won't print the TFMDisplay property because it is too long,
        // so we use text.Text with std.FrameEval instead.
        VSMap *params = vsapi->createMap();
        VSNode *node = vsapi->mapGetNode(out, "clip", 0, nullptr);
        vsapi->mapSetNode(params, "clip", node, maReplace);
        vsapi->mapSetNode(params, "prop_src", node, maReplace);
        VSFunction *displayFuncRef = vsapi->createFunction(tivtcDisplayFunc<DisplayTFM>, vsapi->addNodeRef(node), (VSFreeFunctionData)vsapi->freeNode, core);
        vsapi->freeNode(node);
        vsapi->mapSetFunction(params, "eval", displayFuncRef, maReplace);
        vsapi->freeFunction(displayFuncRef);
        VSPlugin *std_plugin = vsapi->getPluginByID("com.vapoursynth.std", core);
        VSMap *ret = vsapi->invoke(std_plugin, "FrameEval", params);
        vsapi->freeMap(params);
        if (vsapi->mapGetError(ret)) {
            char error[512] = { 0 };
            snprintf(error, 512, "TFM: failed to invoke std.FrameEval: %s", vsapi->mapGetError(ret));
            vsapi->freeMap(ret);
            vsapi->mapSetError(out, error);
            return;
        }
        node = vsapi->mapGetNode(ret, "clip", 0, nullptr);
        vsapi->freeMap(ret);
        vsapi->mapSetNode(out, "clip", node, maReplace);
        vsapi->freeNode(node);
    }
}


static const VSFrame *VS_CC tdecimateGetFrame(int n, int activationReason, void *instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, [[maybe_unused]] const VSAPI *vsapi) {
    TDecimate *d = (TDecimate *)instanceData;

    return d->GetFrame(n, activationReason, frameData, frameCtx, core);
}


static void VS_CC tdecimateFree(void *instanceData, [[maybe_unused]] VSCore *core, [[maybe_unused]] const VSAPI *vsapi) {
    TDecimate *d = (TDecimate *)instanceData;

    delete d;
}


static void VS_CC tdecimateCreate(const VSMap *in, VSMap *out, [[maybe_unused]] void *userData, VSCore *core, const VSAPI *vsapi) {
    int err;

    VSNode *clip = vsapi->mapGetNode(in, "clip", 0, nullptr); /// move lower if possible

    const int mode = optInt(in, "mode", 0, vsapi);

    const int cycleR = optInt(in, "cycleR", 1, vsapi);

    const int cycle = optInt(in, "cycle", 5, vsapi);

    const double rate = optFloat(in, "rate", 23.976, vsapi);

    bool chroma = optBool(in, "chroma", true, vsapi);  // forced off for Y-only clips below

    {
        const VSVideoInfo *vi = vsapi->getVideoInfo(clip);
        if (vi->format.colorFamily == cfGray)
            chroma = false;
    }

    const double dupThresh = optFloat(in, "dupThresh", mode == 7 ? (chroma ? 0.4 : 0.5)
                              : (chroma ? 1.1 : 1.4), vsapi);

    const double vidThresh = optFloat(in, "vidThresh", mode == 7 ? (chroma ? 3.5 : 4.0)
                              : (chroma ? 1.1 : 1.4), vsapi);

    const double sceneThresh = optFloat(in, "sceneThresh", 15, vsapi);

    const int hybrid = optInt(in, "hybrid", 0, vsapi);

    const int vidDetect = optInt(in, "vidDetect", 3, vsapi);

    const int conCycle = optInt(in, "conCycle", vidDetect >= 3 ? 1 : 2, vsapi);

    const int conCycleTP = optInt(in, "conCycleTP", vidDetect >= 3 ? 1 : 2, vsapi);

    const char *ovr = optData(in, "ovr", "", vsapi);

    const char *output = optData(in, "output", "", vsapi);

    const char *input = optData(in, "input", "", vsapi);

    const char *tfmIn = optData(in, "tfmIn", "", vsapi);

    const char *mkvOut = optData(in, "mkvOut", "", vsapi);

    const int nt = optInt(in, "nt", 0, vsapi);

    const int blockx = optInt(in, "blockx", 32, vsapi);

    const int blocky = optInt(in, "blocky", 32, vsapi);

    const bool debug = optBool(in, "debug", false, vsapi);

    const bool display = optBool(in, "display", false, vsapi);

    const int vfrDec = optInt(in, "vfrDec", 1, vsapi);

    const bool batch = optBool(in, "batch", false, vsapi);

    const bool tcfv1 = optBool(in, "tcfv1", true, vsapi);

    const bool se = optBool(in, "se", false, vsapi);

    const bool exPP = optBool(in, "exPP", false, vsapi);

    const int maxndl = optInt(in, "maxndl", -200, vsapi);

    const bool m2PA = optBool(in, "m2PA", false, vsapi);

    const bool denoise = optBool(in, "denoise", false, vsapi);

    const bool noblend = optBool(in, "noblend", true, vsapi);

    const bool ssd = optBool(in, "ssd", false, vsapi);

    const bool hint = optBool(in, "hint", true, vsapi);

    VSNode *clip2 = vsapi->mapGetNode(in, "clip2", 0, &err);
    if (err)
        clip2 = vsapi->addNodeRef(clip); // simplifies the code in the getframe functions

    const int sdlim = optInt(in, "sdlim", 0, vsapi);


    const char *orgOut = optData(in, "orgOut", "", vsapi);


    TDecimate *tdecimate_data;

    try {
        tdecimate_data = new TDecimate(clip, mode, cycleR, cycle, rate, dupThresh, vidThresh, sceneThresh, hybrid, vidDetect, conCycle, conCycleTP, ovr, output, input, tfmIn, mkvOut, nt, blockx, blocky, debug, display, vfrDec, batch, tcfv1, se, chroma, exPP, maxndl, m2PA, denoise, noblend, ssd, hint, clip2, sdlim, orgOut, vsapi, core);
    } catch (const TIVTCError& e) {
        vsapi->mapSetError(out, e.what());

        vsapi->freeNode(clip);
        vsapi->freeNode(clip2);

        return;
    }

    const int filter_modes[8] = {
        fmParallelRequests,
        fmParallelRequests,
        fmUnordered, // Either fmUnordered or fmParallelRequests. I figured out which one but I didn't write it down and forgot.
        fmUnordered, // mode 3 also needs linear access; it detects and reports violations itself
        fmParallel, // mode 4 uses per-invocation metric scratch and is safe to run concurrently
        fmParallel,
        fmParallel,
        fmUnordered
    };

    // Every mode reads arbitrary frames of the source (decimation reorders them), and both child
    // and clip2 can be read for the same output frame, so neither dependency is spatial.
    VSFilterDependency dec_deps[] = {{ clip, rpGeneral }, { clip2, rpGeneral }};
    VSNode *dec_node = vsapi->createVideoFilter2("TDecimate", &tdecimate_data->vi, tdecimateGetFrame, tdecimateFree,
        filter_modes[mode], dec_deps, 2, tdecimate_data, core);
    if (!dec_node) {
        // The core took neither the dependency references nor ownership of the instance on
        // failure; the instance owns clip and clip2 and frees them on destruction.
        delete tdecimate_data;
        vsapi->mapSetError(out, "TDecimate: failed to create the filter node.");
        return;
    }

    vsapi->mapConsumeNode(out, "clip", dec_node, maReplace);


    if (display) {
        // text.FrameProps won't print the TDecimateDisplay property because it is too long,
        // so we use text.Text with std.FrameEval instead.
        VSMap *params = vsapi->createMap();
        VSNode *node = vsapi->mapGetNode(out, "clip", 0, nullptr);
        vsapi->mapSetNode(params, "clip", node, maReplace);
        vsapi->mapSetNode(params, "prop_src", node, maReplace);
        VSFunction *displayFuncRef = vsapi->createFunction(tivtcDisplayFunc<DisplayTDecimate>, vsapi->addNodeRef(node), (VSFreeFunctionData)vsapi->freeNode, core);
        vsapi->freeNode(node);
        vsapi->mapSetFunction(params, "eval", displayFuncRef, maReplace);
        vsapi->freeFunction(displayFuncRef);
        VSPlugin *std_plugin = vsapi->getPluginByID("com.vapoursynth.std", core);
        VSMap *ret = vsapi->invoke(std_plugin, "FrameEval", params);
        vsapi->freeMap(params);
        if (vsapi->mapGetError(ret)) {
            char error[512] = { 0 };
            snprintf(error, 512, "TDecimate: failed to invoke std.FrameEval: %s", vsapi->mapGetError(ret));
            vsapi->freeMap(ret);
            vsapi->mapSetError(out, error);
            return;
        }
        node = vsapi->mapGetNode(ret, "clip", 0, nullptr);
        vsapi->freeMap(ret);
        vsapi->mapSetNode(out, "clip", node, maReplace);
        vsapi->freeNode(node);
    }
}


VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin *plugin, const VSPLUGINAPI *vspapi) {
    vspapi->configPlugin("com.nodame.tivtc", "tivtc", "Field matching and decimation", VS_MAKE_VERSION(4, 0), VAPOURSYNTH_API_VERSION, 0, plugin);
    vspapi->registerFunction("TFM",
                 "clip:vnode;"
                 "order:int:opt;"
                 "field:int:opt;"
                 "mode:int:opt;"
                 "PP:int:opt;"
                 "ovr:data:opt;"
                 "input:data:opt;"
                 "output:data:opt;"
                 "outputC:data:opt;"
                 "debug:int:opt;"
                 "display:int:opt;"
                 "slow:int:opt;"
                 "mChroma:int:opt;"
                 "cNum:int:opt;"
                 "cthresh:int:opt;"
                 "MI:int:opt;"
                 "chroma:int:opt;"
                 "blockx:int:opt;"
                 "blocky:int:opt;"
                 "y0:int:opt;"
                 "y1:int:opt;"
                 "mthresh:int:opt;"
                 "clip2:vnode:opt;"
                 "d2v:data:opt;"
                 "ovrDefault:int:opt;"
                 "flags:int:opt;"
                 "scthresh:float:opt;"
                 "micout:int:opt;"
                 "micmatching:int:opt;"
                 "trimIn:data:opt;"
                 "hint:int:opt;"
                 "metric:int:opt;"
                 "batch:int:opt;"
                 "ubsco:int:opt;"
                 "mmsco:int:opt;"
                 , "clip:vnode;", tfmCreate, nullptr, plugin);

    vspapi->registerFunction("TDecimate",
                 "clip:vnode;"
                 "mode:int:opt;"
                 "cycleR:int:opt;"
                 "cycle:int:opt;"
                 "rate:float:opt;"
                 "dupThresh:float:opt;"
                 "vidThresh:float:opt;"
                 "sceneThresh:float:opt;"
                 "hybrid:int:opt;"
                 "vidDetect:int:opt;"
                 "conCycle:int:opt;"
                 "conCycleTP:int:opt;"
                 "ovr:data:opt;"
                 "output:data:opt;"
                 "input:data:opt;"
                 "tfmIn:data:opt;"
                 "mkvOut:data:opt;"
                 "nt:int:opt;"
                 "blockx:int:opt;"
                 "blocky:int:opt;"
                 "debug:int:opt;"
                 "display:int:opt;"
                 "vfrDec:int:opt;"
                 "batch:int:opt;"
                 "tcfv1:int:opt;"
                 "se:int:opt;"
                 "chroma:int:opt;"
                 "exPP:int:opt;"
                 "maxndl:int:opt;"
                 "m2PA:int:opt;"
                 "denoise:int:opt;"
                 "noblend:int:opt;"
                 "ssd:int:opt;"
                 "hint:int:opt;"
                 "clip2:vnode:opt;"
                 "sdlim:int:opt;"
                 "orgOut:data:opt;"
                 , "clip:vnode;", tdecimateCreate, nullptr, plugin);
}
