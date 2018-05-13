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

STATIC int ProcessOneChannel(CtlSourceT *source, const char* uid, json_object *channelJ, AlsaPcmChannels *channel) {
    const char*channelUid;
    
    int error = wrap_json_unpack(channelJ, "{ss,si,s?i !}", "target", &channelUid, "channel", &channel->port);
    if (error) goto OnErrorExit;
    
    channel->uid=strdup(channelUid);
    return 0;

OnErrorExit:
    AFB_ApiError(source->api, "ProcessOneChannel: zone=%s channel: missing (target||channel) json=%s", uid, json_object_get_string(channelJ));
    return -1;
}

STATIC int ProcessOneZone(CtlSourceT *source, json_object *zoneJ, AlsaSndZoneT *zone) {
    json_object *mappingJ;
    size_t count;
    const char* streamType;
    int error;

    error = wrap_json_unpack(zoneJ, "{ss,s?s,so !}", "uid", &zone->uid, "type", &streamType, "mapping", &mappingJ);
    if (error) {
        AFB_ApiNotice(source->api, "ProcessOneone missing 'uid|type|mapping' zone=%s", json_object_get_string(zoneJ));
        goto OnErrorExit;
    }

    if (!streamType) zone->type = SND_PCM_STREAM_PLAYBACK;
    else {
        if (!strcasecmp(streamType, "capture")) zone->type = SND_PCM_STREAM_CAPTURE;
        else if (!strcasecmp(streamType, "playback")) zone->type = SND_PCM_STREAM_PLAYBACK;
        else {
            AFB_ApiError(source->api, "ProcessOneZone:%s invalid stream type !(playback||capture) json=%s", zone->uid, json_object_get_string(zoneJ));
            goto OnErrorExit;
        }
    }
    
    // make sure remain valid even when json object is removed
    zone->uid= strdup(zone->uid); 

    switch (json_object_get_type(mappingJ)) {
        case json_type_object:
            count = 1;
            zone->channels = calloc(count + 1, sizeof (AlsaPcmChannels));
            error = ProcessOneChannel(source, zone->uid, mappingJ, &zone->channels[0]);
            if (error) goto OnErrorExit;
            break;
        case json_type_array:
            count = json_object_array_length(mappingJ);
            zone->channels = calloc(count + 1, sizeof (AlsaPcmChannels));
            for (int idx = 0; idx < count; idx++) {
                json_object *channelJ = json_object_array_get_idx(mappingJ, idx);
                error = ProcessOneChannel(source, zone->uid, channelJ, &zone->channels[idx]);
                if (error) goto OnErrorExit;
            }
            break;
        default:
            AFB_ApiError(source->api, "ProcessOneZone:%s invalid mapping=%s", zone->uid, json_object_get_string(mappingJ));
            goto OnErrorExit;
    }

    return 0;

OnErrorExit:
    return -1;
}

CTLP_LUA2C(snd_zones, source, argsJ, responseJ) {
    AlsaSndZoneT *sndZone;
    int error;
    size_t count;

    switch (json_object_get_type(argsJ)) {
        case json_type_object:
            count = 1;
            sndZone = calloc(count + 1, sizeof (AlsaSndZoneT));
            error = ProcessOneZone(source, argsJ, &sndZone[0]);
            if (error) {
                AFB_ApiError(source->api, "L2C:sndzones: invalid zone= %s", json_object_get_string(argsJ));
                goto OnErrorExit;
            }
            break;

        case json_type_array:
            count = json_object_array_length(argsJ);
            sndZone = calloc(count + 1, sizeof (AlsaSndZoneT));
            for (int idx = 0; idx < count; idx++) {
                json_object *sndZoneJ = json_object_array_get_idx(argsJ, idx);
                error = ProcessOneZone(source, sndZoneJ, &sndZone[idx]);
                if (error) {
                    AFB_ApiError(source->api, "L2C:sndzones: invalid zone= %s", json_object_get_string(sndZoneJ));
                    goto OnErrorExit;
                }
            }
            break;
        default:
            AFB_ApiError(source->api, "L2C:sndzones: invalid argsJ=  %s", json_object_get_string(argsJ));
            goto OnErrorExit;
    }

    // register routed into global softmixer handle
    Softmixer->zonePcms = calloc(count+1, sizeof (AlsaPcmInfoT*));

    // instantiate one route PCM per zone with multi plugin as slave
    for (int idx = 0; sndZone[idx].uid != NULL; idx++) {
        Softmixer->zonePcms[idx] = AlsaCreateRoute(source, &sndZone[idx], 0);
        if (!Softmixer->zonePcms[idx]) {
            AFB_ApiNotice(source->api, "L2C:sndzones fail to create route zone=%s", sndZone[idx].uid);
            goto OnErrorExit;
        }
    }
    
    // do not need this handle anymore
    free (sndZone);
    return 0;

OnErrorExit:
    return -1;
}