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


PUBLIC AlsaPcmCtlT* AlsaCreateDmix(SoftMixerT *mixer, const char* pcmName, AlsaSndPcmT *pcmSlave, int open) {
   
    snd_config_t *dmixConfig = NULL, *slaveConfig = NULL, *elemConfig = NULL, *pcmConfig=NULL;
    AlsaPcmCtlT *pcmPlug= calloc(1,sizeof(AlsaPcmCtlT));
    AlsaSndCtlT *sndSlave=pcmSlave->sndcard;
    pcmPlug->cid.cardid=pcmName;

    char * fullPcmName = NULL;
    int error=0;

    AFB_ApiInfo(mixer->api, "%s:  %s, slave %s, cardid %s (dev %d, subdev %d)\n",
    		    __func__,
				pcmName,
				pcmSlave->uid,
				sndSlave->cid.cardid,
				sndSlave->cid.device,
				sndSlave->cid.subdev
				);

    error = asprintf(&fullPcmName,"%s,%d,%d", sndSlave->cid.cardid, sndSlave->cid.device, sndSlave->cid.subdev);
    if (error == -1) {
    	AFB_ApiError(mixer->api,
    	                     "%s: Insufficient memory",
							 __func__);
    	goto OnErrorExit;
    }

    error = snd_config_top(&dmixConfig);
    if (error) goto OnErrorExit;

    error += snd_config_set_id (dmixConfig, pcmName);
    if (error) goto OnErrorExit;

    error += snd_config_imake_string(&elemConfig, "type", "dmix");
    if (error) goto OnErrorExit;
    error += snd_config_add(dmixConfig, elemConfig);
    if (error) goto OnErrorExit;
    error += snd_config_imake_integer(&elemConfig, "ipc_key", uniqueIpcIndex++);
    if (error) goto OnErrorExit;
    error += snd_config_add(dmixConfig, elemConfig);
    if (error) goto OnErrorExit;

    error += snd_config_make_compound(&slaveConfig, "slave", 0);
    if (error) goto OnErrorExit;
    error += snd_config_imake_string(&elemConfig, "pcm", fullPcmName);
    if (error) goto OnErrorExit;
    error += snd_config_add(slaveConfig, elemConfig);
    if (error) goto OnErrorExit;

    if (sndSlave->params->rate) {
        error += snd_config_imake_integer(&elemConfig, "rate", sndSlave->params->rate);
        if (error) goto OnErrorExit;
        error += snd_config_add(slaveConfig, elemConfig);
        if (error) goto OnErrorExit;
    }

    if (sndSlave->params->format) {
        error += snd_config_imake_string(&elemConfig, "format", sndSlave->params->formatS);
        if (error) goto OnErrorExit;
        error += snd_config_add(slaveConfig, elemConfig);
        if (error) goto OnErrorExit;
    }

    /* It is critical to set the right number of channels ... know.
     * Trying to set another value later leads to silent failure
     * */

    error += snd_config_imake_integer(&elemConfig, "channels", pcmSlave->ccount);
    if (error) goto OnErrorExit;
    error += snd_config_add(slaveConfig, elemConfig);
    if (error) goto OnErrorExit;

    // add slave leaf into config
    error += snd_config_add(dmixConfig, slaveConfig);
    if (error) goto OnErrorExit;

    if (open) error = _snd_pcm_dmix_open(&pcmPlug->handle, pcmPlug->cid.cardid, snd_config, dmixConfig, SND_PCM_STREAM_PLAYBACK , SND_PCM_NONBLOCK); 
    if (error) {
        AFB_ApiError(mixer->api,
                     "%s: fail to create Dmix=%s Slave=%s Error=%s",
                     __func__, sndSlave->cid.cardid, sndSlave->cid.cardid, snd_strerror(error));
        goto OnErrorExit;
    }
    
    snd_config_update();
    error += snd_config_search(snd_config, "pcm", &pcmConfig);    
    error += snd_config_add(pcmConfig, dmixConfig);
    if (error) {
        AFB_ApiError(mixer->api, "%s: fail to add configDMIX=%s", __func__, pcmPlug->cid.cardid);
        goto OnErrorExit;
    }
    
    // Debug config & pcm
    AlsaDumpCtlConfig(mixer, "plug-dmix", dmixConfig, 1);
    AFB_ApiNotice(mixer->api, "%s: %s done", __func__, pcmPlug->cid.cardid);
    return pcmPlug;

OnErrorExit:

	free(fullPcmName);

    AlsaDumpCtlConfig(mixer, "plug-pcm", pcmConfig, 1);
    AlsaDumpCtlConfig(mixer, "plug-dmix", dmixConfig, 1);

    AFB_ApiNotice(mixer->api, "%s: FAIL", __func__);
    return NULL;
}
