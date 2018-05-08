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

PUBLIC snd_pcm_t* AlsaCreateCapture(CtlSourceT *source, const char* sndDevPath, unsigned int deviceIdx, unsigned int subpcmNamex, unsigned int channelCount) {
    char pcmName[32];
    int error;
    snd_pcm_t *pcmHandle;
    snd_pcm_hw_params_t *pcmParams;
    
    AFB_ApiNotice(source->api, "AlsaCreateCapture: start ");
    
        // get card info from /dev/snd/xxx
    snd_ctl_card_info_t *cardInfo = AlsaByPathInfo(source, sndDevPath);
    
    if (!cardInfo) {
        AFB_ApiError(source->api,"AlsaCreateCapture: fail to find sndcard by path= %s", sndDevPath);
        goto OnErrorExit;        
    }

    // extract useful info from cardInfo handle
    int cardIdx = snd_ctl_card_info_get_card(cardInfo);
    const char *cardName = snd_ctl_card_info_get_name(cardInfo);


    // build a valid name and open sndcard
    snprintf(pcmName, sizeof (pcmName), "hw:%i,%i,%i", cardIdx, deviceIdx, subpcmNamex);
    error= snd_pcm_open(&pcmHandle, pcmName, SND_PCM_STREAM_CAPTURE, SND_PCM_ASYNC);
    if (error) {
        AFB_ApiError(source->api,"AlsaCreateCapture: fail openpcm (hw:%d -> %s pcmNamex=%i subpcmNamex=%d): %s"
                , cardIdx, cardName, deviceIdx, subpcmNamex, snd_strerror(error));
        goto OnErrorExit;
    }

    // set default to param object
    snd_pcm_hw_params_alloca(&pcmParams);
    snd_pcm_hw_params_any(pcmHandle, pcmParams);
    
    
    error= snd_pcm_hw_params_set_channels (pcmHandle, pcmParams, channelCount);
    if (error) {
        AFB_ApiError(source->api,"lsaCreateCapture: fail set channel count (hw:%d -> %s pcmNamex=%i subpcmNamex=%d channelCount=%d): %s"
                , cardIdx, cardName, deviceIdx, subpcmNamex, channelCount, snd_strerror(error));
        goto OnErrorExit;
    }
    
    // push selected params to PCM
    error= snd_pcm_hw_params(pcmHandle, pcmParams);
    if (error) {
        AFB_ApiError(source->api,"lsaCreateCapture: fail pushing params (hw:%d -> %s pcmNamex=%i subpcmNamex=%d): %s"
                , cardIdx, cardName, deviceIdx, subpcmNamex, snd_strerror(error));
        goto OnErrorExit;
    }

    AFB_ApiNotice(source->api, "AlsaCreateCapture: done");
    free (cardInfo);
    return NULL;

OnErrorExit:
    AFB_ApiNotice(source->api, "AlsaCreateCapture: OnErrorExit");
    return NULL;
}