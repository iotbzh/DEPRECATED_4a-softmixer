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

#define _GNU_MIXER  // needed for vasprintf

#include "alsa-softmixer.h"
#include <string.h>

// Fulup need to be cleanup with new controller version
extern Lua2cWrapperT Lua2cWrap;

PUBLIC AlsaSndZoneT *ApiZoneGetByUid(SoftMixerT *mixer, const char *target) {

    assert(mixer->zones[0]);

    // search for subdev into every registered loop
    for (int idx = 0; mixer->zones[idx]; idx++) {
        if (mixer->zones[idx]->uid && !strcasecmp(mixer->zones[idx]->uid, target)) {
            return mixer->zones[idx];
        }
    }
    AFB_ApiError(mixer->api, "ApiZoneGetByUid mixer=%s fail to find zone=%s", mixer->uid, target);
    return NULL;
}

STATIC AlsaPcmChannelT* ProcessOneChannel(SoftMixerT *mixer, const char* uid, json_object *channelJ) {
    AlsaPcmChannelT *channel = calloc(1, sizeof (AlsaPcmChannelT));

    int error = wrap_json_unpack(channelJ, "{ss,si !}"
            , "target", &channel->uid
            , "channel", &channel->port
            );
    if (error) goto OnErrorExit;

    channel->uid = strdup(channel->uid);
    return channel;

OnErrorExit:
    AFB_ApiError(mixer->api, "ProcessOneChannel: zone=%s channel: missing (target|channel) json=%s", uid, json_object_get_string(channelJ));
    return NULL;
}

STATIC AlsaSndZoneT *AttacheOneZone(SoftMixerT *mixer, const char *uid, json_object *zoneJ) {
    AlsaSndZoneT *zone = calloc(1, sizeof (AlsaSndZoneT));
    json_object *sinkJ = NULL, *sourceJ = NULL;
    size_t count;
    int error;

    error = wrap_json_unpack(zoneJ, "{ss,s?o,s?o !}"
            , "uid", &zone->uid
            , "sink", &sinkJ
            , "source", &sourceJ
            );
    if (error || (!sinkJ && sourceJ)) {
        AFB_ApiNotice(mixer->api, "AttacheOneZone missing 'uid|sink|source' error=%s zone=%s", wrap_json_get_error_string(error), json_object_get_string(zoneJ));
        goto OnErrorExit;
    }

    // make sure remain valid even when json object is removed
    zone->uid = strdup(zone->uid);

    if (sinkJ) {

        switch (json_object_get_type(sinkJ)) {
            case json_type_object:
                zone->sinks = calloc(2, sizeof (void*));
                zone->sinks[0] = ProcessOneChannel(mixer, zone->uid, sinkJ);
                if (!zone->sinks[0]) goto OnErrorExit;

                break;
            case json_type_array:
                count = json_object_array_length(sinkJ);
                zone->sinks = calloc(count + 1, sizeof (void*));
                for (int idx = 0; idx < count; idx++) {
                    json_object *subdevJ = json_object_array_get_idx(sinkJ, idx);
                    zone->sinks[idx] = ProcessOneChannel(mixer, zone->uid, subdevJ);
                    if (error) goto OnErrorExit;
                }
                break;
            default:
                AFB_ApiError(mixer->api, "AttacheOneZone: Mixer=%s Hal=%s zone=%s invalid mapping=%s", mixer->uid, uid, zone->uid, json_object_get_string(sinkJ));
                goto OnErrorExit;
        }

    }

    if (sourceJ) {
        switch (json_object_get_type(sourceJ)) {
            case json_type_object:
                zone->sources = calloc(2, sizeof (void*));
                zone->sources[0] = ProcessOneChannel(mixer, zone->uid, sourceJ);
                if (!zone->sources[0]) goto OnErrorExit;
                break;
            case json_type_array:
                count = json_object_array_length(sourceJ);
                zone->sources = calloc(count + 1, sizeof (void*));
                for (int idx = 0; idx < count; idx++) {
                    json_object *subdevJ = json_object_array_get_idx(sourceJ, idx);
                    zone->sources[idx] = ProcessOneChannel(mixer, zone->uid, subdevJ);
                    if (error) goto OnErrorExit;
                }
                break;
            default:
                AFB_ApiError(mixer->api, "AttacheOneZone:Mixer=%s Hal=%s zone=%s mapping=%s", mixer->uid, uid, zone->uid, json_object_get_string(sourceJ));
                goto OnErrorExit;
        }
    }

    return zone;

OnErrorExit:
    return NULL;
}

PUBLIC int ApiZoneAttach(SoftMixerT *mixer, AFB_ReqT request, const char *uid, json_object * argsJ) {

    int index;
    for (index = 0; index < mixer->max.zones; index++) {
        if (!mixer->zones[index]) break;
    }

    if (index == mixer->max.zones) {
        AFB_ReqFailF(request, "too-small", "mixer=%s max zone=%d", mixer->uid, mixer->max.zones);
        goto OnErrorExit;
    }

    switch (json_object_get_type(argsJ)) {
            long count;

        case json_type_object:
            mixer->zones[index] = AttacheOneZone(mixer, uid, argsJ);
            if (!mixer->zones[index]) {
                AFB_ReqFailF(request, "invalid-syntax", "mixer=%s invalid zone= %s", mixer->uid, json_object_get_string(argsJ));
                goto OnErrorExit;
            }

            AlsaPcmCtlT *routeConfig = AlsaCreateRoute(mixer, mixer->zones[index], 0);
            if (!routeConfig) {
                AFB_ApiError(mixer->api, "AttacheOneZone: Mixer=%s Hal=%s zone=%s Fail to attach PCM Route", mixer->uid, uid, mixer->zones[index]->uid);
                goto OnErrorExit;
            }

            break;

        case json_type_array:
            count = json_object_array_length(argsJ);
            if (count > (mixer->max.zones - index)) {
                AFB_ReqFailF(request, "too-small", "mixer=%s max zone=%d", mixer->uid, mixer->max.zones);
                goto OnErrorExit;

            }

            for (int idx = 0; idx < count; idx++) {
                json_object *zoneJ = json_object_array_get_idx(argsJ, idx);
                mixer->zones[index + idx] = AttacheOneZone(mixer, uid, zoneJ);
                if (!mixer->zones[index + idx]) {
                    AFB_ReqFailF(request, "invalid-syntax", "mixer=%s invalid zone= %s", mixer->uid, json_object_get_string(zoneJ));
                    goto OnErrorExit;
                }
                
                AlsaPcmCtlT *routeConfig = AlsaCreateRoute(mixer, mixer->zones[idx], 0);
                if (!routeConfig) {
                    AFB_ApiError(mixer->api, "AttacheOneZone: Mixer=%s Hal=%s zone=%s Fail to attach PCM Route", mixer->uid, uid, mixer->zones[idx]->uid);
                    goto OnErrorExit;
                }
            }
            break;
        default:
            AFB_ReqFailF(request, "invalid-syntax", "mixer=%s zones invalid argsJ= %s", mixer->uid, json_object_get_string(argsJ));
            goto OnErrorExit;
    }

    return 0;

OnErrorExit:
    return -1;
}
