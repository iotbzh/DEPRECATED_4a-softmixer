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

CTLP_LUA2C(AlsaDmix, source, argsJ, responseJ) {
    json_object* subscribeArgsJ = NULL;

    int error = 0;
    wrap_json_pack(&subscribeArgsJ, "{ss}", "value", "location");
    AFB_ApiNotice(source->api, "lua2c router with %s", json_object_to_json_string_ext(subscribeArgsJ, JSON_C_TO_STRING_PRETTY));

    return error;
}


CTLP_LUA2C(AlsaRouter, source, argsJ, responseJ) {
    json_object *sndInJ, *sndOutJ, *paramsJ=NULL;
    AlsaPcmInfoT *sndIn, *sndOut;
    int error;
    
    // make sndIn/Out a pointer to get cleaner code
    sndIn=calloc(1, sizeof(AlsaPcmInfoT));
    sndOut=calloc(1, sizeof(AlsaPcmInfoT));
    
    // set pcm options to defaults
    AlsaPcmHwInfoT *pcmOpts;
    pcmOpts=calloc(1, sizeof(AlsaPcmHwInfoT));
    pcmOpts->format=SND_PCM_FORMAT_UNKNOWN;
    pcmOpts->access=SND_PCM_ACCESS_RW_INTERLEAVED;
        
    error= wrap_json_unpack(argsJ, "{s:o,s:o,s?o}", "devin", &sndInJ, "devout", &sndOutJ, "params", &paramsJ);
    if (error) {
        AFB_ApiNotice(source->api, "--lua2c-- AlsaRouter ARGS missing devIn|devOut args=%s", json_object_get_string(argsJ));
        goto OnErrorExit;
    }

    error= wrap_json_unpack(sndInJ, "{s?s,s?s,s?i,s?i,s?i}", "path",&sndIn->devpath, "id",&sndIn->devid, "numid",&sndIn->numid, "dev",&sndIn->device, "sub",&sndIn->subdev);
    if (error || (!sndIn->devpath && !sndIn->devid)) {
        AFB_ApiNotice(source->api, "--lua2c-- AlsaRouter DEV-IN missing 'path|id|dev|sub|numid' devin=%s", json_object_get_string(sndInJ));
        goto OnErrorExit;
    }
    
    error= wrap_json_unpack(sndOutJ, "{s?s,s?s,s?i,s?i, s?i}", "path",&sndOut->devpath, "id",&sndOut->devid, "numid",&sndOut->numid,"dev",&sndOut->device, "sub",&sndOut->subdev);
    if (error || (!sndOut->devpath && !sndOut->devid)) {
        AFB_ApiNotice(source->api, "--lua2c-- AlsaRouter DEV-OUT missing 'path|id|dev|sub' devout=%s", json_object_get_string(sndOutJ));
        goto OnErrorExit;
    }
    
    if (paramsJ) if ((error= wrap_json_unpack(paramsJ, "{s?i, s?i, s?i, s?i}", "format", &pcmOpts->format, "access", &pcmOpts->access, "rate", &pcmOpts->rate, "channels",&pcmOpts->channels)) != 0) {
        AFB_ApiNotice(source->api, "--lua2c-- AlsaRouter PARAMS missing 'format|access|rate|channels' params=%s", json_object_get_string(paramsJ));
        goto OnErrorExit;
    }
      
    AFB_ApiNotice(source->api, "--lua2c-- AlsaRouter devin=%s devout=%s rate=%d channel=%d", sndIn->devpath, sndOut->devpath, pcmOpts->rate, pcmOpts->channels);
    
    // Check sndOut Exist and build a valid devid config
    error= AlsaByPathDevid (source, sndOut);
    if (error) goto OnErrorExit;
    
    //AlsaPcmInfoT *pcmOut = AlsaByPathOpenPcm(source, sndOut, SND_PCM_STREAM_PLAYBACK);
    
    // open capture PCM
    AlsaPcmInfoT *pcmIn = AlsaByPathOpenPcm(source, sndIn, SND_PCM_STREAM_CAPTURE);
    if (!pcmIn) goto OnErrorExit;
    
    AlsaPcmInfoT *pcmDmix= AlsaCreateDmix(source, "DmixPlugPcm", sndOut);
    if(!pcmDmix)  goto OnErrorExit;
        
    AlsaPcmInfoT *pcmVol= AlsaCreateVol(source, "SoftVol", sndIn, pcmDmix);
    if(!pcmVol)  goto OnErrorExit;         
            
    error = AlsaPcmCopy(source, pcmIn, pcmVol, pcmOpts);
    if(error) goto OnErrorExit;
    
    // Registration to event should be done after pcm_start
    if (sndIn->numid) {
        error= AlsaCtlRegister(source, pcmIn, sndIn->numid);
        if(error)  goto OnErrorExit;         
    }    
    
    return 0;
    
OnErrorExit:
    AFB_ApiNotice(source->api, "--lua2c-- ERROR AlsaRouter sndIn=%s sndOut=%s rate=%d channel=%d", sndIn->devpath, sndOut->devpath, pcmOpts->rate, pcmOpts->channels);
    return -1;    
}

