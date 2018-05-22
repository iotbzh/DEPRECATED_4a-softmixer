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

// Fulup need to be cleanup with new controller version
extern Lua2cWrapperT Lua2cWrap;



STATIC int ProcessOneChannel(CtlSourceT *source, const char* uid, json_object *channelJ, AlsaPcmChannelT *channel) {
    const char*channelUid;

    int error = wrap_json_unpack(channelJ, "{ss,si !}", "uid", &channelUid, "port", &channel->port);
    if (error) goto OnErrorExit;

    channel->uid = strdup(channelUid);
    return 0;

OnErrorExit:
    AFB_ApiError(source->api, "ProcessOneChannel: sndcard=%s channel: missing (uid||port) json=%s", uid, json_object_get_string(channelJ));
    return -1;
}

PUBLIC int ProcessSndParams(CtlSourceT *source, const char* uid, json_object *paramsJ, AlsaPcmHwInfoT *params) {
    const char *format = NULL, *access = NULL;

    // some default values
    params->rate = ALSA_DEFAULT_PCM_RATE;
    params->channels = 2;
    params->sampleSize = 0;

    int error = wrap_json_unpack(paramsJ, "{s?i,s?i, s?s, s?s !}", "rate", &params->rate, "channels", &params->channels, "format", &format, "access", &access);
    if (error) goto OnErrorExit;

    if (!format) params->format = SND_PCM_FORMAT_S16_LE;
    else if (!strcasecmp(format, "S16_LE")) params->format = SND_PCM_FORMAT_S16_LE;
    else if (!strcasecmp(format, "S16_BE")) params->format = SND_PCM_FORMAT_S16_BE;
    else if (!strcasecmp(format, "U16_LE")) params->format = SND_PCM_FORMAT_U16_LE;
    else if (!strcasecmp(format, "U16_BE")) params->format = SND_PCM_FORMAT_U16_BE;
    else if (!strcasecmp(format, "S32_LE")) params->format = SND_PCM_FORMAT_S32_LE;
    else if (!strcasecmp(format, "S32_BE")) params->format = SND_PCM_FORMAT_S32_BE;
    else if (!strcasecmp(format, "U32_LE")) params->format = SND_PCM_FORMAT_U32_LE;
    else if (!strcasecmp(format, "U32_BE")) params->format = SND_PCM_FORMAT_U32_BE;
    else if (!strcasecmp(format, "S24_LE")) params->format = SND_PCM_FORMAT_S24_LE;
    else if (!strcasecmp(format, "S24_BE")) params->format = SND_PCM_FORMAT_S24_BE;
    else if (!strcasecmp(format, "U24_LE")) params->format = SND_PCM_FORMAT_U24_LE;
    else if (!strcasecmp(format, "U24_BE")) params->format = SND_PCM_FORMAT_U24_BE;
    else if (!strcasecmp(format, "S8")) params->format = SND_PCM_FORMAT_S8;
    else if (!strcasecmp(format, "U8")) params->format = SND_PCM_FORMAT_U8;
    else if (!strcasecmp(format, "FLOAT_LE")) params->format = SND_PCM_FORMAT_FLOAT_LE;
    else if (!strcasecmp(format, "FLOAT_BE")) params->format = SND_PCM_FORMAT_FLOAT_LE;
    else {
        AFB_ApiNotice(source->api, "ProcessSndParams:%s(params) unsupported format 'S16_LE|S32_L|...' format=%s", uid, format);
        goto OnErrorExit;
    }

    if (!access) params->access = SND_PCM_ACCESS_RW_INTERLEAVED;
    else if (!strcasecmp(access, "MMAP_INTERLEAVED")) params->access = SND_PCM_ACCESS_MMAP_INTERLEAVED;
    else if (!strcasecmp(access, "MMAP_NONINTERLEAVED")) params->access = SND_PCM_ACCESS_MMAP_NONINTERLEAVED;
    else if (!strcasecmp(access, "MMAP_COMPLEX")) params->access = SND_PCM_ACCESS_MMAP_COMPLEX;
    else if (!strcasecmp(access, "RW_INTERLEAVED")) params->access = SND_PCM_ACCESS_RW_INTERLEAVED;
    else if (!strcasecmp(access, "RW_NONINTERLEAVED")) params->access = SND_PCM_ACCESS_RW_NONINTERLEAVED;

    else {
        AFB_ApiNotice(source->api, "ProcessSndParams:%s(params) unsupported access 'RW_INTERLEAVED|MMAP_INTERLEAVED|MMAP_COMPLEX' access=%s", uid, access);
        goto OnErrorExit;
    }

    return 0;

OnErrorExit:
    return -1;
}

STATIC int ProcessOneSndCard(CtlSourceT *source, json_object *sndcardJ, AlsaPcmInfoT *snd) {
    json_object *sinkJ = NULL, *paramsJ = NULL;
    int error;

    error = wrap_json_unpack(sndcardJ, "{ss,s?s,s?s,s?i,s?i,s?i,so,s?o !}"
            , "uid", &snd->uid
            , "devpath", &snd->devpath
            , "cardid", &snd->cardid
            , "cardidx", &snd->cardidx
            , "device", &snd->device
            , "subdev", &snd->subdev
            , "sink", &sinkJ
            , "params", &paramsJ
            );
    if (error || !snd->uid || !sinkJ || (!snd->devpath && !snd->cardid && snd->cardidx)) {
        AFB_ApiNotice(source->api, "ProcessOneSndCard missing 'uid|path|cardid|cardidx|channels|device|subdev|numid|params' devin=%s", json_object_get_string(sndcardJ));
        goto OnErrorExit;
    }

    if (paramsJ) {
        error = ProcessSndParams(source, snd->uid, paramsJ, &snd->params);
        if (error) {
            AFB_ApiError(source->api, "ProcessOneSndCard: sndcard=%s invalid params=%s", snd->uid, json_object_get_string(paramsJ));
            goto OnErrorExit;
        }
    } else {
        snd->params.rate = ALSA_DEFAULT_PCM_RATE;
        snd->params.access = SND_PCM_ACCESS_RW_INTERLEAVED;
        snd->params.format = SND_PCM_FORMAT_S16_LE;
        snd->params.channels = 2;
    }

    // check snd card is accessible
    error = AlsaByPathDevid(source, snd);
    if (error) {
        AFB_ApiError(source->api, "ProcessOneSndCard: sndcard=%s not found config=%s", snd->uid, json_object_get_string(sndcardJ));
        goto OnErrorExit;
    }

    // protect each sndcard with a dmix plugin to enable audio-stream mixing
    char dmixUid[100];
    snprintf(dmixUid, sizeof (dmixUid), "Dmix-%s", snd->uid);
    AlsaPcmInfoT *dmixPcm = AlsaCreateDmix(source, dmixUid, snd, 0);
    if (!dmixPcm) {
        AFB_ApiError(source->api, "ProcessOneSndCard: sndcard=%s fail to attach dmix plugin", snd->uid);
        goto OnErrorExit;
    } else {
        snd->cardid = dmixPcm->cardid;
    }

    switch (json_object_get_type(sinkJ)) {
        case json_type_object:
            snd->ccount = 1;
            snd->channels = calloc(snd->ccount + 1, sizeof (AlsaPcmChannelT));
            error = ProcessOneChannel(source, snd->uid, sndcardJ, &snd->channels[0]);
            if (error) goto OnErrorExit;
            break;
        case json_type_array:
            snd->ccount = json_object_array_length(sinkJ);
            snd->channels = calloc(snd->ccount + 1, sizeof (AlsaPcmChannelT));
            for (int idx = 0; idx < snd->ccount; idx++) {
                json_object *channelJ = json_object_array_get_idx(sinkJ, idx);
                error = ProcessOneChannel(source, snd->uid, channelJ, &snd->channels[idx]);
                if (error) goto OnErrorExit;
            }
            break;
        default:
            AFB_ApiError(source->api, "ProcessOneSndCard:%s invalid sink=%s", snd->uid, json_object_get_string(sinkJ));
            goto OnErrorExit;
    }

    return 0;

OnErrorExit:
    return -1;
}

PUBLIC int SndBackend(CtlSourceT *source, json_object *argsJ) {
    SoftMixerHandleT *mixerHandle = (SoftMixerHandleT*) source->context;
    int error;
    size_t count;

    assert(mixerHandle);

    if (mixerHandle->backend) {
        AFB_ApiError(source->api, "SndBackend: mixer=%s backend already declared  %s", mixerHandle->uid, json_object_get_string(argsJ));
        goto OnErrorExit;
    }

    switch (json_object_get_type(argsJ)) {
        case json_type_object:
            count = 1;
            mixerHandle->backend = calloc(count + 1, sizeof (AlsaPcmInfoT));
            error = ProcessOneSndCard(source, argsJ, &mixerHandle->backend[0]);
            if (error) goto OnErrorExit;
            break;
        case json_type_array:
            count = json_object_array_length(argsJ);
            mixerHandle->backend = calloc(count + 1, sizeof (AlsaPcmInfoT));
            for (int idx = 0; idx < count; idx++) {
                json_object *sndcardJ = json_object_array_get_idx(argsJ, idx);
                error = ProcessOneSndCard(source, sndcardJ, &mixerHandle->backend[idx]);
                if (error) goto OnErrorExit;
            }
            break;
        default:
            AFB_ApiError(source->api, "SndBackend: mixer=%s invalid argsJ=  %s", mixerHandle->uid, json_object_get_string(argsJ));
            goto OnErrorExit;
    }


    if (count == 1) {
        // only one sound card we multi would be useless
        mixerHandle->multiPcm = &mixerHandle->backend[0];

    } else {

        // instantiate an alsa multi plugin
        mixerHandle->multiPcm = AlsaCreateMulti(source, "PcmMulti", 0);
        if (!mixerHandle->multiPcm) goto OnErrorExit;

    }
    return 0;

OnErrorExit:
    AFB_ApiNotice(source->api, "SndBackend mixer=%s fail to process: %s", mixerHandle->uid, json_object_get_string(argsJ));
    return -1;
}
