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

PUBLIC AlsaPcmHwInfoT *ApiSinkGetParamsByZone(SoftMixerT *mixer, const char *target) {

    AlsaSndZoneT *zone = ApiZoneGetByUid(mixer, target);
    if (!zone || !zone->sinks) return NULL;

    // use 1st channel to find attached sound card.
    const char *channel = zone->sinks[0]->uid;

    // search for channel uid into mixer sinks
    for (int idx = 0; mixer->sinks[idx]; idx++) {
        for (int jdx = 0; jdx < mixer->sinks[idx]->ccount; jdx++) {
            if (mixer->sinks[idx]->channels[jdx]->uid && !strcasecmp(channel, mixer->sinks[idx]->channels[jdx]->uid)) {
                return mixer->sinks[idx]->sndcard->params;
            }
        }
    }
    return NULL;
}

PUBLIC int ApiSinkAttach(SoftMixerT *mixer, AFB_ReqT request, const char *uid, json_object * argsJ) {

    int index;
    for (index = 0; index < mixer->max.sinks; index++) {
        if (!mixer->sinks[index]) break;
    }

    if (index == mixer->max.sinks) {
        AFB_ReqFailF(request, "too-small", "mixer=%s max sink=%d argsJ= %s", mixer->uid, mixer->max.sinks, json_object_get_string(argsJ));
        goto OnErrorExit;
    }

    switch (json_object_get_type(argsJ)) {
            long count;
            char *dmixUid;
            AlsaPcmCtlT* dmixConfig;

        case json_type_object:
            mixer->sinks[index] = ApiPcmAttachOne(mixer, uid, SND_PCM_STREAM_PLAYBACK, argsJ);
            if (!mixer->sinks[index]) {
                AFB_ReqFailF(request, "invalid-syntax", "mixer=%s invalid sink= %s", mixer->uid, json_object_get_string(argsJ));
                goto OnErrorExit;
            }

            // move from hardware to DMIX attach to sndcard
            (void) asprintf(&dmixUid, "dmix-%s", mixer->sinks[index]->uid);

            dmixConfig = AlsaCreateDmix(mixer, dmixUid, mixer->sinks[index], 0);
            if (!dmixConfig) {
                AFB_ReqFailF(request, "internal-error", "mixer=%s sink=%s fail to create DMIX config", mixer->uid, mixer->sinks[index]->uid);
                goto OnErrorExit;
            }

            break;

        case json_type_array:
            count = json_object_array_length(argsJ);
            if (count > (mixer->max.sinks - count)) {
                AFB_ReqFailF(request, "too-small", "mixer=%s max sink=%d argsJ= %s", mixer->uid, mixer->max.sinks, json_object_get_string(argsJ));
                goto OnErrorExit;
            }

            for (int idx = 0; idx < count; idx++) {
                json_object *sinkJ = json_object_array_get_idx(argsJ, idx);
                mixer->sinks[index + idx] = ApiPcmAttachOne(mixer, uid, SND_PCM_STREAM_PLAYBACK, sinkJ);
                if (!mixer->sinks[index + idx]) {
                    AFB_ReqFailF(request, "invalid-syntax", "mixer=%s invalid sink= %s", mixer->uid, json_object_get_string(sinkJ));
                    goto OnErrorExit;
                }

                // move from hardware to DMIX attach to sndcard
                (void) asprintf(&dmixUid, "dmix-%s", mixer->sinks[index]->uid);

                dmixConfig = AlsaCreateDmix(mixer, dmixUid, mixer->sinks[index], 0);
                if (!dmixConfig) {
                    AFB_ReqFailF(request, "internal-error", "mixer=%s sink=%s fail to create DMIX config", mixer->uid, mixer->sinks[index]->uid);
                    goto OnErrorExit;
                }
            }
            break;
        default:
            AFB_ReqFailF(request, "invalid-syntax", "mixer=%s sinks invalid argsJ= %s", mixer->uid, json_object_get_string(argsJ));
            goto OnErrorExit;
    }

    return 0;

OnErrorExit:
    return -1;
}