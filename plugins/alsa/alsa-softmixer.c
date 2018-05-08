/*
 * Copyright (C) 2017 "IoT.bzh"
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

// Force Lua2cWrapper inclusion within already existing plugin
CTLP_LUA_REGISTER("alsa-mixer")

// Call at initialisation time
CTLP_ONLOAD(plugin, callbacks) {
	AFB_ApiDebug (plugin->api, "SoftMixer plugin: uid='%s' 'info='%s'", plugin->uid, plugin->info);        
        return NULL;
}

CTLP_LUA2C(AlsaRouter, source, argsJ, responseJ) {
    json_object *devInJ, *devOutJ, *paramsJ=NULL;
    AlsaDevByPathT devIn, devOut;
    int err;
    
    // init default values
    memset(&devIn,0,sizeof(AlsaDevByPathT));    
    memset(&devOut,0,sizeof(AlsaDevByPathT));    
    int rate=ALSA_PCM_DEFAULT_RATE;
    int channels=ALSA_PCM_DEFAULT_CHANNELS;
    snd_pcm_format_t format=ALSA_PCM_DEFAULT_FORMAT;
    snd_pcm_access_t access=ALSA_PCM_DEFAULT_ACCESS;
    
        
    err= wrap_json_unpack(argsJ, "{s:o,s:o,s?o}", "devin", &devInJ, "devout", &devOutJ, "params", &paramsJ);
    if (err) {
        AFB_ApiNotice(source->api, "--lua2c-- AlsaRouter ARGS missing devIn|devOut args=%s", json_object_get_string(argsJ));
        goto OnErrorExit;
    }

    AFB_ApiNotice(source->api, "--lua2c-- AlsaRouter **** PARAMS missing 'format|access|rate|channels' params=%s", json_object_get_string(paramsJ));

    err= wrap_json_unpack(devInJ, "{s?s,s?s,s?i,s?i}", "path", &devIn.devpath, "id",&devIn.devid,"numid",&devIn.numid,"dev", &devIn.device, "sub", &devIn.subdev);
    if (err || (!devIn.devpath && !devIn.devid)) {
        AFB_ApiNotice(source->api, "--lua2c-- AlsaRouter DEV-IN missing 'path|id|dev|sub|numid' devin=%s", json_object_get_string(devInJ));
        goto OnErrorExit;
    }
    
    err= wrap_json_unpack(devOutJ, "{s?s,s?s,s?i, s?i,s?i}", "path", &devOut.devpath, "id", &devOut.device, "sub", &devOut.subdev);
    if (err || (!devOut.devpath && !devOut.devid)) {
        AFB_ApiNotice(source->api, "--lua2c-- AlsaRouter DEV-OUT missing 'path|id|dev|sub' devout=%s", json_object_get_string(devOutJ));
        goto OnErrorExit;
    }
    
    if (paramsJ) if ((err= wrap_json_unpack(paramsJ, "{s?i, s?i, s?i, s?i}", "format", &format, "access", &access, "rate", &rate, "channels",&channels)) != 0) {
        AFB_ApiNotice(source->api, "--lua2c-- AlsaRouter PARAMS missing 'format|access|rate|channels' params=%s", json_object_get_string(paramsJ));
        goto OnErrorExit;
    }
      
    AFB_ApiNotice(source->api, "--lua2c-- AlsaRouter devIn=%s devOut=%s rate=%d channel=%d", devIn.devpath, devOut.devpath, rate, channels);
       
    // open PCM and start frame copy from binder Mainloop
    snd_pcm_t* pcmIn = AlsaByPathOpenPcm(source, &devIn, SND_PCM_STREAM_CAPTURE);
    snd_pcm_t* pcmOut = AlsaByPathOpenPcm(source, &devOut, SND_PCM_STREAM_PLAYBACK);
    err = AlsaPcmCopy(source, pcmIn, pcmOut, format, access, (unsigned int)rate, (unsigned int)channels);
    if(err) goto OnErrorExit;
    
    // Registration to event should be done after pcm_start
    if (devIn.numid) {
        err= AlsaCtlRegister(source, pcmIn, devIn.numid);
        if(err)  goto OnErrorExit;         
    }    
    
    return 0;
    
OnErrorExit:
    return -1;    
}

CTLP_LUA2C(AlsaDmix, source, argsJ, responseJ) {
    json_object* subscribeArgsJ = NULL;

    int err = 0;
    wrap_json_pack(&subscribeArgsJ, "{ss}", "value", "location");
    AFB_ApiNotice(source->api, "lua2c router with %s", json_object_to_json_string_ext(subscribeArgsJ, JSON_C_TO_STRING_PRETTY));

    return err;
}
