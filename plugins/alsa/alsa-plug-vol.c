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

ALSA_PLUG_PROTO(softvol); // stream uses softvol plugin

PUBLIC AlsaPcmCtlT *AlsaCreateSoftvol(SoftMixerT *mixer, AlsaStreamAudioT *stream, char* slaveid, AlsaSndCtlT *sndcard, char* ctlName, int max, int open) {
    snd_config_t *streamConfig, *elemConfig, *slaveConfig, *controlConfig,*pcmConfig;
    AlsaPcmCtlT *pcmVol= calloc(1,sizeof(AlsaPcmCtlT));
    int error = 0;
     
    AFB_ApiInfo(mixer->api, "%s create SOFTVOL on %s", __func__, slaveid);

    char *cardid = NULL;
    if (asprintf(&cardid, "softvol-%s", stream->uid) == -1)
        goto OnErrorExit;

    pcmVol->cid.cardid = (const char *) cardid;
    
    // refresh global alsalib config and create PCM top config
    snd_config_update();
    error += snd_config_top(&streamConfig);
    error += snd_config_set_id (streamConfig, pcmVol->cid.cardid);
    error += snd_config_imake_string(&elemConfig, "type", "softvol");
    error += snd_config_add(streamConfig, elemConfig);
    error += snd_config_imake_integer(&elemConfig, "resolution", max+1); 
    error += snd_config_add(streamConfig, elemConfig);
    if (error) goto OnErrorExit;
    
    // add slave leaf
    error += snd_config_make_compound(&slaveConfig, "slave", 0);
    error += snd_config_imake_string(&elemConfig, "pcm", slaveid);
    error += snd_config_add(slaveConfig, elemConfig);

    error += snd_config_add(streamConfig, slaveConfig);
    if (error) goto OnErrorExit;
    
    // add control leaf
    error += snd_config_make_compound(&controlConfig, "control", 0);
    error += snd_config_imake_string(&elemConfig, "name", ctlName);
    error += snd_config_add(controlConfig, elemConfig);
    error += snd_config_imake_integer(&elemConfig, "card", sndcard->cid.cardidx);
    error += snd_config_add(controlConfig, elemConfig);
    error += snd_config_add(streamConfig, controlConfig);
    if (error) goto OnErrorExit; 
    
    if (open) error = _snd_pcm_softvol_open(&pcmVol->handle, stream->uid, snd_config, streamConfig, SND_PCM_STREAM_PLAYBACK , SND_PCM_NONBLOCK); 
    if (error) {
        AFB_ApiError(mixer->api,
                     "%s: %s(stream) fail to create Plug=%s Slave=%s error=%s",
                     __func__, stream->uid, pcmVol->cid.cardid, sndcard->cid.cardid, snd_strerror(error));
        goto OnErrorExit;
    }
  
    // update top config to access previous plugin PCM
    snd_config_update();
    
    error += snd_config_search(snd_config, "pcm", &pcmConfig);    
    error += snd_config_add(pcmConfig, streamConfig);
    if (error) {
        AFB_ApiError(mixer->api,
                     "%s: %s(stream) fail to add config error=%s",
                     __func__, stream->uid, snd_strerror(error));
        goto OnErrorExit;
    }
    
    // Debug config & pcm
    //AlsaDumpCtlConfig (mixer, "plug-config", pcmConfig, 1);
    AlsaDumpCtlConfig(mixer, "plug-softvol", streamConfig, 1);
    AFB_ApiNotice(mixer->api, "%s: %s(stream) done", __func__, stream->uid);
    return pcmVol;

OnErrorExit:
	free(cardid);
    //AlsaDumpCtlConfig (mixer, "plug-config", pcmConfig, 1);
    AlsaDumpCtlConfig(mixer, "plug-softvol", streamConfig, 1);
    AFB_ApiNotice(mixer->api, "AlsaCreateSoftvol:%s(stream) OnErrorExit", stream->uid);
    return NULL;
}
