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


static void VS_CC tfmCreate(const VSMap *in, VSMap *out, [[maybe_unused]] void *userData, VSCore *core, const VSAPI *vsapi) {
    int err;

    int order = vsh::int64ToIntS(vsapi->mapGetInt(in, "order", 0, &err));
    if (err)
        order = -1;

    int field = vsh::int64ToIntS(vsapi->mapGetInt(in, "field", 0, &err));
    if (err)
        field = -1;

    int mode = vsh::int64ToIntS(vsapi->mapGetInt(in, "mode", 0, &err));
    if (err)
        mode = 1;

    int PP = vsh::int64ToIntS(vsapi->mapGetInt(in, "PP", 0, &err));
    if (err)
        PP = 6;

    const char *ovr = vsapi->mapGetData(in, "ovr", 0, &err);
    if (err)
        ovr = "";

    const char *input = vsapi->mapGetData(in, "input", 0, &err);
    if (err)
        input = "";

    const char *output = vsapi->mapGetData(in, "output", 0, &err);
    if (err)
        output = "";

    const char *outputC = vsapi->mapGetData(in, "outputC", 0, &err);
    if (err)
        outputC = "";

    bool debug = !!vsapi->mapGetInt(in, "debug", 0, &err); /// not used for anything at the moment. maybe use logMessage ?
    if (err)
        debug = false;

    bool display = !!vsapi->mapGetInt(in, "display", 0, &err);
    if (err)
        display = false;

    int slow = vsh::int64ToIntS(vsapi->mapGetInt(in, "slow", 0, &err));
    if (err)
        slow = 1;

    bool mChroma = !!vsapi->mapGetInt(in, "mChroma", 0, &err);
    if (err)
        mChroma = true;

    int cNum = vsh::int64ToIntS(vsapi->mapGetInt(in, "cNum", 0, &err));
    if (err)
        cNum = 15;

    int cthresh = vsh::int64ToIntS(vsapi->mapGetInt(in, "cthresh", 0, &err));
    if (err)
        cthresh = 9;

    int MI = vsh::int64ToIntS(vsapi->mapGetInt(in, "MI", 0, &err));
    if (err)
        MI = 80;

    bool chroma = !!vsapi->mapGetInt(in, "chroma", 0, &err);
    if (err)
        chroma = false;

    int blockx = vsh::int64ToIntS(vsapi->mapGetInt(in, "blockx", 0, &err));
    if (err)
        blockx = 16;

    int blocky = vsh::int64ToIntS(vsapi->mapGetInt(in, "blocky", 0, &err));
    if (err)
        blocky = 16;

    int y0 = vsh::int64ToIntS(vsapi->mapGetInt(in, "y0", 0, &err));
    if (err)
        y0 = 0;

    int y1 = vsh::int64ToIntS(vsapi->mapGetInt(in, "y1", 0, &err));
    if (err)
        y1 = 0;

    int mthresh = vsh::int64ToIntS(vsapi->mapGetInt(in, "mthresh", 0, &err));
    if (err)
        mthresh = 5;

    const char *d2v = vsapi->mapGetData(in, "d2v", 0, &err);
    if (err)
        d2v = "";

    int ovrDefault = vsh::int64ToIntS(vsapi->mapGetInt(in, "ovrDefault", 0, &err));
    if (err)
        ovrDefault = 0;

    int flags = vsh::int64ToIntS(vsapi->mapGetInt(in, "flags", 0, &err));
    if (err)
        flags = 4;

    double scthresh = vsapi->mapGetFloat(in, "scthresh", 0, &err);
    if (err)
        scthresh = 12.0;

    int micout = vsh::int64ToIntS(vsapi->mapGetInt(in, "micout", 0, &err));
    if (err)
        micout = 0;

    int micmatching = vsh::int64ToIntS(vsapi->mapGetInt(in, "micmatching", 0, &err));
    if (err)
        micmatching = 1;

    const char *trimIn = vsapi->mapGetData(in, "trimIn", 0, &err);
    if (err)
        trimIn = "";

    bool hint = !!vsapi->mapGetInt(in, "hint", 0, &err);
    if (err)
        hint = true;

    int metric = vsh::int64ToIntS(vsapi->mapGetInt(in, "metric", 0, &err));
    if (err)
        metric = 0;

    bool batch = !!vsapi->mapGetInt(in, "batch", 0, &err);
    if (err)
        batch = false;

    bool ubsco = !!vsapi->mapGetInt(in, "ubsco", 0, &err);
    if (err)
        ubsco = true;

    bool mmsco = !!vsapi->mapGetInt(in, "mmsco", 0, &err);
    if (err)
        mmsco = true;

    int opt = vsh::int64ToIntS(vsapi->mapGetInt(in, "opt", 0, &err));
    if (err)
        opt = 4;


    VSNode *clip = vsapi->mapGetNode(in, "clip", 0, nullptr);

    TFM *tfm_data;

    try {
        tfm_data = new TFM(clip, order, field, mode, PP, ovr, input, output, outputC, debug, display, slow, mChroma, cNum, cthresh,
                       MI, chroma, blockx, blocky, y0, y1, d2v, ovrDefault, flags, scthresh, micout, micmatching, trimIn, hint,
                       metric, batch, ubsco, mmsco, opt, vsapi, core);
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
            tfmpp_data = new TFMPP(tfm_node, PP, mthresh, ovr, display, clip2, hint, opt, vsapi, core);
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

    int mode = vsh::int64ToIntS(vsapi->mapGetInt(in, "mode", 0, &err));
    if (err)
        mode = 0;

    int cycleR = vsh::int64ToIntS(vsapi->mapGetInt(in, "cycleR", 0, &err));
    if (err)
        cycleR = 1;

    int cycle = vsh::int64ToIntS(vsapi->mapGetInt(in, "cycle", 0, &err));
    if (err)
        cycle = 5;

    double rate = vsapi->mapGetFloat(in, "rate", 0, &err);
    if (err)
        rate = 23.976;

    bool chroma = !!vsapi->mapGetInt(in, "chroma", 0, &err);
    if (err)
        chroma = true;

    {
        const VSVideoInfo *vi = vsapi->getVideoInfo(clip);
        if (vi->format.colorFamily == cfGray)
            chroma = false;
    }

    double dupThresh = vsapi->mapGetFloat(in, "dupThresh", 0, &err);
    if (err)
        dupThresh = mode == 7 ? (chroma ? 0.4 : 0.5)
                              : (chroma ? 1.1 : 1.4);

    double vidThresh = vsapi->mapGetFloat(in, "vidThresh", 0, &err);
    if (err)
        vidThresh = mode == 7 ? (chroma ? 3.5 : 4.0)
                              : (chroma ? 1.1 : 1.4);

    double sceneThresh = vsapi->mapGetFloat(in, "sceneThresh", 0, &err);
    if (err)
        sceneThresh = 15;

    int hybrid = vsh::int64ToIntS(vsapi->mapGetInt(in, "hybrid", 0, &err));
    if (err)
        hybrid = 0;

    int vidDetect = vsh::int64ToIntS(vsapi->mapGetInt(in, "vidDetect", 0, &err));
    if (err)
        vidDetect = 3;

    int conCycle = vsh::int64ToIntS(vsapi->mapGetInt(in, "conCycle", 0, &err));
    if (err)
        conCycle = vidDetect >= 3 ? 1 : 2;

    int conCycleTP = vsh::int64ToIntS(vsapi->mapGetInt(in, "conCycleTP", 0, &err));
    if (err)
        conCycleTP = vidDetect >= 3 ? 1 : 2;

    const char *ovr = vsapi->mapGetData(in, "ovr", 0, &err);
    if (err)
        ovr = "";

    const char *output = vsapi->mapGetData(in, "output", 0, &err);
    if (err)
        output = "";

    const char *input = vsapi->mapGetData(in, "input", 0, &err);
    if (err)
        input = "";

    const char *tfmIn = vsapi->mapGetData(in, "tfmIn", 0, &err);
    if (err)
        tfmIn = "";

    const char *mkvOut = vsapi->mapGetData(in, "mkvOut", 0, &err);
    if (err)
        mkvOut = "";

    int nt = vsh::int64ToIntS(vsapi->mapGetInt(in, "nt", 0, &err));
    if (err)
        nt = 0;

    int blockx = vsh::int64ToIntS(vsapi->mapGetInt(in, "blockx", 0, &err));
    if (err)
        blockx = 32;

    int blocky = vsh::int64ToIntS(vsapi->mapGetInt(in, "blocky", 0, &err));
    if (err)
        blocky = 32;

    bool debug = !!vsapi->mapGetInt(in, "debug", 0, &err);
    if (err)
        debug = false;

    bool display = !!vsapi->mapGetInt(in, "display", 0, &err);
    if (err)
        display = false;

    int vfrDec = vsh::int64ToIntS(vsapi->mapGetInt(in, "vfrDec", 0, &err));
    if (err)
        vfrDec = 1;

    bool batch = !!vsapi->mapGetInt(in, "batch", 0, &err);
    if (err)
        batch = false;

    bool tcfv1 = !!vsapi->mapGetInt(in, "tcfv1", 0, &err);
    if (err)
        tcfv1 = true;

    bool se = !!vsapi->mapGetInt(in, "se", 0, &err);
    if (err)
        se = false;

    bool exPP = !!vsapi->mapGetInt(in, "exPP", 0, &err);
    if (err)
        exPP = false;

    int maxndl = vsh::int64ToIntS(vsapi->mapGetInt(in, "maxndl", 0, &err));
    if (err)
        maxndl = -200;

    bool m2PA = !!vsapi->mapGetInt(in, "m2PA", 0, &err);
    if (err)
        m2PA = false;

    bool denoise = !!vsapi->mapGetInt(in, "denoise", 0, &err);
    if (err)
        denoise = false;

    bool noblend = !!vsapi->mapGetInt(in, "noblend", 0, &err);
    if (err)
        noblend = true;

    bool ssd = !!vsapi->mapGetInt(in, "ssd", 0, &err);
    if (err)
        ssd = false;

    bool hint = !!vsapi->mapGetInt(in, "hint", 0, &err);
    if (err)
        hint = true;

    VSNode *clip2 = vsapi->mapGetNode(in, "clip2", 0, &err);
    if (err)
        clip2 = vsapi->addNodeRef(clip); // simplifies the code in the getframe functions

    int sdlim = vsh::int64ToIntS(vsapi->mapGetInt(in, "sdlim", 0, &err));
    if (err)
        sdlim = 0;

    int opt = vsh::int64ToIntS(vsapi->mapGetInt(in, "opt", 0, &err));
    if (err)
        opt = 4;

    const char *orgOut = vsapi->mapGetData(in, "orgOut", 0, &err);
    if (err)
        orgOut = "";


    TDecimate *tdecimate_data;

    try {
        tdecimate_data = new TDecimate(clip, mode, cycleR, cycle, rate, dupThresh, vidThresh, sceneThresh, hybrid, vidDetect, conCycle, conCycleTP, ovr, output, input, tfmIn, mkvOut, nt, blockx, blocky, debug, display, vfrDec, batch, tcfv1, se, chroma, exPP, maxndl, m2PA, denoise, noblend, ssd, hint, clip2, sdlim, opt, orgOut, vsapi, core);
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
        fmParallelRequests, // mode 4: serialized production - calcMetric() shares the member `diff` scratch buffer
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
    vspapi->configPlugin("com.nodame.tivtc", "tivtc", "Field matching and decimation", VS_MAKE_VERSION(3, 5), VAPOURSYNTH_API_VERSION, 0, plugin);
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
                 "opt:int:opt;"
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
                 "opt:int:opt;"
                 "orgOut:data:opt;"
                 , "clip:vnode;", tdecimateCreate, nullptr, plugin);
}
