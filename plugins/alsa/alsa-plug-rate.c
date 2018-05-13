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


ALSA_PLUG_PROTO(rate);


PUBLIC AlsaPcmInfoT* AlsaCreateRate(CtlSourceT *source, const char* pcmName, AlsaPcmInfoT *pcmSlave, int open) {
   
    snd_config_t *rateConfig, *slaveConfig, *elemConfig, *pcmConfig;
    AlsaPcmInfoT *pcmPlug= calloc(1,sizeof(AlsaPcmInfoT));
    pcmPlug->uid= strdup(pcmName);
    pcmPlug->cardid=pcmPlug->uid;

    int error=0;

    // refresh global alsalib config and create PCM top config
    snd_config_update();
    error += snd_config_top(&rateConfig);
    error += snd_config_set_id (rateConfig, pcmPlug->cardid);
    error += snd_config_imake_string(&elemConfig, "type", "plug");
    error += snd_config_add(rateConfig, elemConfig);
    if (error) goto OnErrorExit;

    error += snd_config_make_compound(&slaveConfig, "slave", 0);
    error += snd_config_imake_string(&elemConfig, "pcm", pcmSlave->cardid);
    if (pcmSlave->params.rate) {
        error += snd_config_add(slaveConfig, elemConfig);
        // *** error += snd_config_imake_integer(&elemConfig, "rate", pcmSlave->params.rate);
        error += snd_config_imake_integer(&elemConfig, "rate", 48000);
    }
    error += snd_config_add(slaveConfig, elemConfig);
    if (error) goto OnErrorExit;

    // add leaf into config
    error += snd_config_add(rateConfig, slaveConfig);
    if (error) goto OnErrorExit;
       
    error += snd_config_search(snd_config, "pcm", &pcmConfig);    
    error += snd_config_add(pcmConfig, rateConfig);
    if (error) {
        AFB_ApiError(source->api, "AlsaCreateRate: fail to add configRATE=%s", pcmPlug->cardid);
        goto OnErrorExit;
    }
    
    if (open) error = _snd_pcm_rate_open(&pcmPlug->handle, pcmPlug->cardid, snd_config, rateConfig, SND_PCM_STREAM_PLAYBACK , SND_PCM_NONBLOCK); 
    if (error) {
        AFB_ApiError(source->api, "AlsaCreateRate: fail to create Rate=%s Slave=%s Error=%s", pcmPlug->cardid, pcmSlave->cardid, snd_strerror(error));
        goto OnErrorExit;
    }
    
    // Debug config & pcm
    AlsaDumpCtlConfig (source, "plug-rate", pcmConfig, 1);
    //AlsaDumpCtlConfig (source, "plug-rate", rateConfig, 1);
    AFB_ApiNotice(source->api, "AlsaCreateRate: %s done\n", pcmPlug->cardid);
    return pcmPlug;

OnErrorExit:
    AlsaDumpCtlConfig(source, "plug-rate", rateConfig, 1);
    AFB_ApiNotice(source->api, "AlsaCreateRate: OnErrorExit\n");
    return NULL;
}