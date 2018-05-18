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

ALSA_PLUG_PROTO(softvol); // stream uses solftvol plugin

STATIC AlsaPcmInfoT* SlaveZoneByUid(CtlSourceT *source,  AlsaPcmInfoT **pcmZones, const char *uid) {
    AlsaPcmInfoT *slaveZone= NULL;

    // Loop on every Registryed zone pcm and extract (cardid) from (uid)
    for (int idx = 0; pcmZones[idx] != NULL; idx++) {
        if (!strcasecmp (pcmZones[idx]->uid, uid)) {
           slaveZone= pcmZones[idx];
           return slaveZone;
        }
    }

    return NULL;
}

PUBLIC AlsaPcmInfoT* AlsaCreateSoftvol(CtlSourceT *source, AlsaLoopStreamT *stream, AlsaPcmInfoT *ctlControl, const char* ctlName, int max, int open) {
    SoftMixerHandleT *mixerHandle = (SoftMixerHandleT*) source->context;
    snd_config_t *streamConfig, *elemConfig, *slaveConfig, *controlConfig,*pcmConfig;
    int error = 0;
    AlsaPcmInfoT *pcmPlug= calloc(1,sizeof(AlsaPcmInfoT));

    assert (mixerHandle);

    // assert static/global softmixer handle get requited info
    AlsaSndLoopT *ctlLoop = mixerHandle->frontend;
    if (!ctlLoop) {
        AFB_ApiError(source->api, "AlsaCreateSoftvol:%s(stream) No Loop found [should Registry snd_loop first]",stream->uid);
        goto OnErrorExit;
    }

    // assert static/global softmixer handle get requited info
    AlsaPcmInfoT **pcmZones = mixerHandle->routes;
    if (!pcmZones) {
        AFB_ApiError(source->api, "AlsaCreateSoftvol:%s(stream) No Zone found [should Registry snd_zones first]", stream->uid);
        goto OnErrorExit;
    }
    
    // search for target zone uid
    AlsaPcmInfoT *pcmSlave= SlaveZoneByUid (source, pcmZones, stream->zone);
    if (!pcmSlave || !pcmSlave->uid) {
        AFB_ApiError(source->api, "AlsaCreateSoftvol:%s(stream) fail to find Zone=%s", stream->uid, stream->zone);
        goto OnErrorExit;  
    }
    
    // stream inherit from zone channel count
    pcmPlug->uid= strdup(stream->uid);
    pcmPlug->cardid= pcmPlug->uid;
    pcmPlug->devpath=NULL;
    pcmPlug->ccount= pcmSlave->ccount;
    memcpy (&pcmPlug->params, &stream->params, sizeof(AlsaPcmHwInfoT));
    pcmPlug->params.channels= pcmSlave->ccount;
       
    // refresh global alsalib config and create PCM top config
    snd_config_update();
    error += snd_config_top(&streamConfig);
    error += snd_config_set_id (streamConfig, pcmPlug->cardid);
    error += snd_config_imake_string(&elemConfig, "type", "softvol");
    error += snd_config_add(streamConfig, elemConfig);
    error += snd_config_imake_integer(&elemConfig, "resolution", max+1); 
    error += snd_config_add(streamConfig, elemConfig);
    if (error) goto OnErrorExit;
    
    // add slave leaf
    error += snd_config_make_compound(&slaveConfig, "slave", 0);
    error += snd_config_imake_string(&elemConfig, "pcm", pcmSlave->cardid);
    error += snd_config_add(slaveConfig, elemConfig);
    error += snd_config_add(streamConfig, slaveConfig);
    if (error) goto OnErrorExit;
    
    // add control leaf
    error += snd_config_make_compound(&controlConfig, "control", 0);
    error += snd_config_imake_string(&elemConfig, "name", ctlName);
    error += snd_config_add(controlConfig, elemConfig);
    error += snd_config_imake_integer(&elemConfig, "card", ctlControl->cardidx);
    error += snd_config_add(controlConfig, elemConfig);
    error += snd_config_add(streamConfig, controlConfig);
    if (error) goto OnErrorExit; 
    
    // update top config to access previous plugin PCM
    snd_config_update();
    
    if (open) error = _snd_pcm_softvol_open(&pcmPlug->handle, stream->uid, snd_config, streamConfig, SND_PCM_STREAM_PLAYBACK , SND_PCM_NONBLOCK); 
    if (error) {
        AFB_ApiError(source->api, "AlsaCreateSoftvol:%s(stream) fail to create Plug=%s Slave=%s error=%s", stream->uid, pcmPlug->cardid, pcmSlave->cardid, snd_strerror(error));
        goto OnErrorExit;
    }
  
    error += snd_config_search(snd_config, "pcm", &pcmConfig);    
    error += snd_config_add(pcmConfig, streamConfig);
    if (error) {
        AFB_ApiError(source->api, "AlsaCreateSoftvol:%s(stream) fail to add config", stream->uid);
        goto OnErrorExit;
    }
    
    // Debug config & pcm
    //AlsaDumpCtlConfig (source, "plug-stream", streamConfig, 1);
    AFB_ApiNotice(source->api, "AlsaCreateSoftvol:%s(stream) done\n", stream->uid);
    return pcmPlug;

OnErrorExit:
    AlsaDumpCtlConfig(source, "plug-stream", streamConfig, 1);
    AFB_ApiNotice(source->api, "AlsaCreateSoftvol:%s(stream) OnErrorExit\n", stream->uid);
    return NULL;
}