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

    channel->uid = strdup(channelUid);
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

    error = wrap_json_unpack(zoneJ, "{ss,s?s,so !}"
            , "uid", &zone->uid
            , "type", &streamType
            , "mapping", &mappingJ
            );
    if (error) {
        AFB_ApiNotice(source->api, "ProcessOneZone missing 'uid|type|mapping' zone=%s", json_object_get_string(zoneJ));
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
    zone->uid = strdup(zone->uid);

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

PUBLIC int SndZones(CtlSourceT *source, json_object *argsJ) {
    SoftMixerHandleT *mixerHandle = (SoftMixerHandleT*) source->context;
    AlsaSndZoneT *zones=NULL;
    int error;
    size_t count;

    assert(mixerHandle);

    if (mixerHandle->routes) {
        AFB_ApiError(source->api, "SndZones: mixer=%s Zones already registered %s", mixerHandle->uid, json_object_get_string(argsJ));
        goto OnErrorExit;
    }

    switch (json_object_get_type(argsJ)) {
        case json_type_object:
            count = 1;
            zones = calloc(count + 1, sizeof (AlsaSndZoneT));
            error = ProcessOneZone(source, argsJ, &zones[0]);
            if (error) {
                AFB_ApiError(source->api, "SndZones: mixer=%s invalid zone= %s", mixerHandle->uid, json_object_get_string(argsJ));
                goto OnErrorExit;
            }
            break;

        case json_type_array:
            count = json_object_array_length(argsJ);
            zones = calloc(count + 1, sizeof (AlsaSndZoneT));
            for (int idx = 0; idx < count; idx++) {
                json_object *sndZoneJ = json_object_array_get_idx(argsJ, idx);
                error = ProcessOneZone(source, sndZoneJ, &zones[idx]);
                if (error) {
                    AFB_ApiError(source->api, "SndZones: mixer=%s invalid zone= %s", mixerHandle->uid, json_object_get_string(sndZoneJ));
                    goto OnErrorExit;
                }
            }
            break;
        default:
            AFB_ApiError(source->api, "SndZones: mixer=%s invalid argsJ=  %s", mixerHandle->uid, json_object_get_string(argsJ));
            goto OnErrorExit;
    }

    // register routed into global softmixer handle
    mixerHandle->routes= calloc(count + 1, sizeof (AlsaPcmInfoT*));

    // instantiate one route PCM per zone with multi plugin as slave
    for (int idx = 0; zones[idx].uid != NULL; idx++) {
        mixerHandle->routes[idx] = AlsaCreateRoute(source, &zones[idx], 0);
        if (!mixerHandle->routes[idx]) {
            AFB_ApiNotice(source->api, "SndZones: mixer=%s fail to create route zone=%s", mixerHandle->uid, zones[idx].uid);
            goto OnErrorExit;
        }
    }
    
    free (zones);
    return 0;

OnErrorExit:
    if (zones) free(zones);
    return -1;
}