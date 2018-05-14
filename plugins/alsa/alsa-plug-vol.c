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

ALSA_PLUG_PROTO(softvol);

PUBLIC AlsaPcmInfoT* AlsaCreateVol(CtlSourceT *source, const char *pcmName, AlsaPcmInfoT* ctlTarget, AlsaPcmInfoT* pcmSlave) {

    snd_config_t *volConfig, *elemConfig, *slaveConfig, *controlConfig,*pcmConfig;
    int error = 0;
    AlsaPcmInfoT *pcmPlug= malloc(sizeof(AlsaPcmInfoT));
    pcmPlug->devid= pcmName;

    // refresh global alsalib config and create PCM top config
    snd_config_update();
    error += snd_config_top(&volConfig);
    error += snd_config_set_id (volConfig, pcmPlug->devid);
    error += snd_config_imake_string(&elemConfig, "type", "softvol");
    error += snd_config_add(volConfig, elemConfig);
    if (error) goto OnErrorExit;
    
    // add slave leaf
    error += snd_config_make_compound(&slaveConfig, "slave", 0);
    error += snd_config_imake_string(&elemConfig, "pcm", pcmSlave->devid);
    error += snd_config_add(slaveConfig, elemConfig);
    error += snd_config_add(volConfig, slaveConfig);
    if (error) goto OnErrorExit;
    
    // add control leaf
    error += snd_config_make_compound(&controlConfig, "control", 0);
    error += snd_config_imake_string(&elemConfig, "name", pcmName);
    error += snd_config_add(controlConfig, elemConfig);
    error += snd_config_imake_integer(&elemConfig, "card", ctlTarget->cardid);
    error += snd_config_add(controlConfig, elemConfig);
    error += snd_config_add(volConfig, controlConfig);
    if (error) goto OnErrorExit; 
    
    // update top config to access previous plugin PCM
    snd_config_update();
    
    error = _snd_pcm_softvol_open(&pcmPlug->handle, pcmName, snd_config, volConfig, SND_PCM_STREAM_PLAYBACK , SND_PCM_NONBLOCK); 
    if (error) {
        AFB_ApiError(source->api, "AlsaCreateVol: fail to create Plug=%s Slave=%s", pcmPlug->devid, pcmSlave->devid);
        goto OnErrorExit;
    }
  
    error += snd_config_search(snd_config, "pcm", &pcmConfig);    
    error += snd_config_add(pcmConfig, volConfig);
    if (!error) {
        AFB_ApiError(source->api, "AlsaCreateDmix: fail to add configDMIX=%s", pcmPlug->devid);
        goto OnErrorExit;
    }
    
    // Debug config & pcm
    AlsaDumpCtlConfig (source, volConfig, 1);
    //AlsaDumpPcmInfo(source, pcmPlug->handle, "pcmPlug->handle");
    AFB_ApiNotice(source->api, "AlsaCreateVol: %s done", pcmPlug->devid);
    return pcmPlug;

OnErrorExit:
    AFB_ApiNotice(source->api, "AlsaCreateVol: OnErrorExit");
    AlsaDumpCtlConfig(source, volConfig, 1);
    return NULL;
}