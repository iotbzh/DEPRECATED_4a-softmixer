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

PUBLIC AlsaLoopSubdevT *ApiLoopFindSubdev(SoftMixerT *mixer, const char *streamUid, const char *targetUid, AlsaSndLoopT **loop) {

    // Either allocate a free loop subdev or search for a specific targetUid when specified
    if (targetUid) {
        for (int idx = 0; mixer->loops[idx]; idx++) {
            for (int jdx = 0; jdx < mixer->loops[idx]->scount; jdx++) {
                if (mixer->loops[idx]->subdevs[jdx]->uid && !strcasecmp(mixer->loops[idx]->subdevs[jdx]->uid, targetUid)) {
                    *loop = mixer->loops[idx];
                    return mixer->loops[idx]->subdevs[jdx];
                }
            }
        }
    } else {
        for (int idx = 0; mixer->loops[idx]; idx++) {
            for (int jdx = 0; mixer->loops[idx]->subdevs[jdx]; jdx++) {
                if (!mixer->loops[idx]->subdevs[jdx]->uid) {
                    mixer->loops[idx]->subdevs[jdx]->uid = streamUid;
                    *loop = mixer->loops[idx];
                    return mixer->loops[idx]->subdevs[jdx];
                }
            }
        }
    }
    return NULL;
}

STATIC AlsaLoopSubdevT *ProcessOneSubdev(SoftMixerT *mixer, AlsaSndLoopT *loop, json_object *subdevJ) {
    AlsaLoopSubdevT *subdev = calloc(1, sizeof (AlsaPcmCtlT));

    int error = wrap_json_unpack(subdevJ, "{s?s, si,si !}"
            , "uid", &subdev->uid
            , "subdev", &subdev->index
            , "numid", &subdev->numid
            );
    if (error) {
        AFB_ApiError(mixer->api, "ProcessOneSubdev: loop=%s missing (uid|subdev|numid) error=%s json=%s", loop->uid, wrap_json_get_error_string(error),json_object_get_string(subdevJ));
        goto OnErrorExit;
    }

    // subdev with no UID are dynamically attached
    if (subdev->uid) subdev->uid = strdup(subdev->uid);

    // create loop subdev entry point with cardidx+device+subdev in order to open subdev and not sndcard
    AlsaDevInfoT loopSubdev;
    loopSubdev.devpath=NULL;
    loopSubdev.cardid=NULL;
    loopSubdev.cardidx = loop->sndcard->cid.cardidx;
    loopSubdev.device = loop->capture;
    loopSubdev.subdev = subdev->index;

    // assert we may open this loopback subdev in capture mode
    AlsaPcmCtlT *pcmInfo = AlsaByPathOpenPcm(mixer, &loopSubdev, SND_PCM_STREAM_CAPTURE);
    if (!pcmInfo) goto OnErrorExit;

    // free PCM as we only open loop to assert it's a valid capture device
    snd_pcm_close(pcmInfo->handle);
    free(pcmInfo);

    return subdev;

OnErrorExit:
    return NULL;
}

STATIC AlsaSndLoopT *AttachOneLoop(SoftMixerT *mixer, const char *uid, json_object *argsJ) {
    AlsaSndLoopT *loop = calloc(1, sizeof (AlsaSndLoopT));
    json_object *subdevsJ = NULL, *devicesJ = NULL;
    int error;

    loop->sndcard = (AlsaSndCtlT*) calloc(1, sizeof (AlsaSndCtlT));
    error = wrap_json_unpack(argsJ, "{ss,s?s,s?s,so,so !}"
            , "uid", &loop->uid
            , "path", &loop->sndcard->cid.devpath
            , "cardid", &loop->sndcard->cid.cardid
            , "devices", &devicesJ
            , "subdevs", &subdevsJ
            );
    if (error || !loop->uid || !subdevsJ || (!loop->sndcard->cid.devpath && !loop->sndcard->cid.cardid)) {
        AFB_ApiNotice(mixer->api, "AttachOneLoop mixer=%s hal=%s missing 'uid|path|cardid|devices|subdevs' error=%s args=%s", mixer->uid, uid, wrap_json_get_error_string(error),json_object_get_string(argsJ));
        goto OnErrorExit;
    }

    // try to open sound card control interface
    loop->sndcard->ctl = AlsaByPathOpenCtl(mixer, loop->uid, loop->sndcard);
    if (!loop->sndcard->ctl) {
        AFB_ApiError(mixer->api, "AttachOneLoop mixer=%s hal=%s Fail open sndcard loop=%s devpath=%s cardid=%s (please check 'modprobe snd_aloop')", mixer->uid, uid, loop->uid, loop->sndcard->cid.devpath, loop->sndcard->cid.cardid);
        goto OnErrorExit;
    }

    // Default devices is payback=0 capture=1
    if (!devicesJ) {
        loop->playback = 0;
        loop->capture = 1;
    } else {
        error = wrap_json_unpack(devicesJ, "{si,si !}", "capture", &loop->capture, "playback", &loop->playback);
        if (error) {
            AFB_ApiNotice(mixer->api, "AttachOneLoop mixer=%s hal=%s Loop=%s missing 'capture|playback' error=%s devices=%s", mixer->uid, uid, loop->uid, wrap_json_get_error_string(error),json_object_get_string(devicesJ));
            goto OnErrorExit;
        }
    }

    switch (json_object_get_type(subdevsJ)) {
        case json_type_object:
            loop->scount = 1;
            loop->subdevs = calloc(2, sizeof (void*));
            loop->subdevs[0] = ProcessOneSubdev(mixer, loop, subdevsJ);
            if (!loop->subdevs[0]) goto OnErrorExit;
            break;
        case json_type_array:
            loop->scount = (int) json_object_array_length(subdevsJ);
            loop->subdevs = calloc(loop->scount + 1, sizeof (void*));
            for (int idx = 0; idx < loop->scount; idx++) {
                json_object *subdevJ = json_object_array_get_idx(subdevsJ, idx);
                loop->subdevs[idx] = ProcessOneSubdev(mixer, loop, subdevJ);
                if (!loop->subdevs[idx]) goto OnErrorExit;
            }
            break;
        default:
            AFB_ApiError(mixer->api, "AttachOneLoop mixer=%s hal=%s Loop=%s invalid subdevs= %s", mixer->uid, uid, loop->uid, json_object_get_string(subdevsJ));
            goto OnErrorExit;
    }

    // we may have to register up to 3 control per subdevice (vol, pause, actif)
    loop->sndcard->registry = calloc(loop->scount * SMIXER_SUBDS_CTLS + 1, sizeof (RegistryEntryPcmT));
    loop->sndcard->rcount = loop->scount*SMIXER_SUBDS_CTLS;

    return loop;

OnErrorExit:
    return NULL;
}

PUBLIC int ApiLoopAttach(SoftMixerT *mixer, AFB_ReqT request, const char *uid, json_object * argsJ) {

    int index;
    for (index = 0; index < mixer->max.loops; index++) {
        if (!mixer->loops[index]) break;
    }

    if (index == mixer->max.loops) {
        AFB_IfReqFailF(mixer, request, "too-small", "mixer=%s hal=%s max loop=%d", mixer->uid, uid, mixer->max.loops);
        goto OnErrorExit;
    }

    switch (json_object_get_type(argsJ)) {
            size_t count;

        case json_type_object:
            mixer->loops[index] = AttachOneLoop(mixer, uid, argsJ);
            if (!mixer->loops[index]) {
                AFB_IfReqFailF(mixer, request, "invalid-syntax", "mixer=%s hal=%s invalid loop= %s", mixer->uid, uid, json_object_get_string(argsJ));
                goto OnErrorExit;
            }
            break;

        case json_type_array:
            count = json_object_array_length(argsJ);
            if (count > (mixer->max.loops - index)) {
                AFB_IfReqFailF(mixer, request, "too-small", "mixer=%s hal=%s max loop=%d", mixer->uid, uid, mixer->max.loops);
                goto OnErrorExit;

            }

            for (int idx = 0; idx < count; idx++) {
                json_object *loopJ = json_object_array_get_idx(argsJ, idx);
                mixer->loops[index + idx] = AttachOneLoop(mixer, uid, loopJ);
                if (!mixer->loops[index + idx]) {
                    AFB_IfReqFailF(mixer, request, "invalid-syntax", "mixer=%s hal=%s invalid loop= %s", mixer->uid, uid, json_object_get_string(loopJ));
                    goto OnErrorExit;
                }
            }
            break;
        default:
            AFB_IfReqFailF(mixer, request, "invalid-syntax", "mixer=%s hal=%s loops invalid argsJ= %s", mixer->uid, uid, json_object_get_string(argsJ));
            goto OnErrorExit;
    }

    return 0;

OnErrorExit:
    return -1;
}
