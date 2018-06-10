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
#include <math.h>

// move from vol % to absolute value
#define CONVERT_RANGE(val, min, max) ceil((val) * ((max) - (min)) * 0.01 + (min))
#define CONVERT_VOLUME(val, min, max) (int) CONVERT_RANGE ((double)val, (double)min, (double)max)

// move from volume to percentage (extract from alsa-utils)

STATIC int CONVERT_PERCENT(long val, long min, long max) {
    long range = max - min;
    int tmp;
    if (range == 0)
        return 0;
    val -= min;
    tmp = (int) rint((double) val / (double) range * 100);
    return tmp;
}

typedef enum {
    RVOL_ABS,
    RVOL_ADD,
    RVOL_DEL,

    RVOL_NONE
} volumeT;

typedef struct {
    const char *uid;
    SoftMixerT *mixer;
    AlsaSndPcmT* pcm;
} apiVerbHandleT;

STATIC AlsaPcmChannelT *ProcessOneChannel(SoftMixerT *mixer, const char *uid, json_object *argsJ) {
    AlsaPcmChannelT *channel = calloc(1, sizeof (AlsaPcmChannelT));
    int error = wrap_json_unpack(argsJ, "{ss,si !}", "uid", &channel->uid, "port", &channel->port);
    if (error) goto OnErrorExit;

    channel->uid = strdup(channel->uid);
    return channel;

OnErrorExit:
    AFB_ApiError(mixer->api, "ProcessOneChannel: sndcard=%s channel: missing (uid||port) error=%s json=%s", uid, wrap_json_get_error_string(error), json_object_get_string(argsJ));
    free(channel);
    return NULL;
}

STATIC int PcmAttachOneCtl(SoftMixerT *mixer, AlsaSndCtlT *sndcard, json_object *argsJ, AlsaSndControlT *control) {
    snd_ctl_elem_id_t* elemId = NULL;
    snd_ctl_elem_info_t *elemInfo;
    int numid = 0;
    long value = ALSA_DEFAULT_PCM_VOLUME;
    const char *name;


    int error = wrap_json_unpack(argsJ, "{s?i,s?s,s?i !}"
            , "numid", &numid
            , "name", &name
            , "value", &value
            );
    if (error || (!numid && !name)) {
        AFB_ApiError(mixer->api, "PcmAttachOneCtl: cardid=%s channel: missing (numid|name|value) error=%s json=%s", sndcard->cid.name, wrap_json_get_error_string(error), json_object_get_string(argsJ));
        goto OnErrorExit;
    }

    if (numid > 0) {
        elemId = AlsaCtlGetNumidElemId(mixer, sndcard, numid);
        if (!elemId) {
            AFB_ApiError(mixer->api, "PcmAttachOneCtl sndard=%s fail to find control numid=%d", sndcard->cid.cardid, numid);
            goto OnErrorExit;
        }

    } else {
        elemId = AlsaCtlGetNameElemId(mixer, sndcard, name);
        if (!elemId) {
            AFB_ApiError(mixer->api, "PcmAttachOneCtl sndard=%s fail to find control name=%s", sndcard->cid.cardid, name);
            goto OnErrorExit;
        }
    }

    snd_ctl_elem_info_alloca(&elemInfo);
    snd_ctl_elem_info_set_id(elemInfo, elemId);
    control->name = strdup(snd_ctl_elem_info_get_name(elemInfo));
    control->numid = snd_ctl_elem_info_get_numid(elemInfo);

    if (snd_ctl_elem_info(sndcard->ctl, elemInfo) < 0) {
        AFB_ApiError(mixer->api, "PcmAttachOneCtl: sndard=%s numid=%d name='%s' not loadable", sndcard->cid.cardid, control->numid, control->name);
        goto OnErrorExit;
    }

    if (!snd_ctl_elem_info_is_writable(elemInfo)) {
        AFB_ApiError(mixer->api, "PcmAttachOneCtl: sndard=%s numid=%d name='%s' not writable", sndcard->cid.cardid, control->numid, control->name);
        goto OnErrorExit;
    }

    control->count = snd_ctl_elem_info_get_count(elemInfo);
    switch (snd_ctl_elem_info_get_type(elemInfo)) {
        case SND_CTL_ELEM_TYPE_BOOLEAN:
            control->min = 0;
            control->max = 1;
            control->step = 0;
            error = CtlElemIdSetLong(mixer, sndcard, elemId, value);
            break;

        case SND_CTL_ELEM_TYPE_INTEGER:
        case SND_CTL_ELEM_TYPE_INTEGER64:
            control->min = snd_ctl_elem_info_get_min(elemInfo);
            control->max = snd_ctl_elem_info_get_max(elemInfo);
            control->step = snd_ctl_elem_info_get_step(elemInfo);
            error = CtlElemIdSetLong(mixer, sndcard, elemId, (int) CONVERT_VOLUME(value, control->min, control->max));
            break;

        default:
            AFB_ApiError(mixer->api, "PcmAttachOneCtl: sndard=%s numid=%d name='%s' invalid/unsupported type=%d", sndcard->cid.cardid, control->numid, control->name, snd_ctl_elem_info_get_type(elemInfo));
            goto OnErrorExit;
    }

    if (error) {
        AFB_ApiError(mixer->api, "PcmAttachOneCtl: sndard=%s numid=%d name='%s' not writable", sndcard->cid.cardid, control->numid, control->name);
        goto OnErrorExit;
    }

    free(elemId);

    return 0;

OnErrorExit:
    if (elemId)free(elemId);
    return -1;
}

STATIC int PcmSetControl(SoftMixerT *mixer, AlsaSndCtlT *sndcard, AlsaSndControlT *control, volumeT volType, int *newvol, int *oldval) {
    snd_ctl_elem_id_t* elemId = NULL;
    snd_ctl_elem_info_t *elemInfo;
    int error, value;
    long curval;

    assert(control->numid);

    elemId = AlsaCtlGetNumidElemId(mixer, sndcard, control->numid);
    if (!elemId) {
        AFB_ApiError(mixer->api, "PcmSetControl sndard=%s fail to find control numid=%d", sndcard->cid.cardid, control->numid);
        goto OnErrorExit;
    }

    snd_ctl_elem_info_alloca(&elemInfo);
    snd_ctl_elem_info_set_id(elemInfo, elemId);

    if (snd_ctl_elem_info(sndcard->ctl, elemInfo) < 0) {
        AFB_ApiError(mixer->api, "PcmSetControl: sndard=%s numid=%d name='%s' not loadable", sndcard->cid.cardid, control->numid, control->name);
        goto OnErrorExit;
    }

    if (!snd_ctl_elem_info_is_writable(elemInfo)) {
        AFB_ApiError(mixer->api, "PcmSetControl: sndard=%s numid=%d name='%s' not writable", sndcard->cid.cardid, control->numid, control->name);
        goto OnErrorExit;
    }

    error = CtlElemIdGetLong(mixer, sndcard, elemId, &curval);
    if (error) {
        AFB_ApiError(mixer->api, "PcmSetControl sndard=%s fail to read control numid=%d", sndcard->cid.cardid, control->numid);
        goto OnErrorExit;
    }

    switch (snd_ctl_elem_info_get_type(elemInfo)) {

        case SND_CTL_ELEM_TYPE_BOOLEAN:
            error = CtlElemIdSetLong(mixer, sndcard, elemId, *newvol);
            break;

        case SND_CTL_ELEM_TYPE_INTEGER:
        case SND_CTL_ELEM_TYPE_INTEGER64:

            switch (volType) {
                case RVOL_ADD:
                    value = CONVERT_PERCENT(curval, control->min, control->max) + *newvol;
                    break;
                case RVOL_DEL:
                    value = CONVERT_PERCENT(curval, control->min, control->max) - *newvol;
                    break;
                default:
                    value = *newvol;
            }

            error = CtlElemIdSetLong(mixer, sndcard, elemId, CONVERT_VOLUME(value, control->min, control->max));
            if (error) {
                AFB_ApiError(mixer->api, "PcmSetControl sndard=%s fail to write control numid=%d value=%d", sndcard->cid.cardid, control->numid, value);
                goto OnErrorExit;
            }
            break;

        default:
            AFB_ApiError(mixer->api, "PcmSetControl: sndard=%s numid=%d name='%s' invalid/unsupported type=%d", sndcard->cid.cardid, control->numid, control->name, snd_ctl_elem_info_get_type(elemInfo));
            goto OnErrorExit;
    }

    if (error) {
        AFB_ApiError(mixer->api, "PcmSetControl: sndard=%s numid=%d name='%s' not writable", sndcard->cid.cardid, control->numid, control->name);
        goto OnErrorExit;
    }

    *oldval = CONVERT_PERCENT(curval, control->min, control->max);
    *newvol = value;
    free(elemId);
    return 0;

OnErrorExit:
    if (elemId)free(elemId);
    return -1;
}

STATIC void ApiPcmVerbCB(AFB_ReqT request) {
    apiVerbHandleT *handle = (apiVerbHandleT*) afb_request_get_vcbdata(request);
    int error, verbose = 0, doInfo = 0, doToggle = 0, doMute = -1;
    json_object *volumeJ = NULL;
    json_object *responseJ = NULL;
    json_object *argsJ = afb_request_json(request);

    SoftMixerT *mixer = handle->mixer;
    AlsaSndCtlT *sndcard = handle->pcm->sndcard;
    assert(mixer && sndcard);

    error = wrap_json_unpack(argsJ, "{s?b,s?b,s?b,s?b,s?o !}"
            , "verbose", &verbose
            , "info", &doInfo
            , "mute", &doMute
            , "toggle", &doToggle
            , "volume", &volumeJ
            );
    if (error) {
        AFB_ReqFailF(request, "syntax-error", "Missing 'mute|volume|toggle|quiet' args=%s error=%s", json_object_get_string(argsJ), wrap_json_get_error_string(error));
        goto OnErrorExit;
    }

    if (verbose) responseJ=json_object_new_object();
    
    if (doMute != -1) {
        int mute = (int) doMute;

        error += AlsaCtlNumidSetLong(mixer, sndcard, handle->pcm->mute.numid, mute);
        if (error) {
            AFB_ReqFailF(request, "invalid-numid", "Fail to set pause numid=%d", handle->pcm->mute.numid);
            goto OnErrorExit;
        }

        if (verbose) {
            json_object_object_add(responseJ, "mute", json_object_new_boolean((json_bool) mute));
        }
    }

    if (doToggle) {
        long mute;

        error += AlsaCtlNumidGetLong(mixer, handle->pcm->sndcard, handle->pcm->mute.numid, &mute);
        error += AlsaCtlNumidSetLong(mixer, handle->pcm->sndcard, handle->pcm->mute.numid, !mute);
        if (error) {
            AFB_ReqFailF(request, "invalid-numid", "Fail to toogle pause numid=%d", handle->pcm->mute.numid);
            goto OnErrorExit;
        }

        if (verbose) {
            json_object_object_add(responseJ, "mute", json_object_new_boolean((json_bool)!mute));
        }
    }

    if (volumeJ) {
        volumeT volType;

        int newvol, oldvol;
        const char*volString;

        switch (json_object_get_type(volumeJ)) {
            case json_type_string:
                volString = json_object_get_string(volumeJ);
                switch (volString[0]) {
                    case '+':
                        sscanf(&volString[1], "%d", &newvol);
                        volType = RVOL_ADD;
                        break;

                    case '-':
                        sscanf(&volString[1], "%d", &newvol);
                        volType = RVOL_DEL;
                        break;
                    default:
                        error = sscanf(&volString[0], "%d", &newvol);
                        volType = RVOL_ABS;
                        if (error != 1) {
                            AFB_ReqFailF(request, "not-integer", "relative volume should start by '+|-' value=%s", json_object_get_string(volumeJ));
                            goto OnErrorExit;
                        }
                }
                break;
            case json_type_int:
                volType = RVOL_ABS;
                newvol = json_object_get_int(volumeJ);
                break;
            default:
                AFB_ReqFailF(request, "not-integer", "volume should be string or integer value=%s", json_object_get_string(volumeJ));
                goto OnErrorExit;

        }

        error = PcmSetControl(mixer, handle->pcm->sndcard, &handle->pcm->volume, volType, &newvol, &oldvol);
        if (error) {
            AFB_ReqFailF(request, "invalid-ctl", "Fail to set volume hal=%s card=%s numid=%d name=%s value=%d"
                    , handle->uid, handle->pcm->sndcard->cid.cardid, handle->pcm->volume.numid, handle->pcm->volume.name, newvol);
            goto OnErrorExit;
        }
        
        if (verbose) {
            json_object_object_add(responseJ, "volnew", json_object_new_int(newvol));
            json_object_object_add(responseJ, "volold", json_object_new_int(oldvol));
        }
    }

    AFB_ReqSuccess(request, responseJ, handle->uid);
    return;

OnErrorExit:
    return;
}

PUBLIC AlsaPcmHwInfoT * ApiPcmSetParams(SoftMixerT *mixer, const char *uid, json_object * paramsJ) {
    AlsaPcmHwInfoT *params = calloc(1, sizeof (AlsaPcmHwInfoT));
    const char *format = NULL, *access = NULL;

    // some default values
    params->rate = ALSA_DEFAULT_PCM_RATE;
    params->channels = 2;
    params->sampleSize = 0;

    if (paramsJ) {
        int error = wrap_json_unpack(paramsJ, "{s?i,s?i, s?s, s?s !}", "rate", &params->rate, "channels", &params->channels, "format", &format, "access", &access);
        if (error) {
            AFB_ApiError(mixer->api, "ApiPcmSetParams: sndcard=%s invalid params=%s", uid, json_object_get_string(paramsJ));
            goto OnErrorExit;
        }
    }

    if (!format) {
        params->format = SND_PCM_FORMAT_S16_LE;
        params->formatS = "S16_LE";
        goto check_access;
    }
    params->formatS = strdup(format);
#define FORMAT_CHECK(arg) if (!strcmp(format,#arg)) { params->format = SND_PCM_FORMAT_##arg; goto check_access; }

    FORMAT_CHECK(S16_LE);
    FORMAT_CHECK(S16_BE);
    FORMAT_CHECK(U16_LE);
    FORMAT_CHECK(U16_BE);
    FORMAT_CHECK(S32_BE);
    FORMAT_CHECK(S32_LE);
    FORMAT_CHECK(U32_BE);
    FORMAT_CHECK(U32_LE);
    FORMAT_CHECK(S24_BE);
    FORMAT_CHECK(S24_LE);
    FORMAT_CHECK(U24_BE);
    FORMAT_CHECK(U24_LE);
    FORMAT_CHECK(S8);
    FORMAT_CHECK(U8);
    FORMAT_CHECK(FLOAT_LE);
    FORMAT_CHECK(FLOAT_BE);

    AFB_ApiNotice(mixer->api, "ApiPcmSetParams:%s(params) unsupported format 'S16_LE|S32_L|...' format=%s", uid, format);
    goto OnErrorExit;

check_access:
    AFB_ApiNotice(mixer->api, "ApiPcmSetParams:%s format set to SND_PCM_FORMAT_%s", uid, params->formatS);

#define ACCESS_CHECK(arg) if (!strcmp(access,#arg)) { params->access = SND_PCM_ACCESS_##arg; goto success;}

    if (!access) {
        params->access = SND_PCM_ACCESS_RW_INTERLEAVED;
        goto success;
    }

    ACCESS_CHECK(MMAP_INTERLEAVED);
    ACCESS_CHECK(MMAP_NONINTERLEAVED);
    ACCESS_CHECK(MMAP_COMPLEX);
    ACCESS_CHECK(RW_INTERLEAVED);
    ACCESS_CHECK(RW_NONINTERLEAVED);

    AFB_ApiNotice(mixer->api, "ApiPcmSetParams:%s(params) unsupported access 'RW_INTERLEAVED|MMAP_INTERLEAVED|MMAP_COMPLEX' access=%s", uid, access);
    goto OnErrorExit;

success:
    AFB_ApiNotice(mixer->api, "ApiPcmSetParams:%s access set to %s", uid, access);
    return params;

OnErrorExit:
    free(params);
    return NULL;
}

PUBLIC AlsaSndPcmT * ApiPcmAttachOne(SoftMixerT *mixer, const char *uid, snd_pcm_stream_t direction, json_object * argsJ) {
    AlsaSndPcmT *pcm = calloc(1, sizeof (AlsaSndPcmT));
    json_object *sourceJ = NULL, *paramsJ = NULL, *sinkJ = NULL, *targetJ = NULL;
    int error;

    pcm->sndcard = (AlsaSndCtlT*) calloc(1, sizeof (AlsaSndCtlT));
    error = wrap_json_unpack(argsJ, "{ss,s?s,s?s,s?i,s?i,s?o,s?o,s?o !}"
            , "uid", &pcm->uid
            , "path", &pcm->sndcard->cid.devpath
            , "cardid", &pcm->sndcard->cid.cardid
            , "device", &pcm->sndcard->cid.device
            , "subdev", &pcm->sndcard->cid.subdev
            , "sink", &sinkJ
            , "source", &sourceJ
            , "params", &paramsJ
            );
    if (error) {
        AFB_ApiError(mixer->api, "ApiPcmAttachOne: hal=%s missing 'uid|path|cardid|device|sink|source|params' error=%s args=%s", uid, wrap_json_get_error_string(error), json_object_get_string(argsJ));
        goto OnErrorExit;
    }

    // try to open sound card control interface
    pcm->sndcard->ctl = AlsaByPathOpenCtl(mixer, pcm->uid, pcm->sndcard);
    if (!pcm->sndcard->ctl) {
        AFB_ApiError(mixer->api, "ApiPcmAttachOne: hal=%s Fail to open sndcard uid=%s devpath=%s cardid=%s", uid, pcm->uid, pcm->sndcard->cid.devpath, pcm->sndcard->cid.cardid);
        goto OnErrorExit;
    }

    // check sndcard accepts params
    pcm->sndcard->params = ApiPcmSetParams(mixer, pcm->uid, paramsJ);
    if (!pcm->sndcard->params) {
        AFB_ApiError(mixer->api, "ApiPcmAttachOne: hal=%s  Fail to set params sndcard uid=%s params=%s", uid, pcm->uid, json_object_get_string(paramsJ));
        goto OnErrorExit;
    }

    if (direction == SND_PCM_STREAM_PLAYBACK) {
        if (!sinkJ) {
            AFB_ApiError(mixer->api, "ApiPcmAttachOne: hal=%s SND_PCM_STREAM_PLAYBACK require sinks args=%s", uid, json_object_get_string(argsJ));
            goto OnErrorExit;
        }
        targetJ = sinkJ;
    }

    if (direction == SND_PCM_STREAM_CAPTURE) {
        if (!sourceJ) {
            AFB_ApiError(mixer->api, "ApiPcmAttachOne: hal=%s SND_PCM_STREAM_CAPTURE require sources args=%s", uid, json_object_get_string(argsJ));
            goto OnErrorExit;
        }
        targetJ = sourceJ;

        // we may have to register SMIXER_SUBDS_CTLS per subdev (Fulup ToBeDone when sndcard get multiple device/subdev)
        pcm->sndcard->registry = calloc(SMIXER_SUBDS_CTLS + 1, sizeof (RegistryEntryPcmT));
        pcm->sndcard->rcount = SMIXER_SUBDS_CTLS;
    }

    json_object *channelsJ = NULL, *controlsJ = NULL;
    error = wrap_json_unpack(targetJ, "{so,s?o !}"
            , "channels", &channelsJ
            , "controls", &controlsJ
            );
    if (error) {
        AFB_ApiNotice(mixer->api, "ApiPcmAttachOne: hal=%s pcms missing channels|[controls] error=%s paybacks=%s", uid, wrap_json_get_error_string(error), json_object_get_string(argsJ));
        goto OnErrorExit;
    }
    if (channelsJ) {
        switch (json_object_get_type(channelsJ)) {

            case json_type_object:
                pcm->ccount = 1;
                pcm->channels = calloc(2, sizeof (void*));
                pcm->channels[0] = ProcessOneChannel(mixer, pcm->uid, channelsJ);
                if (!pcm->channels[0]) goto OnErrorExit;
                break;
            case json_type_array:
                pcm->ccount = (int) json_object_array_length(channelsJ);
                pcm->channels = calloc(pcm->ccount + 1, sizeof (void*));
                for (int idx = 0; idx < pcm->ccount; idx++) {
                    json_object *channelJ = json_object_array_get_idx(channelsJ, idx);
                    pcm->channels[idx] = ProcessOneChannel(mixer, pcm->uid, channelJ);
                    if (!pcm->channels[idx]) goto OnErrorExit;
                }
                break;
            default:
                AFB_ApiError(mixer->api, "ApiPcmAttachOne:%s invalid pcm=%s", pcm->uid, json_object_get_string(channelsJ));
                goto OnErrorExit;
        }
    }

    if (controlsJ) {
        json_object *volJ = NULL, *muteJ = NULL;
        error = wrap_json_unpack(controlsJ, "{s?o,s?o !}"
                , "volume", &volJ
                , "mute", &muteJ
                );
        if (error) {
            AFB_ApiNotice(mixer->api, "ApiPcmAttachOne: source missing [volume]|[mute] error=%s control=%s", wrap_json_get_error_string(error), json_object_get_string(controlsJ));
            goto OnErrorExit;
        }

        if (volJ) error += PcmAttachOneCtl(mixer, pcm->sndcard, volJ, &pcm->volume);
        if (muteJ) error += PcmAttachOneCtl(mixer, pcm->sndcard, muteJ, &pcm->mute);
        if (error) goto OnErrorExit;

        // create master control for this sink
        char *apiVerb, *apiInfo;
        if (direction == SND_PCM_STREAM_PLAYBACK) {
            (void) asprintf(&apiVerb, "%s:playback", pcm->uid);
            (void) asprintf(&apiInfo, "HAL:%s SND_PCM_STREAM_PLAYBACK", uid);
        } else {
            (void) asprintf(&apiVerb, "%s:capture", pcm->uid);
            (void) asprintf(&apiInfo, "HAL:%s SND_PCM_STREAM_PLAYBACK", uid);
        }     
        
        apiVerbHandleT *handle = calloc(1, sizeof (apiVerbHandleT));
        handle->uid = uid;
        handle->pcm = pcm;
        handle->mixer = mixer;
        pcm->verb=apiVerb;
        error = afb_dynapi_add_verb(mixer->api, apiVerb, apiInfo, ApiPcmVerbCB, handle, NULL, 0);
        if (error) {
            AFB_ApiError(mixer->api, "ApiPcmAttachOne mixer=%s verb=%s fail to Register Master control ", mixer->uid, apiVerb);
            goto OnErrorExit;
        }
    }

    // free useless resource and secure others
    pcm->uid = strdup(pcm->uid);

    return pcm;

OnErrorExit:
    free(pcm);
    return NULL;
}

