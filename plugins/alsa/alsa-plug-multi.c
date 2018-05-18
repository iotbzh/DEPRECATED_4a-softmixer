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

ALSA_PLUG_PROTO(multi);

PUBLIC AlsaPcmInfoT* AlsaCreateMulti(CtlSourceT *source, const char *pcmUid, int open) {
    SoftMixerHandleT *mixerHandle = (SoftMixerHandleT*) source->context;
    snd_config_t *multiConfig, *elemConfig, *slavesConfig, *slaveConfig, *bindingsConfig, *bindingConfig, *pcmConfig;
    int error = 0, channelIdx=0;
    AlsaPcmInfoT *pcmPlug = calloc(1, sizeof (AlsaPcmInfoT));
    pcmPlug->uid   = pcmUid;
    pcmPlug->cardid = pcmUid;
    
    assert(mixerHandle);
    
    AlsaPcmInfoT* pcmSlaves=mixerHandle->backend;
    if (!pcmSlaves) {
        AFB_ApiError(source->api, "AlsaCreateMulti: No Sound Card find [should Registry snd_cards first]");
        goto OnErrorExit;
    }

    // refresh global alsalib config and create PCM top config
    snd_config_update();
    error += snd_config_top(&multiConfig);
    error += snd_config_set_id (multiConfig, pcmPlug->cardid);
    error += snd_config_imake_string(&elemConfig, "type", "multi");
    error += snd_config_add(multiConfig, elemConfig);
    if (error) goto OnErrorExit;

    error += snd_config_make_compound(&slavesConfig, "slaves", 0);
    error += snd_config_make_compound(&bindingsConfig, "bindings", 0);

    // loop on sound card to include into multi
    for (int idx = 0; pcmSlaves[idx].uid != NULL; idx++) {
        AlsaPcmInfoT* sndcard=&pcmSlaves[idx];
        AlsaPcmChannelT *channels = sndcard->channels;
                
        for (channelIdx=0; channels[channelIdx].uid != NULL; channelIdx++) {
            char idxS[4]; // 999 channel should be more than enough
            snprintf (idxS, sizeof(idxS), "%d", pcmPlug->ccount++);
            // multi does not support to name channels        
            error += snd_config_make_compound(&bindingConfig,idxS, 0);
            error += snd_config_imake_string(&elemConfig, "slave", sndcard->uid);
            error += snd_config_add(bindingConfig, elemConfig);
            error += snd_config_imake_integer(&elemConfig,"channel", channelIdx);
            error += snd_config_add(bindingConfig, elemConfig);
            error += snd_config_add(bindingsConfig, bindingConfig);
        }
        
        error += snd_config_make_compound(&slaveConfig, sndcard->uid, 0);
        error += snd_config_imake_string(&elemConfig, "pcm", sndcard->cardid);
        error += snd_config_add(slaveConfig, elemConfig);
        error += snd_config_imake_integer(&elemConfig, "channels", channelIdx);
        error += snd_config_add(slaveConfig, elemConfig);
        error += snd_config_add(slavesConfig, slaveConfig);       
    }
    
    error += snd_config_add(multiConfig, slavesConfig);
    error += snd_config_add(multiConfig, bindingsConfig);
    if (error) goto OnErrorExit;

    // update top config to access previous plugin PCM
    snd_config_update();

    if (open) error = _snd_pcm_multi_open(&pcmPlug->handle, pcmUid, snd_config, multiConfig, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if (error) {
        AFB_ApiError(source->api, "AlsaCreateMulti: fail to create Plug=%s Error=%s", pcmPlug->cardid, snd_strerror(error));
        goto OnErrorExit;
    }

    error += snd_config_search(snd_config, "pcm", &pcmConfig);
    error += snd_config_add(pcmConfig, multiConfig);
    if (error) {
        AFB_ApiError(source->api, "AlsaCreateDmix: fail to add configDMIX=%s", pcmPlug->cardid);
        goto OnErrorExit;
    }

    // Debug config & pcm
    //AlsaDumpCtlConfig(source, "plug-multi", multiConfig, 1);
    AFB_ApiNotice(source->api, "AlsaCreateMulti: %s done\n", pcmPlug->cardid);
    return pcmPlug;

OnErrorExit:
    AlsaDumpCtlConfig(source, "plug-multi", multiConfig, 1);
    AFB_ApiNotice(source->api, "AlsaCreateMulti: OnErrorExit\n");
    return NULL;
}