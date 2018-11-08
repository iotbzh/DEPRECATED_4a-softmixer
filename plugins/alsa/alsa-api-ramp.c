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

PUBLIC AlsaVolRampT *ApiRampGetByUid(SoftMixerT *mixer, const char *uid) {
    AlsaVolRampT *ramp = NULL;

    // Loop on every Register zone pcm and extract (cardid) from (uid)
    for (int idx = 0; mixer->ramps[idx]->uid != NULL; idx++) {
        if (!strcasecmp(mixer->ramps[idx]->uid, uid)) {
            ramp = mixer->ramps[idx];
            return ramp;
        }
    }
    return NULL;
}

STATIC AlsaVolRampT *AttachOneRamp(SoftMixerT *mixer, const char *uid, json_object *rampJ) {
    const char*rampUid;
    AlsaVolRampT *ramp = calloc(1, sizeof (AlsaVolRampT));

    int error = wrap_json_unpack(rampJ, "{ss,si,si,si !}"
            , "uid", &rampUid
            , "delay", &ramp->delay
            , "up", &ramp->stepUp
            , "down", &ramp->stepDown
            );
    if (error) {
        AFB_ApiError(mixer->api, "AttachOneRamp mixer=%s hal=%s error=%s json=%s", mixer->uid, uid, wrap_json_get_error_string(error), json_object_get_string(rampJ));
        goto OnErrorExit;
    }

    ramp->delay = ramp->delay * 1000; // move from ms to us
    ramp->uid = strdup(rampUid);
    return ramp;

OnErrorExit:
    free(ramp);
    return NULL;
}

PUBLIC int ApiRampAttach(SoftMixerT *mixer, AFB_ReqT request, const char *uid, json_object *argsJ) {
    int index;

    for (index = 0; index < mixer->max.ramps; index++) {
        if (!mixer->ramps[index]) break;
    }

    if (index == mixer->max.ramps) {
        AFB_ReqFailF(request, "too-small", "mixer=%s hal=%s max ramp=%d", mixer->uid, uid, mixer->max.ramps);
        goto OnErrorExit;
    }

    switch (json_object_get_type(argsJ)) {
        long count;
        
        case json_type_object:
            mixer->ramps[index] = AttachOneRamp(mixer, uid, argsJ);
            if (!mixer->ramps[index]) {
                AFB_ReqFailF(request, "bad-ramp", "mixer=%s hal=%s invalid ramp= %s", mixer->uid, uid, json_object_get_string(argsJ));
                goto OnErrorExit;
            }
            break;

        case json_type_array:
            count = json_object_array_length(argsJ);
            if (count > (mixer->max.ramps - index)) {
                AFB_ReqFailF(request, "too-small", "mixer=%s hal=%s max ramp=%d", mixer->uid, uid, mixer->max.ramps);
                goto OnErrorExit;

            }

            for (int idx = 0; idx < count; idx++) {
                json_object *streamAudioJ = json_object_array_get_idx(argsJ, idx);
                mixer->ramps[index + idx] = AttachOneRamp(mixer, uid, streamAudioJ);
                if (!mixer->ramps[index + idx]) {
                    AFB_ReqFailF(request, "bad-ramp", "mixer=%s hal=%s invalid ramp= %s", mixer->uid, uid, json_object_get_string(streamAudioJ));
                    goto OnErrorExit;
                }
            }
            break;
        default:
            AFB_ReqFailF(request, "invalid-syntax", "mixer=%s hal=%s ramps invalid argsJ= %s", mixer->uid, uid, json_object_get_string(argsJ));
            goto OnErrorExit;
    }
    return 0;

OnErrorExit:
    return -1;
}
