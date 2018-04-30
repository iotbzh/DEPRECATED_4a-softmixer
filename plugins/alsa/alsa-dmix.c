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


PUBLIC int AlsaCreateDmix(CtlSourceT *source) {

    AFB_ApiNotice(source->api, "AlsaCreateDmix: start ");
    
    int cardid= AlsaCardInfoByPath ("/dev/snd/by-id/usb-Focusrite_Scarlett_18i8_USB_10004EE6-00");
    AFB_ApiNotice(source->api, "AlsaCreateDmix: cardid=%d ", cardid);

    snd_pcm_t *dmixPcm;
    snd_config_t *dmixConfig, *slaveConfig, *elemConfig;
    snd_pcm_stream_t streamPcm = SND_PCM_STREAM_PLAYBACK;
    int error = 0, streamMode = SND_PCM_NONBLOCK;


    error += snd_config_top(&dmixConfig);
    error += snd_config_imake_integer(&elemConfig, "ipc_key", uniqueIpcIndex++);
    error += snd_config_add(dmixConfig, elemConfig);
    if (error) goto OnErrorExit;

    error += snd_config_make_compound(&slaveConfig, "slave", 0);
    error += snd_config_imake_string(&elemConfig, "pcm", "hw:6");
    error += snd_config_add(slaveConfig, elemConfig);
    if (error) goto OnErrorExit;

    // add leaf into config
    error += snd_config_add(dmixConfig, slaveConfig);
    if (error) goto OnErrorExit;
    
    snd_config_update();
    AlsaDumpConfig (source, snd_config, 1);
    AlsaDumpConfig (source, dmixConfig, 1);


   int status = _snd_pcm_dmix_open(&dmixPcm, "MyDMix", snd_config, dmixConfig, streamPcm , streamMode); 

    AFB_ApiNotice(source->api, "AlsaCreateDmix: done");

    return status;

OnErrorExit:
    AFB_ApiNotice(source->api, "AlsaCreateDmix: OnErrorExit");
    return 1;
}