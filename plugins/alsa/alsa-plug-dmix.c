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

static int uniqueIpcIndex = 1024;

ALSA_PLUG_PROTO(dmix);


PUBLIC AlsaPcmInfoT* AlsaCreateDmix(CtlSourceT *source, const char* pcmName, AlsaPcmInfoT *pcmSlave) {
   
    snd_config_t *dmixConfig, *slaveConfig, *elemConfig, *pcmConfig;
    AlsaPcmInfoT *pcmPlug= malloc(sizeof(AlsaPcmInfoT));
    pcmPlug->devid= pcmName;
    int error=0;

    // refresh global alsalib config and create PCM top config
    snd_config_update();
    error += snd_config_top(&dmixConfig);
    error += snd_config_set_id (dmixConfig, pcmPlug->devid);
    error += snd_config_imake_string(&elemConfig, "type", "dmix");
    error += snd_config_add(dmixConfig, elemConfig);
    error += snd_config_imake_integer(&elemConfig, "ipc_key", uniqueIpcIndex++);
    error += snd_config_add(dmixConfig, elemConfig);
    if (error) goto OnErrorExit;

    error += snd_config_make_compound(&slaveConfig, "slave", 0);
    error += snd_config_imake_string(&elemConfig, "pcm", pcmSlave->devid);
    error += snd_config_add(slaveConfig, elemConfig);
    if (error) goto OnErrorExit;

    // add leaf into config
    error += snd_config_add(dmixConfig, slaveConfig);
    if (error) goto OnErrorExit;
       
    error = _snd_pcm_dmix_open(&pcmPlug->handle, pcmPlug->devid, snd_config, dmixConfig, SND_PCM_STREAM_PLAYBACK , SND_PCM_NONBLOCK); 
    if (error) {
        AFB_ApiError(source->api, "AlsaCreateDmix: fail to create DMIX=%s SLAVE=%s", pcmPlug->devid, pcmSlave->devid);
        goto OnErrorExit;
    }
    
    error += snd_config_search(snd_config, "pcm", &pcmConfig);    
    error += snd_config_add(pcmConfig, dmixConfig);
    if (error) {
        AFB_ApiError(source->api, "AlsaCreateDmix: fail to add configDMIX=%s", pcmPlug->devid);
        goto OnErrorExit;
    }
    
    // Debug config & pcm
    AlsaDumpCtlConfig (source, dmixConfig, 1);
    //AlsaDumpPcmInfo(source, pcmPlug->handle, pcmPlug->devid);
    AFB_ApiNotice(source->api, "AlsaCreateDmix: %s done", pcmPlug->devid);
    return pcmPlug;

OnErrorExit:
    AFB_ApiNotice(source->api, "AlsaCreateDmix: OnErrorExit");
    return NULL;
}