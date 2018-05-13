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

// Fulup need to be cleanup with new controller version
extern Lua2cWrapperT Lua2cWrap;

STATIC int ProcessOneStream(CtlSourceT *source, json_object *streamJ, AlsaSndStreamT *stream) {
    int error;
    json_object *paramsJ = NULL;

    // Make sure default runs
    stream->volume = ALSA_DEFAULT_PCM_VOLUME;
    stream->mute = 0;

    error = wrap_json_unpack(streamJ, "{ss,ss,s?i,s?b,s?o !}", "uid", &stream->uid, "zone", &stream->zone, "volume", &stream->volume
            , "mute", stream->mute, "params", &paramsJ);
    if (error) {
        AFB_ApiNotice(source->api, "ProcessOneStream missing 'uid|zone|volume|rate|mute|params' stream=%s", json_object_get_string(streamJ));
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

CTLP_LUA2C(snd_streams, source, argsJ, responseJ) {
    AlsaSndStreamT *sndStream;
    int error;
    long value;
    size_t count;

    // assert static/global softmixer handle get requited info
    AlsaSndLoopT *ctlLoop = Softmixer->loopCtl;
    if (!ctlLoop) {
        AFB_ApiError(source->api, "L2C:sndstreams: No Loop found [should register snd_loop first]");
        goto OnErrorExit;
    }

    switch (json_object_get_type(argsJ)) {
        case json_type_object:
            count = 1;
            sndStream = calloc(count + 1, sizeof (AlsaSndStreamT));
            error = ProcessOneStream(source, argsJ, &sndStream[0]);
            if (error) {
                AFB_ApiError(source->api, "L2C:sndstreams: invalid stream= %s", json_object_get_string(argsJ));
                goto OnErrorExit;
            }
            break;

        case json_type_array:
            count = json_object_array_length(argsJ);
            sndStream = calloc(count + 1, sizeof (AlsaSndStreamT));
            for (int idx = 0; idx < count; idx++) {
                json_object *sndStreamJ = json_object_array_get_idx(argsJ, idx);
                error = ProcessOneStream(source, sndStreamJ, &sndStream[idx]);
                if (error) {
                    AFB_ApiError(source->api, "sndstreams: invalid stream= %s", json_object_get_string(sndStreamJ));
                    goto OnErrorExit;
                }
            }
            break;
        default:
            AFB_ApiError(source->api, "L2C:sndstreams: invalid argsJ=  %s", json_object_get_string(argsJ));
            goto OnErrorExit;
    }


    // return stream data to application as a json array
    *responseJ = json_object_new_array();

    for (int idx = 0; sndStream[idx].uid != NULL; idx++) {
        json_object *streamJ, *paramsJ;

        // Search for a free loop capture device
        AFB_ApiNotice(source->api, "L2C:sndstreams stream=%s Start", (char*) sndStream[idx].uid);
        ctlLoop->scount--;
        if (ctlLoop->scount < 0) {
            AFB_ApiError(source->api, "L2C:sndstreams no more subdev avaliable in loopback=%s", ctlLoop->uid);
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

        // Register capture PCM for active/pause event
        if (captureDev->numid) {
            error = AlsaCtlRegister(source, captureDev, captureDev->numid);
            if (error) goto OnErrorExit;
        }

        // Try to create/setup volume control.
        snd_ctl_t* ctlDev = AlsaCrlFromPcm(source, captureDev->handle);
        if (!ctlDev) {
            AFB_ApiError(source->api, "AlsaCtlRegister [pcm=%s] fail attache sndcard", captureDev->cardid);
            goto OnErrorExit;
        }

        // create mute control and register it as pause/resume ctl)
        char runName[ALSA_CARDID_MAX_LEN];
        snprintf(runName, sizeof (runName), "run-%s", sndStream[idx].uid);

        // create a single boolean value control for pause/resume
        int runNumid = AlsaCtlCreateControl(source, ctlDev, playbackDev, runName, 1, 0, 1, 1, !sndStream[idx].mute);
        if (runNumid <= 0) goto OnErrorExit;

        // register mute/unmute as a pause/resume control
        error = AlsaCtlRegister(source, captureDev, runNumid);
        if (error) goto OnErrorExit;

        // create stream and delay pcm openning until vol control is created
        char volName[ALSA_CARDID_MAX_LEN];
        snprintf(volName, sizeof (volName), "vol-%s", sndStream[idx].uid);
        AlsaPcmInfoT *streamPcm = AlsaCreateStream(source, &sndStream[idx], captureDev, volName, 0);
        if (!streamPcm) {
            AFB_ApiError(source->api, "L2C:sndstreams:%s(pcm) fail to create stream", sndStream[idx].uid);
            goto OnErrorExit;
        }

        // create volume control before softvol pcm is opened
        int volNumid = AlsaCtlCreateControl(source, ctlDev, playbackDev, volName, streamPcm->params.channels, 0, 100, 1, sndStream[idx].volume);
        if (volNumid <= 0) goto OnErrorExit;

        //        **** Fulup (would need some help to get automatic rate converter to work).         
        //        // add a rate converter plugin to match stream params config
        //        char rateName[ALSA_CARDID_MAX_LEN];
        //        snprintf(rateName, sizeof (rateName), "rate-%s", sndStream[idx].uid);
        //        AlsaPcmInfoT *ratePcm= AlsaCreateRate(source, rateName, streamPcm, 1);
        //        if (!ratePcm) {
        //            AFB_ApiError(source->api, "L2C:sndstreams:%s(pcm) fail to create rate converter", sndStream[idx].uid);
        //            goto OnErrorExit;
        //        }

        // everything is not ready to open capture pcm
        error = snd_pcm_open(&streamPcm->handle, sndStream[idx].uid, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
        if (error) {
            AFB_ApiError(source->api, "L2C:sndstreams:%s(pcm) fail to open capture", sndStream[idx].uid);
            goto OnErrorExit;
        }

        // capture stream inherit channel from targeted zone
        captureDev->ccount = streamPcm->ccount;
        streamPcm->params.channels = streamPcm->ccount;

        // start stream pcm copy (at this both capture & sink pcm should be open)
        error = AlsaPcmCopy(source, captureDev, streamPcm, &streamPcm->params);
        if (error) goto OnErrorExit;

        // retrieve active/pause control and set PCM status accordingly 
        error = AlsaCtlNumidGetLong(source, ctlDev, captureDev->numid, &value);
        if (error) goto OnErrorExit;

        // toggle pause/resume (should be done after pcm_start)
        if ((error = snd_pcm_pause(captureDev->handle, !value)) < 0) {
            AFB_ApiError(source->api, "L2C:sndstreams [capture=%s] fail to pause error=%s", captureDev->cardid, snd_strerror(error));
            goto OnErrorExit;
        }

        // prepare response for application
        playbackDev->device = ctlLoop->playback;
        error = AlsaByPathDevid(source, playbackDev);

        error += wrap_json_pack(&paramsJ, "{si si si si}", "rate", streamPcm->params.rate, "channels", streamPcm->params.channels, "format", streamPcm->params.format, "access", streamPcm->params.access);
        error += wrap_json_pack(&streamJ, "{ss ss si si so}", "uid", streamPcm->uid, "alsa", playbackDev->cardid, "volid", volNumid, "runid", runNumid, "params", paramsJ);
        error += json_object_array_add(*responseJ, streamJ);
        if (error) {
            AFB_ApiError(source->api, "L2C:sndstreams:%s(stream) fail to prepare response", captureDev->cardid);
            goto OnErrorExit;
        }

        snd_ctl_close(ctlDev);
        // Debug Alsa Config 
        //AlsaDumpElemConfig (source, "\n\nAlsa_Config\n------------\n", "pcm");
        //AlsaDumpPcmInfo(source, "\n\nPcm_config\n-----------\n", streamPcm->handle);

        AFB_ApiNotice(source->api, "L2C:sndstreams:%s(stream) OK reponse=%s\n", streamPcm->uid, json_object_get_string(streamJ));
    }

    return 0;

OnErrorExit:
    return -1;
}