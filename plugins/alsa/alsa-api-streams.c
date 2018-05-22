/*
 * Copyright (C) 2018 "IoT.bzh"
 * Author Fulup Ar Foll <fulup@iot.bzh>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#define _GNU_SOURCE  // needed for vasprintf

#include "alsa-softmixer.h"
#include <string.h>

// Set stream volume control in %
#define VOL_CONTROL_MAX  100
#define VOL_CONTROL_MIN  0
#define VOL_CONTROL_STEP 1

// Fulup need to be cleanup with new controller version
extern Lua2cWrapperT Lua2cWrap;

typedef struct {
    const char* verb;
    AlsaLoopStreamT *stream;
    SoftMixerHandleT *mixer;
    AlsaVolRampT *ramp;
} apiHandleT;

STATIC AlsaVolRampT* RampGetByUid(CtlSourceT *source, AlsaVolRampT *ramps, const char *uid) {
    AlsaVolRampT *ramp = NULL;

    // Loop on every Registryed zone pcm and extract (cardid) from (uid)
    for (int idx = 0; ramps[idx].uid != NULL; idx++) {
        if (!strcasecmp(ramps[idx].uid, uid)) {
            ramp = &ramps[idx];
            return ramp;
        }
    }
    return NULL;
}

STATIC void StreamApiVerbCB(AFB_ReqT request) {
    int error, doClose = 0, doQuiet = 0, doToggle = 0, doMute = -1;
    long mute, volume;
    json_object *responseJ, *volumeJ = NULL, *rampJ = NULL, *argsJ = afb_request_json(request);
    apiHandleT *handle = (apiHandleT*) afb_request_get_vcbdata(request);
    snd_ctl_t *ctlDev = NULL;

    CtlSourceT *source = alloca(sizeof (CtlSourceT));
    source->uid = handle->verb;
    source->api = request->dynapi;
    source->request = NULL;
    source->context = NULL;

    error = wrap_json_unpack(argsJ, "{s?b s?b,s?b,s?b,s?o,s?o !}"
            , "quiet", &doQuiet
            , "close", &doClose
            , "mute", &doMute
            , "toggle", &doToggle
            , "volume", &volumeJ
            , "ramp", &rampJ
            );
    if (error) {
        AFB_ReqFailF(request, "StreamApiVerbCB", "Missing 'close|mute|volume|quiet' args=%s", json_object_get_string(argsJ));
        goto OnErrorExit;
    }

    ctlDev = AlsaCtlOpenCtl(source, handle->mixer->frontend->cardid);
    if (!ctlDev) {
        AFB_ReqFailF(request, "StreamApiVerbCB", "Fail to open sndcard=%s", handle->mixer->frontend->cardid);
        goto OnErrorExit;
    }

    if (doClose) {
        AFB_ReqFailF(request, "StreamApiVerbCB", "(Fulup) Close action still to be done mixer=%s", json_object_get_string(argsJ));
        goto OnErrorExit;
    }

    if (doToggle) {
        error += AlsaCtlNumidGetLong(source, ctlDev, handle->stream->mute, &mute);
        error += AlsaCtlNumidSetLong(source, ctlDev, handle->stream->mute, !mute);
    }

    if (volumeJ) {
        long curvol, newvol;
        const char*volString;

        error = AlsaCtlNumidGetLong(source, ctlDev, handle->stream->volume, &curvol);
        if (error) {
            AFB_ReqFailF(request, "invalid-numid", "Fail to set volume numid=%d value=%ld", handle->stream->volume, volume);
            goto OnErrorExit;
        }


        switch (json_object_get_type(volumeJ)) {
            case json_type_string:
                volString = json_object_get_string(volumeJ);
                switch (volString[0]) {
                    case '+':
                        sscanf(&volString[1], "%ld", &newvol);
                        newvol = curvol + newvol;
                        break;

                    case '-':
                        sscanf(&volString[1], "%ld", &newvol);
                        newvol = curvol - newvol;
                        break;
                    default:
                        AFB_ReqFailF(request, "not-integer", "relative volume should start by '+|-' value=%s", json_object_get_string(volumeJ));
                        goto OnErrorExit;
                }
                break;
            case json_type_int:
                newvol = json_object_get_int(volumeJ);
                break;
            default:
                AFB_ReqFailF(request, "not-integer", "volume should be string or integer value=%s", json_object_get_string(volumeJ));
                goto OnErrorExit;

        }

        error = AlsaCtlNumidSetLong(source, ctlDev, handle->stream->volume, newvol);
        if (error) {
            AFB_ReqFailF(request, "StreamApiVerbCB", "Fail to set stream volume numid=%d value=%ld", handle->stream->volume, newvol);
            goto OnErrorExit;
        }
    }

    if (rampJ) {       
        error = AlsaVolRampApply(source, handle->mixer->frontend, handle->stream, handle->ramp, rampJ);
        if (error) {
            AFB_ReqFailF(request, "StreamApiVerbCB", "Fail to set stream volram numid=%d value=%s", handle->stream->volume, json_object_get_string(rampJ));
            goto OnErrorExit;
        }
    }

    if (doMute != -1) {
        error = AlsaCtlNumidSetLong(source, ctlDev, handle->stream->mute, !mute);
        if (error) {
            AFB_ReqFailF(request, "StreamApiVerbCB", "Fail to set stream volume numid=%d value=%d", handle->stream->volume, !mute);
            goto OnErrorExit;
        }
    }

    // if not in quiet mode return effective selected control values
    if (doQuiet) responseJ = NULL;
    else {
        error += AlsaCtlNumidGetLong(source, ctlDev, handle->stream->volume, &volume);
        error += AlsaCtlNumidGetLong(source, ctlDev, handle->stream->mute, &mute);
        if (error) {
            AFB_ReqFailF(request, "StreamApiVerbCB", "Fail to get stream numids volume=%ld mute=%ld", volume, mute);
            goto OnErrorExit;
        }
        wrap_json_pack(&responseJ, "{si,sb}", "volume", volume, "mute", !mute);
    }

    snd_ctl_close(ctlDev);
    AFB_ReqSucess(request, responseJ, handle->verb);
    return;

OnErrorExit:
    if (ctlDev) snd_ctl_close(ctlDev);
    return;

}

STATIC int ProcessOneStream(CtlSourceT *source, json_object *streamJ, AlsaLoopStreamT *stream) {
    int error;
    json_object *paramsJ = NULL;

    // Make sure default runs
    stream->volume = ALSA_DEFAULT_PCM_VOLUME;
    stream->mute = 0;
    stream->info = NULL;

    error = wrap_json_unpack(streamJ, "{ss,s?s,ss,s?i,s?b,s?o,s?s !}"
            , "uid", &stream->uid
            , "info", &stream->info
            , "zone", &stream->zone
            , "volume", &stream->volume
            , "mute", stream->mute
            , "params", &paramsJ
            , "ramp", &stream->ramp
            );
    if (error) {
        AFB_ApiNotice(source->api, "ProcessOneStream missing 'uid|[info]|zone|[volume]|[mute]|[params]' stream=%s", json_object_get_string(streamJ));
        goto OnErrorExit;
    }

    if (paramsJ) error = ProcessSndParams(source, stream->uid, paramsJ, &stream->params);
    if (error) {
        AFB_ApiError(source->api, "ProcessOneSndCard: sndcard=%s invalid params=%s", stream->uid, json_object_get_string(paramsJ));
        goto OnErrorExit;
    } else {
        stream->params.rate = ALSA_DEFAULT_PCM_RATE;
        stream->params.rate = ALSA_DEFAULT_PCM_RATE;
        stream->params.access = SND_PCM_ACCESS_RW_INTERLEAVED;
        stream->params.format = SND_PCM_FORMAT_S16_LE;
        stream->params.channels = 2;
        stream->params.sampleSize = 0;
    }

    // make sure remain valid even when json object is removed
    stream->uid = strdup(stream->uid);
    stream->zone = strdup(stream->zone);

    return 0;

OnErrorExit:
    return -1;
}

PUBLIC int LoopStreams(CtlSourceT *source, json_object *argsJ, json_object **responseJ) {
    SoftMixerHandleT *mixer = (SoftMixerHandleT*) source->context;
    AlsaLoopStreamT *loopStream;
    int error;
    long value;
    int count;

    assert(mixer);

    // assert static/global softmixer handle get requited info
    AlsaSndLoopT *ctlLoop = mixer->frontend;
    if (!ctlLoop) {
        AFB_ApiError(source->api, "LoopStreams: mixer=%s No Loop found [should Registry snd_loop first]", mixer->uid);
        goto OnErrorExit;
    }

    switch (json_object_get_type(argsJ)) {
        case json_type_object:
            count = 1;
            loopStream = calloc(count + 1, sizeof (AlsaLoopStreamT));
            error = ProcessOneStream(source, argsJ, &loopStream[0]);
            if (error) {
                AFB_ApiError(source->api, "LoopStreams: mixer=%s invalid stream= %s", mixer->uid, json_object_get_string(argsJ));
                goto OnErrorExit;
            }
            break;

        case json_type_array:
            count = json_object_array_length(argsJ);
            loopStream = calloc(count + 1, sizeof (AlsaLoopStreamT));
            for (int idx = 0; idx < count; idx++) {
                json_object *loopStreamJ = json_object_array_get_idx(argsJ, idx);
                error = ProcessOneStream(source, loopStreamJ, &loopStream[idx]);
                if (error) {
                    AFB_ApiError(source->api, "loopstreams: mixer=%s invalid stream= %s", mixer->uid, json_object_get_string(loopStreamJ));
                    goto OnErrorExit;
                }
            }
            break;
        default:
            AFB_ApiError(source->api, "LoopStreams: mixer=%s invalid argsJ=  %s", mixer->uid, json_object_get_string(argsJ));
            goto OnErrorExit;
    }


    // return stream data to application as a json array
    *responseJ = json_object_new_array();

    for (int idx = 0; loopStream[idx].uid != NULL; idx++) {
        json_object *streamJ, *paramsJ;

        // Search for a free loop capture device
        AFB_ApiNotice(source->api, "LoopStreams: mixer=%s stream=%s Start", mixer->uid, (char*) loopStream[idx].uid);
        ctlLoop->scount--;
        if (ctlLoop->scount < 0) {
            AFB_ApiError(source->api, "LoopStreams: mixer=%s stream=%s no more subdev avaliable in loopback=%s", mixer->uid, loopStream[idx].uid, ctlLoop->uid);
            goto OnErrorExit;
        }

        // Retrieve subdev loop device and open corresponding pcm
        AlsaPcmInfoT *playbackDev = &ctlLoop->subdevs[ctlLoop->scount];

        // capture use the same card/subdev as playback with a different device
        playbackDev->device = ctlLoop->capture;
        AlsaPcmInfoT *captureDev = AlsaByPathOpenPcm(source, playbackDev, SND_PCM_STREAM_CAPTURE);
        if (!captureDev) goto OnErrorExit;

        // configure with default loopback subdev params
        error = AlsaPcmConf(source, captureDev, &playbackDev->params);
        if (error) goto OnErrorExit;

        // Registry capture PCM for active/pause event
        if (captureDev->numid) {
            error = AlsaCtlRegister(source, mixer, captureDev, FONTEND_NUMID_RUN, captureDev->numid);
            if (error) goto OnErrorExit;
        }

        // Try to create/setup volume control.
        snd_ctl_t* ctlDev = AlsaCrlFromPcm(source, captureDev->handle);
        if (!ctlDev) {
            AFB_ApiError(source->api, "LoopStreams: mixer=%s [pcm=%s] fail attache sndcard", mixer->uid, captureDev->cardid);
            goto OnErrorExit;
        }

        // create mute control and Registry it as pause/resume ctl)
        char runName[ALSA_CARDID_MAX_LEN];
        snprintf(runName, sizeof (runName), "run-%s", loopStream[idx].uid);

        // create a single boolean value control for pause/resume
        int pauseNumid = AlsaCtlCreateControl(source, ctlDev, playbackDev, runName, 1, 0, 1, 1, loopStream[idx].mute);
        if (pauseNumid <= 0) goto OnErrorExit;

        // Registry mute/unmute as a pause/resume control
        error = AlsaCtlRegister(source, mixer, captureDev, FONTEND_NUMID_PAUSE, pauseNumid);
        if (error) goto OnErrorExit;

        // create stream and delay pcm openning until vol control is created
        char volName[ALSA_CARDID_MAX_LEN];
        snprintf(volName, sizeof (volName), "vol-%s", loopStream[idx].uid);
        AlsaPcmInfoT *streamPcm = AlsaCreateSoftvol(source, &loopStream[idx], captureDev, volName, VOL_CONTROL_MAX, 0);
        if (!streamPcm) {
            AFB_ApiError(source->api, "LoopStreams: mixer=%s%s(pcm) fail to create stream", mixer->uid, loopStream[idx].uid);
            goto OnErrorExit;
        }

        // create volume control before softvol pcm is opened
        int volNumid = AlsaCtlCreateControl(source, ctlDev, playbackDev, volName, streamPcm->params.channels, VOL_CONTROL_MIN, VOL_CONTROL_MAX, VOL_CONTROL_STEP, loopStream[idx].volume);
        if (volNumid <= 0) goto OnErrorExit;

        //        **** Fulup (would need some help to get automatic rate converter to work).         
        //        // add a rate converter plugin to match stream params config
        //        char rateName[ALSA_CARDID_MAX_LEN];
        //        snprintf(rateName, sizeof (rateName), "rate-%s", loopStream[idx].uid);
        //        AlsaPcmInfoT *ratePcm= AlsaCreateRate(source, rateName, streamPcm, 1);
        //        if (!ratePcm) {
        //            AFB_ApiError(source->api, "LoopStreams: mixer=%s%s(pcm) fail to create rate converter", loopStream[idx].uid);
        //            goto OnErrorExit;
        //        }

        // everything is not ready to open capture pcm
        error = snd_pcm_open(&streamPcm->handle, loopStream[idx].uid, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
        if (error) {
            AFB_ApiError(source->api, "LoopStreams: mixer=%s%s(pcm) fail to open capture", mixer->uid, loopStream[idx].uid);
            goto OnErrorExit;
        }

        // capture stream inherit channel from targeted zone
        captureDev->ccount = streamPcm->ccount;
        streamPcm->params.channels = streamPcm->ccount;

        // start stream pcm copy (at this both capture & sink pcm should be open, we use output params to configure both in+outPCM)
        error = AlsaPcmCopy(source, &loopStream[idx], captureDev, streamPcm, &streamPcm->params);
        if (error) goto OnErrorExit;

        error = AlsaCtlRegister(source, mixer, captureDev, FONTEND_NUMID_IGNORE, volNumid);
        if (error) goto OnErrorExit;
        
        // retrieve active/pause control and set PCM status accordingly 
        error = AlsaCtlNumidGetLong(source, ctlDev, captureDev->numid, &value);
        if (error) goto OnErrorExit;

        // toggle pause/resume (should be done after pcm_start)
        if ((error = snd_pcm_pause(captureDev->handle, !value)) < 0) {
            AFB_ApiWarning(source->api, "LoopStreams: mixer=%s [capture=%s] fail to pause error=%s", mixer->uid, captureDev->cardid, snd_strerror(error));
        }

        // prepare response for application
        playbackDev->device = ctlLoop->playback;
        error = AlsaByPathDevid(source, playbackDev);

        error += wrap_json_pack(&paramsJ, "{si si si si}", "rate", streamPcm->params.rate, "channels", streamPcm->params.channels, "format", streamPcm->params.format, "access", streamPcm->params.access);
        error += wrap_json_pack(&streamJ, "{ss ss si si so}", "uid", streamPcm->uid, "alsa", playbackDev->cardid, "volid", volNumid, "runid", pauseNumid, "params", paramsJ);
        error += json_object_array_add(*responseJ, streamJ);
        if (error) {
            AFB_ApiError(source->api, "LoopStreams: mixer=%s stream=%s fail to prepare response", mixer->uid, captureDev->cardid);
            goto OnErrorExit;
        }

        // create a dedicated verb for this stream compose of mixeruid/streamuid
        apiHandleT *apiHandle = calloc(1, sizeof (apiHandleT));
        char apiVerb[128];
        error = snprintf(apiVerb, sizeof (apiVerb), "%s/%s", mixer->uid, loopStream[idx].uid);
        if (error == sizeof (apiVerb)) {
            AFB_ApiError(source->api, "LoopStreams mixer=%s fail to Registry Stream API too long %s/%s", mixer->uid, mixer->uid, loopStream[idx].uid);
            goto OnErrorExit;
        }

        // if set get stream attached volramp
        if (loopStream->ramp) {
            apiHandle->ramp = RampGetByUid(source, ctlLoop->ramps, loopStream->ramp);
            if (!apiHandle->ramp) {
                AFB_ApiError(source->api, "LoopStreams: mixer=%s%s(pcm) fail to find ramp=%s", mixer->uid, loopStream[idx].uid, loopStream->ramp);
                goto OnErrorExit;
            }         
        }

        apiHandle->mixer = mixer;
        apiHandle->stream= &loopStream[idx];
        apiHandle->verb = strdup(apiVerb);
        error = afb_dynapi_add_verb(source->api, apiHandle->verb, loopStream[idx].info, StreamApiVerbCB, apiHandle, NULL, 0);
        if (error) {
            AFB_ApiError(source->api, "LoopStreams mixer=%s fail to Registry API verb=%s", mixer->uid, apiHandle->verb);
            goto OnErrorExit;
        }

        // free temporary resources
        snd_ctl_close(ctlDev);
        loopStream[idx].volume = volNumid;
        loopStream[idx].mute = pauseNumid;

        // Debug Alsa Config 
        //AlsaDumpElemConfig (source, "\n\nAlsa_Config\n------------\n", "pcm");
        //AlsaDumpPcmInfo(source, "\n\nPcm_config\n-----------\n", streamPcm->handle);

        AFB_ApiNotice(source->api, "LoopStreams: mixer=%s stream=%s OK reponse=%s\n", mixer->uid, streamPcm->uid, json_object_get_string(streamJ));
    }

    // save handle for further use
    mixer->streams = loopStream;
    return 0;

OnErrorExit:
    return -1;
}
