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

ALSA_PLUG_PROTO(route);

STATIC int CardChannelByUid(CtlSourceT *source, AlsaPcmInfoT *pcmBackend, const char *uid) {
    int channelIdx = -1;

    // search for channel within all sound card backend (channel port target is computed by order)
    int targetIdx=0;
    for (int cardIdx = 0; pcmBackend[cardIdx].uid != NULL; cardIdx++) {
        AlsaPcmChannels *channels = pcmBackend[cardIdx].channels;
        if (!channels) {
            AFB_ApiError(source->api, "CardChannelByUid: No Backend card=%s [should declare channels]", pcmBackend[cardIdx].uid);
            goto OnErrorExit;
        }

        for (int idx = 0; channels[idx].uid != NULL; idx++) {
            if (!strcmp(channels[idx].uid, uid)) return targetIdx;
            targetIdx++;
        }
    }

    // this is OnErrorExit
    return channelIdx;

OnErrorExit:
    return -1;
}

PUBLIC AlsaPcmInfoT* AlsaCreateRoute(CtlSourceT *source, AlsaSndZoneT *zone, int open) {

    snd_config_t *routeConfig, *elemConfig, *slaveConfig, *tableConfig, *pcmConfig;
    int error = 0;
    AlsaPcmInfoT *pcmPlug = calloc(1, sizeof (AlsaPcmInfoT));
    pcmPlug->uid    = zone->uid;
    pcmPlug->cardid = zone->uid;

    AlsaPcmInfoT *pcmBackend = Softmixer->sndcardCtl;
    AlsaPcmInfoT* pcmSlave=Softmixer->multiPcm;
    if (!pcmBackend || !pcmSlave) {
        AFB_ApiError(source->api, "AlsaCreateRoute:zone(%s)(zone) No Sound Card Ctl find [should register snd_cards first]", zone->uid);
        goto OnErrorExit;
    }

    // refresh global alsalib config and create PCM top config
    snd_config_update();
    error += snd_config_top(&routeConfig);
    error += snd_config_set_id(routeConfig, pcmPlug->cardid);
    error += snd_config_imake_string(&elemConfig, "type", "route");
    error += snd_config_add(routeConfig, elemConfig);
    error += snd_config_make_compound(&slaveConfig, "slave", 0);
    error += snd_config_imake_string(&elemConfig, "pcm", pcmSlave->cardid);
    error += snd_config_add(slaveConfig, elemConfig);
    error += snd_config_imake_integer(&elemConfig, "channels", pcmSlave->ccount);
    error += snd_config_add(slaveConfig, elemConfig);
    error += snd_config_add(routeConfig, slaveConfig);
    error += snd_config_make_compound(&tableConfig, "ttable", 0);
    error += snd_config_add(routeConfig, tableConfig);
    if (error) goto OnErrorExit;

    // tempry store to unable multiple channel to route to the same port
    snd_config_t **cports;
    cports = alloca(sizeof (snd_config_t*)*(pcmSlave->ccount + 1));
    memset(cports, 0, (sizeof (snd_config_t*)*(pcmSlave->ccount + 1)));

    // loop on sound card to include into multi
    for (int idx = 0; zone->channels[idx].uid != NULL; idx++) {

        int target = CardChannelByUid(source, pcmBackend, zone->channels[idx].uid);
        if (target < 0) {
            AFB_ApiError(source->api, "AlsaCreateRoute:zone(%s) fail to find channel=%s", zone->uid, zone->channels[idx].uid);
            goto OnErrorExit;
        }
        
        int channel = zone->channels[idx].port;
        double volume = 1.0; // currently only support 100%

        // if channel entry does not exit into ttable create it now 
        if (!cports[channel]) {
            pcmPlug->ccount++;
            char channelS[4]; // 999 channel should be more than enough
            snprintf(channelS, sizeof (channelS), "%d", channel);
            error += snd_config_make_compound(&cports[channel], channelS, 0);
            error += snd_config_add(tableConfig, cports[channel]);
        }

        // ttable require target port as a table and volume as a value
        char targetS[4];
        snprintf(targetS, sizeof (targetS), "%d", target);
        error += snd_config_imake_real(&elemConfig, targetS, volume);
        error += snd_config_add(cports[channel], elemConfig);
        if (error) goto OnErrorExit;
    }

    // update top config to access previous plugin PCM
    snd_config_update();

    if (open) error = _snd_pcm_route_open(&pcmPlug->handle, zone->uid, snd_config, routeConfig, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if (error) {
        AFB_ApiError(source->api, "AlsaCreateRoute:zone(%s) fail to create Plug=%s error=%s", zone->uid, pcmPlug->cardid, snd_strerror(error));
        goto OnErrorExit;
    }

    error += snd_config_search(snd_config, "pcm", &pcmConfig);
    error += snd_config_add(pcmConfig, routeConfig);
    if (error) {
        AFB_ApiError(source->api, "AlsaCreateDmix:%s fail to add configDMIX=%s", zone->uid, pcmPlug->cardid);
        goto OnErrorExit;
    }

    // Debug config & pcm
    //AlsaDumpCtlConfig(source, "plug-route", routeConfig, 1);
    AFB_ApiNotice(source->api, "AlsaCreateRoute:zone(%s) done\n", zone->uid);
    return pcmPlug;

OnErrorExit:
    AlsaDumpCtlConfig(source, "plug-route", routeConfig, 1);
    AFB_ApiNotice(source->api, "AlsaCreateRoute:zone(%s) OnErrorExit\n", zone->uid);
    return NULL;
}