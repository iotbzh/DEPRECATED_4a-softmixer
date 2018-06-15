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

typedef struct {
    const char *uid;
    const char *cardid;
    int cardidx;
    int ccount;
    int port;
} ChannelCardPortT;

STATIC int CardChannelByUid(SoftMixerT *mixer, const char *uid, ChannelCardPortT *response) {

    // search for channel within all sound card sink (channel port target is computed by order)
    for (int idx = 0; mixer->sinks[idx]; idx++) {
        int jdx;

        AlsaPcmChannelT **channels = mixer->sinks[idx]->channels;
        if (!channels) {
            AFB_ApiError(mixer->api, "CardChannelByUid: No Sink card=%s [should declare channels]", mixer->sinks[idx]->uid);
            goto OnErrorExit;
        }

        for (jdx = 0; jdx < mixer->sinks[idx]->ccount; jdx++) {
            if (!strcasecmp(channels[jdx]->uid, uid)) {
                response->port = channels[jdx]->port;
                response->uid = mixer->sinks[idx]->uid;
                response->ccount = mixer->sinks[idx]->ccount;
                response->cardid = mixer->sinks[idx]->sndcard->cid.cardid;
                response->cardidx = mixer->sinks[idx]->sndcard->cid.cardidx;
                break;
            }
        }

        if (jdx == mixer->sinks[idx]->ccount) {
            AFB_ApiError(mixer->api, "CardChannelByUid: No Channel with uid=%s [should declare channels]", uid);
            goto OnErrorExit;
        }
    }

    return 0;

OnErrorExit:
    return -1;
}

PUBLIC AlsaPcmCtlT* AlsaCreateRoute(SoftMixerT *mixer, AlsaSndZoneT *zone, int open) {
    snd_config_t *routeConfig, *elemConfig, *slaveConfig, *tableConfig, *pcmConfig;
    int scount=0, error = 0;
    ChannelCardPortT slave, channel;
    AlsaPcmCtlT *pcmRoute = calloc(1, sizeof (AlsaPcmCtlT));
    char *cardid = NULL;
    char *dmixUid = NULL;

    if (!mixer->sinks) {
        AFB_ApiError(mixer->api, "AlsaCreateRoute: mixer=%s zone(%s)(zone) No sink found [should Registry sound card first]", mixer->uid, zone->uid);
        goto OnErrorExit;
    }

    if (asprintf(&cardid, "route-%s", zone->uid) == -1)
        goto OnErrorExit;

    pcmRoute->cid.cardid = (const char *) cardid;
    
    pcmRoute->params = ApiSinkGetParamsByZone(mixer, zone->uid);
    zone->params=pcmRoute->params;
    
    // use 1st zone channel to retrieve sound card name + channel count. 
    error = CardChannelByUid(mixer, zone->sinks[0]->uid, &slave);
    if (error) {
        AFB_ApiError(mixer->api, "AlsaCreateRoute:zone(%s) fail to find channel=%s", zone->uid, zone->sinks[0]->uid);
        goto OnErrorExit;
    }
    
    // move from hardware to DMIX attach to sndcard
    if (asprintf(&dmixUid, "dmix-%s", slave.uid) == -1)
        goto OnErrorExit;

    // temporary store to unable multiple channel to route to the same port
    snd_config_t **cports = alloca(slave.ccount * sizeof (void*));
    memset(cports, 0, slave.ccount * sizeof (void*));
    int zcount = 0;

    // We create 1st ttable to retrieve sndcard slave and channel count
    (void)snd_config_make_compound(&tableConfig, "ttable", 0);

    for (scount = 0; zone->sinks[scount] != NULL; scount++) {

        error = CardChannelByUid(mixer, zone->sinks[scount]->uid, &channel);
        if (error) {
            AFB_ApiError(mixer->api, "AlsaCreateRoute:zone(%s) fail to find channel=%s", zone->uid, zone->sinks[scount]->uid);
            goto OnErrorExit;
        }

        if (slave.cardidx != channel.cardidx) {
            AFB_ApiError(mixer->api, "AlsaCreateRoute:zone(%s) cannot span over multiple sound card %s != %s ", zone->uid, slave.cardid, channel.cardid);
            goto OnErrorExit;
        }

        int port = zone->sinks[scount]->port;
        int target = channel.port;
        double volume = 1.0; // currently only support 100%

        // if channel entry does not exit into ttable create it now 
        if (!cports[port]) { 
            zcount++;
            char channelS[4]; // 999 channel should be more than enough
            snprintf(channelS, sizeof (channelS), "%d", port);
            error += snd_config_make_compound(&cports[port], channelS, 0);
            error += snd_config_add(tableConfig, cports[port]);
        }

        // ttable require target port as a table and volume as a value
        char targetS[4];
        snprintf(targetS, sizeof (targetS), "%d", target);
        error += snd_config_imake_real(&elemConfig, targetS, volume);
        error += snd_config_add(cports[port], elemConfig);
        if (error) goto OnErrorExit;
    }
    if (error) goto OnErrorExit;

    // update zone with route channel count and sndcard params
    pcmRoute->ccount = zcount;
    zone->ccount=zcount;

    // refresh global alsalib config and create PCM top config
    snd_config_update();
    error += snd_config_top(&routeConfig);
    error += snd_config_set_id(routeConfig, cardid);
    error += snd_config_imake_string(&elemConfig, "type", "route");
    error += snd_config_add(routeConfig, elemConfig);
    error += snd_config_make_compound(&slaveConfig, "slave", 0);
    error += snd_config_imake_string(&elemConfig, "pcm", dmixUid);
    error += snd_config_add(slaveConfig, elemConfig);
    error += snd_config_imake_integer(&elemConfig, "channels", slave.ccount);
    error += snd_config_add(slaveConfig, elemConfig);
    error += snd_config_add(routeConfig, slaveConfig);
    error += snd_config_add(routeConfig, tableConfig);
    if (error) goto OnErrorExit;

    if (open) error = _snd_pcm_route_open(&pcmRoute->handle, pcmRoute->cid.cardid, snd_config, routeConfig, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if (error) {
        AFB_ApiError(mixer->api, "AlsaCreateRoute:zone(%s) fail to create Plug=%s error=%s", zone->uid, pcmRoute->cid.cardid, snd_strerror(error));
        goto OnErrorExit;
    }

    snd_config_update();
    error += snd_config_search(snd_config, "pcm", &pcmConfig);
    error += snd_config_add(pcmConfig, routeConfig);
    if (error) {
        AFB_ApiError(mixer->api, "AlsaCreateDmix:%s fail to add config route=%s error=%s", zone->uid, pcmRoute->cid.cardid, snd_strerror(error));
        goto OnErrorExit;
    }

    // Debug config & pcm
    AFB_ApiNotice(mixer->api, "AlsaCreateRoute:zone(%s) done", zone->uid);
    //AlsaDumpCtlConfig(mixer, "plug-route", routeConfig, 1);
    return pcmRoute;

OnErrorExit:
    free(pcmRoute);
    free(cardid);
    free(dmixUid);
    AlsaDumpCtlConfig(mixer, "plug-pcm", snd_config, 1);
    AlsaDumpCtlConfig(mixer, "plug-route", routeConfig, 1);
    AFB_ApiNotice(mixer->api, "AlsaCreateRoute:zone(%s) OnErrorExit\n", zone->uid);
    return NULL;
}
