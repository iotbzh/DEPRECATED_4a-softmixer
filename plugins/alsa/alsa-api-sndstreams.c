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
    const char*format=NULL;

    error = wrap_json_unpack(streamJ, "{ss,ss,s?i,s?b,s?i,s?i,s?i !}", "uid", &stream->uid, "zone", &stream->zone, "volume", &stream->volume, "mute", stream->mute
            , "rate", &stream->params.rate, "format", &format, "access", &stream->params.access);
    if (error) {
        AFB_ApiNotice(source->api, "ProcessOneStream missing 'uid|zone|volume|rate|mute' stream=%s", json_object_get_string(streamJ));
        goto OnErrorExit;
    }

    if (!format) stream->params.format=SND_PCM_FORMAT_UNKNOWN;
    else if (!strcasecmp (format, "S16_LE"))  stream->params.format=SND_PCM_FORMAT_S16_LE;
    else if (!strcasecmp (format, "S16_BE"))  stream->params.format=SND_PCM_FORMAT_S16_BE;
    else if (!strcasecmp (format, "U16_LE"))  stream->params.format=SND_PCM_FORMAT_U16_LE;
    else if (!strcasecmp (format, "U16_BE"))  stream->params.format=SND_PCM_FORMAT_U16_BE;
    else if (!strcasecmp (format, "S32_LE"))  stream->params.format=SND_PCM_FORMAT_S32_LE;
    else if (!strcasecmp (format, "S32_BE"))  stream->params.format=SND_PCM_FORMAT_S32_BE;
    else if (!strcasecmp (format, "U32_LE"))  stream->params.format=SND_PCM_FORMAT_U32_LE;
    else if (!strcasecmp (format, "U32_BE"))  stream->params.format=SND_PCM_FORMAT_U32_BE;
    else if (!strcasecmp (format, "S24_LE"))  stream->params.format=SND_PCM_FORMAT_S24_LE;
    else if (!strcasecmp (format, "S24_BE"))  stream->params.format=SND_PCM_FORMAT_S24_BE;
    else if (!strcasecmp (format, "U24_LE"))  stream->params.format=SND_PCM_FORMAT_U24_LE;
    else if (!strcasecmp (format, "U24_BE"))  stream->params.format=SND_PCM_FORMAT_U24_BE;
    else if (!strcasecmp (format, "S8"))  stream->params.format=SND_PCM_FORMAT_S8;
    else if (!strcasecmp (format, "U8"))  stream->params.format=SND_PCM_FORMAT_U8;
    else if (!strcasecmp (format, "FLOAT_LE"))  stream->params.format=SND_PCM_FORMAT_FLOAT_LE;
    else if (!strcasecmp (format, "FLOAT_BE"))  stream->params.format=SND_PCM_FORMAT_FLOAT_LE;
    else {
        AFB_ApiNotice(source->api, "ProcessOneStream unsupported format 'uid|zone|volume|rate|mute' stream=%s", json_object_get_string(streamJ));
        goto OnErrorExit;        
    }    

    if(!stream->params.rate) stream->params.rate=ALSA_DEFAULT_PCM_RATE;
    
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
        json_object *streamJ;
        
        // Search for a free loop capture device
        AFB_ApiNotice(source->api, "L2C:sndstreams stream=%s Start", (char*)sndStream[idx].uid);
        ctlLoop->scount--;
        if (ctlLoop->scount < 0) {
            AFB_ApiError(source->api, "L2C:sndstreams no more subdev avaliable in loopback=%s", ctlLoop->uid);
            goto OnErrorExit;
        }

        // Retrieve subdev loop device and open corresponding pcm
        AlsaPcmInfoT *playbackDev = &ctlLoop->subdevs[ctlLoop->scount];
        
        // capture use the same card/subdev as playback with a different device
        playbackDev->device= ctlLoop->capture;
        AlsaPcmInfoT *captureDev  = AlsaByPathOpenPcm(source, playbackDev, SND_PCM_STREAM_CAPTURE);
        if (!captureDev) goto OnErrorExit;
        
        AlsaPcmInfoT *streamPcm = AlsaCreateStream(source, &sndStream[idx], captureDev);
        if (!streamPcm) {
            AFB_ApiError(source->api, "L2C:sndstreams fail to create stream=%s", (char*) sndStream[idx].uid);
            goto OnErrorExit;
        }
        
        // capture stream inherit channel from targeted zone
        captureDev->ccount = streamPcm->ccount;
        sndStream[idx].params.channels=streamPcm->ccount;
        
        // start stream pcm copy 
        error = AlsaPcmCopy(source, captureDev, streamPcm, &sndStream[idx].params);
        if (error) goto OnErrorExit;

        // Registration to event should be done after pcm_start
        if (captureDev->numid) {
            error = AlsaCtlRegister(source, captureDev, captureDev->numid);
            if (error) goto OnErrorExit;
        }
        
        // prepare response for application
        playbackDev->device= ctlLoop->playback;
        error = AlsaByPathDevid(source, playbackDev);
        wrap_json_pack(&streamJ, "{ss ss si}", "uid", sndStream[idx].uid, "alsa", playbackDev->cardid, "numid", captureDev->numid);
        json_object_array_add(*responseJ,streamJ);
        
        // Debug Alsa Config 
        //AlsaDumpElemConfig (source, "\n\nAlsa_Config\n------------\n", "pcm");
        //AlsaDumpPcmInfo(source, "\n\nPcm_config\n-----------\n", streamPcm->handle);

        AFB_ApiNotice(source->api, "L2C:sndstreams stream=%s OK\n", (char*) sndStream[idx].uid);
    }

    return 0;

OnErrorExit:
    return -1;
}