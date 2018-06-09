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

PUBLIC AlsaSndCtlT *ApiSourceFindSubdev(SoftMixerT *mixer, const char *target) {

    // search for subdev into every registered source
    for (int idx = 0; mixer->sources[idx]; idx++) {
        if (mixer->sources[idx]->uid && !strcasecmp(mixer->sources[idx]->uid, target)) {
            return mixer->sources[idx]->sndcard;
        }
    }
    return NULL;
}

PUBLIC int ApiSourceAttach(SoftMixerT *mixer, AFB_ReqT request, const char *uid, json_object * argsJ) {

    int index;
    for (index = 0; index < mixer->max.sources; index++) {
        if (!mixer->sources[index]) break;
    }

    if (index == mixer->max.sources) {
        AFB_ReqFailF(request, "too-small", "mixer=%s max source=%d", mixer->uid, mixer->max.sources);
        goto OnErrorExit;
    }

    switch (json_object_get_type(argsJ)) {
        long count;
        
        case json_type_object:
            mixer->sources[index] = ApiPcmAttachOne(mixer, uid, SND_PCM_STREAM_CAPTURE, argsJ);
            if (!mixer->sources[index]) {
                AFB_ReqFailF(request, "invalid-syntax", "mixer=%s invalid source= %s", mixer->uid, json_object_get_string(argsJ));
                goto OnErrorExit;
            }
            break;

        case json_type_array:
            count = json_object_array_length(argsJ);
            if (count > (mixer->max.sources - index)) {
                AFB_ReqFailF(request, "too-small", "mixer=%s max source=%d", mixer->uid, mixer->max.sources);
                goto OnErrorExit;

            }

            for (int idx = 0; idx < count; idx++) {
                json_object *sourceJ = json_object_array_get_idx(argsJ, idx);
                mixer->sources[index + idx] = ApiPcmAttachOne(mixer, uid, SND_PCM_STREAM_CAPTURE, sourceJ);
                if (!mixer->sources[index + idx]) {
                    AFB_ReqFailF(request, "invalid-syntax", "mixer=%s invalid source= %s", mixer->uid, json_object_get_string(sourceJ));
                    goto OnErrorExit;
                }
            }
            break;
        default:
            AFB_ReqFailF(request, "invalid-syntax", "mixer=%s sources invalid argsJ= %s", mixer->uid, json_object_get_string(argsJ));
            goto OnErrorExit;
    }

    return 0;

OnErrorExit:
    return -1;
}