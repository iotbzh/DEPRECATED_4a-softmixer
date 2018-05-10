/*
 * Copyright(C) 2018 "IoT.bzh"
 * Author Fulup Ar Foll <fulup@iot.bzh>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http : //www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License

for the specific language governing permissions and
 * limitations under the License.
 *
 * reference :
 * https://github.com/zonque/simple-alsa-loop/blob/master/loop.c
 * https://www.alsa-project.org/alsa-doc/alsa-lib/_2test_2pcm_8c-example.html#a31
 *
 */

#define _GNU_SOURCE  // needed for vasprintf

#include "alsa-softmixer.h"
#include <pthread.h>
#include <sys/syscall.h>



#define BUFFER_FRAME_COUNT 1024

typedef struct {
    snd_pcm_t *pcmIn;
    snd_pcm_t *pcmOut;
    AFB_ApiT api;
    sd_event_source* evtsrc;
    void* buffer;
    size_t frameSize;
    unsigned int frameCount;
    unsigned int channels;
    sd_event *sdLoop;
    pthread_t thread;
    int tid;
    char* info;
} AlsaPcmCopyHandleT;

STATIC int AlsaPeriodSize(snd_pcm_format_t pcmFormat) {
    int pcmSampleSize;

    switch (pcmFormat) {

        case SND_PCM_FORMAT_S8:
        case SND_PCM_FORMAT_U8:
            pcmSampleSize = 1;
            break;

        case SND_PCM_FORMAT_U16_LE:
        case SND_PCM_FORMAT_U16_BE:
        case SND_PCM_FORMAT_S16_LE:
        case SND_PCM_FORMAT_S16_BE:
            pcmSampleSize = 2;
            break;

        case SND_PCM_FORMAT_U32_LE:
        case SND_PCM_FORMAT_U32_BE:
        case SND_PCM_FORMAT_S32_LE:
        case SND_PCM_FORMAT_S32_BE:
            pcmSampleSize = 4;
            break;

        default:
            pcmSampleSize = 0;
    }

    return pcmSampleSize;
}

PUBLIC int AlsaPcmConf(CtlSourceT *source, AlsaPcmInfoT *pcm, AlsaPcmHwInfoT *opts) {
    char string[32];
    int error;
    snd_pcm_hw_params_t *pxmHwParams;
    snd_pcm_sw_params_t *pxmSwParams;

    // retrieve hadware config from PCM
    snd_pcm_hw_params_alloca(&pxmHwParams);
    snd_pcm_hw_params_any(pcm->handle, pxmHwParams);

    if (!opts->access) opts->access = SND_PCM_ACCESS_RW_INTERLEAVED;
    error = snd_pcm_hw_params_set_access(pcm->handle, pxmHwParams, opts->access);
    if (error) {
        AFB_ApiError(source->api, "AlsaPcmConf: Fail PCM=%s Set_Interleave=%d mode error=%s", ALSA_PCM_UID(pcm->handle, string), opts->access, snd_strerror(error));
        goto OnErrorExit;
    };

    if (opts->format != SND_PCM_FORMAT_UNKNOWN) {
        if ((error = snd_pcm_hw_params_set_format(pcm->handle, pxmHwParams, opts->format)) < 0) {
            AFB_ApiError(source->api, "AlsaPcmConf: Fail PCM=%s Set_Format=%d error=%s", ALSA_PCM_UID(pcm->handle, string), opts->format, snd_strerror(error));
            AlsaDumpFormats(source, pcm->handle);
            goto OnErrorExit;
        }
    }

    if (opts->rate > 0) {
        unsigned int pcmRate = opts->rate;
        if ((error = snd_pcm_hw_params_set_rate_near(pcm->handle, pxmHwParams, &opts->rate, 0)) < 0) {
            AFB_ApiError(source->api, "AlsaPcmConf: Fail PCM=%s Set_Rate=%d error=%s", ALSA_PCM_UID(pcm->handle, string), opts->rate, snd_strerror(error));
            goto OnErrorExit;
        }

        // check we got requested rate
        if (opts->rate != pcmRate) {
            AFB_ApiError(source->api, "AlsaPcmConf: Fail PCM=%s Set_Rate ask=%dHz get=%dHz", ALSA_PCM_UID(pcm->handle, string), pcmRate, opts->rate);
            goto OnErrorExit;
        }
    }

    if (opts->channels) {
        if ((error = snd_pcm_hw_params_set_channels(pcm->handle, pxmHwParams, opts->channels)) < 0) {
            AFB_ApiError(source->api, "AlsaPcmConf: Fail PCM=%s Set_Channels=%d error=%s", ALSA_PCM_UID(pcm->handle, string), opts->channels, snd_strerror(error));
            goto OnErrorExit;
        };
    }

    // store selected values
    if ((error = snd_pcm_hw_params(pcm->handle, pxmHwParams)) < 0) {
        AFB_ApiError(source->api, "AlsaPcmConf: Fail PCM=%s apply hwparams error=%s", ALSA_PCM_UID(pcm->handle, string), snd_strerror(error));
        goto OnErrorExit;
    }

    // check we effective hw params after optional format change
    snd_pcm_hw_params_get_channels(pxmHwParams, &opts->channels);
    snd_pcm_hw_params_get_format(pxmHwParams, &opts->format);
    snd_pcm_hw_params_get_rate(pxmHwParams, &opts->rate, 0);
    opts->sampleSize = AlsaPeriodSize(opts->format);
    if (opts->sampleSize == 0) {
        AFB_ApiError(source->api, "AlsaPcmConf: Fail PCM=%s unsupported format format=%d", ALSA_PCM_UID(pcm->handle, string), opts->format);
        goto OnErrorExit;
    }

    // retrieve software config from PCM
    snd_pcm_sw_params_alloca(&pxmSwParams);
    snd_pcm_sw_params_current(pcm->handle, pxmSwParams);

    if ((error = snd_pcm_sw_params_set_avail_min(pcm->handle, pxmSwParams, 16)) < 0) {
        AFB_ApiError(source->api, "AlsaPcmConf: Fail to PCM=%s set_buffersize error=%s", ALSA_PCM_UID(pcm->handle, string), snd_strerror(error));
        goto OnErrorExit;
    };

    // push software params into PCM
    if ((error = snd_pcm_sw_params(pcm->handle, pxmSwParams)) < 0) {
        AFB_ApiError(source->api, "AlsaPcmConf: Fail to push software=%s params error=%s", ALSA_PCM_UID(pcm->handle, string), snd_strerror(error));
        goto OnErrorExit;
    };

    AFB_ApiNotice(source->api, "AlsaPcmConf: PCM=%s channels=%d rate=%d format=%d access=%d done", ALSA_PCM_UID(pcm->handle,string), opts->channels, opts->rate, opts->format, opts->access);
    return 0;

OnErrorExit:
    return -1;
}

STATIC int AlsaPcmReadCB(sd_event_source* src, int fd, uint32_t revents, void* userData) {
    char string[32];
    int error;
    snd_pcm_sframes_t framesIn, framesOut, availIn, availOut;
    AlsaPcmCopyHandleT *pcmCopyHandle = (AlsaPcmCopyHandleT*) userData;

    // PCM has was closed
    if ((revents & EPOLLHUP) != 0) {
        AFB_ApiNotice(pcmCopyHandle->api, "AlsaPcmReadCB PCM=%s hanghup/disconnected", ALSA_PCM_UID(pcmCopyHandle->pcmIn, string));
        goto ExitOnSuccess;
    }

    // ignore any non input events
    if ((revents & EPOLLIN) == 0) {
        goto ExitOnSuccess;
    }

    // retrieve PCM state
    snd_pcm_state_t pcmState = snd_pcm_state(pcmCopyHandle->pcmIn);
    
    // When pause flush remaining frame and wait
    if (pcmState == SND_PCM_STATE_PAUSED) {
        framesIn = snd_pcm_readi(pcmCopyHandle->pcmIn, pcmCopyHandle->buffer, pcmCopyHandle->frameCount);
        AFB_ApiInfo(pcmCopyHandle->api, "AlsaPcmReadCB: paused frame:%ld ignored", framesIn);
        goto ExitOnSuccess;
    }

    // When XRNS append try to restore PCM
    if (pcmState == SND_PCM_STATE_XRUN) {
        AFB_ApiNotice(pcmCopyHandle->api, "AlsaPcmReadCB PCM=%s XRUN", ALSA_PCM_UID(pcmCopyHandle->pcmIn, string));
        snd_pcm_prepare(pcmCopyHandle->pcmIn);
    }

    // when PCM suspending loop until ready to go
    if (pcmState == SND_PCM_STATE_SUSPENDED) {
        while (1) {
            if ((error = snd_pcm_resume(pcmCopyHandle->pcmIn)) < 0) {
                AFB_ApiNotice(pcmCopyHandle->api, "AlsaPcmReadCB PCM=%s SUSPENDED fail to resume", ALSA_PCM_UID(pcmCopyHandle->pcmIn, string));
                sleep(1); // Fulup should be replace with corresponding AFB_timer
            } else {
                AFB_ApiNotice(pcmCopyHandle->api, "AlsaPcmReadCB PCM=%s SUSPENDED success to resume", ALSA_PCM_UID(pcmCopyHandle->pcmIn, string));
            }
        }
    }

    // do we have waiting frame
    availIn = snd_pcm_avail_update(pcmCopyHandle->pcmIn);
    if (availIn <= 0) {
        goto ExitOnSuccess;
    }

    // do we have space to push frame
    availOut = snd_pcm_avail_update(pcmCopyHandle->pcmOut);
    if (availOut <= 0) {
        snd_pcm_prepare(pcmCopyHandle->pcmOut);
        goto ExitOnSuccess;
    }

    // make sure we can push all input frame into output pcm without locking
    if (availOut < availIn) availIn = availOut;

    // we get too many data ignore some
    if (availIn > pcmCopyHandle->frameCount) {
        availIn = pcmCopyHandle->frameCount;
    }

    // effectively read pcmIn and push frame to pcmOut
    framesIn = snd_pcm_readi(pcmCopyHandle->pcmIn, pcmCopyHandle->buffer, availIn);
    if (framesIn < 0 || framesIn != availIn) {
        AFB_ApiNotice(pcmCopyHandle->api, "AlsaPcmReadCB PcmIn=%s UNDERUN frame=%ld", ALSA_PCM_UID(pcmCopyHandle->pcmIn, string), framesIn);
        goto ExitOnSuccess;
    }

    // In/Out frames transfer through buffer copy
    framesOut = snd_pcm_writei(pcmCopyHandle->pcmOut, pcmCopyHandle->buffer, framesIn);
    if (framesOut < 0 || framesOut != framesIn) {
        AFB_ApiNotice(pcmCopyHandle->api, "AlsaPcmReadCB PcmOut=%s UNDERUN/SUSPEND frameOut=%ld", ALSA_PCM_UID(pcmCopyHandle->pcmOut, string), framesOut);
        goto ExitOnSuccess;
    }

    if (framesIn != framesOut) {
        AFB_ApiNotice(pcmCopyHandle->api, "AlsaPcmReadCB PCM=%s Loosing frames=%ld", ALSA_PCM_UID(pcmCopyHandle->pcmOut, string), (framesIn - framesOut));
        goto ExitOnSuccess;
    }

    return 0;

    // Cannot handle error in callback
ExitOnSuccess:
    return 0;
}

static void *LoopInThread(void *handle) {
    AlsaPcmCopyHandleT *pcmCopyHandle = (AlsaPcmCopyHandleT*) handle;
    int count = 0;
    int watchdog = MAINLOOP_WATCHDOG * 1000;
    pcmCopyHandle->tid = (int) syscall(SYS_gettid);

    AFB_ApiNotice(pcmCopyHandle->api, "LoopInThread:%s/%d Started", pcmCopyHandle->info, pcmCopyHandle->tid);


    /* loop until end */
    for (;;) {
        int res = sd_event_run(pcmCopyHandle->sdLoop, watchdog);
        if (res == 0) {
            AFB_ApiInfo(pcmCopyHandle->api, "LoopInThread:%s/%d Idle count=%d", pcmCopyHandle->info, pcmCopyHandle->tid, count++);
            continue;
        }
        if (res < 0) {
            AFB_ApiError(pcmCopyHandle->api, "LoopInThread:%s/%d ERROR=%i Exit errno=%s.\n", pcmCopyHandle->info, pcmCopyHandle->tid, res, strerror(res));
            break;
        }
    }
    pthread_exit(0);
}

PUBLIC int AlsaPcmCopy(CtlSourceT *source, AlsaPcmInfoT *pcmIn, AlsaPcmInfoT *pcmOut, AlsaPcmHwInfoT * opts) {
    char string[32];
    struct pollfd *pcmInFds; 
    int error;

    // prepare PCM for capture and replay
    error = AlsaPcmConf(source, pcmIn, opts);
    if (error) goto OnErrorExit;

    // Prepare PCM for usage
    if ((error = snd_pcm_start(pcmIn->handle)) < 0) {
        AFB_ApiError(source->api, "AlsaPcmCopy: Fail to prepare PCM=%s error=%s", ALSA_PCM_UID(pcmIn->handle, string), snd_strerror(error));
        goto OnErrorExit;
    };

    
    error = AlsaPcmConf(source, pcmOut, opts);
    if (error) goto OnErrorExit;

    // Prepare PCM for usage
    if ((error = snd_pcm_prepare(pcmOut->handle)) < 0) {
        AFB_ApiError(source->api, "AlsaPcmCopy: Fail to start PCM=%s error=%s", ALSA_PCM_UID(pcmOut->handle, string), snd_strerror(error));
        goto OnErrorExit;
    };
    


    AlsaPcmCopyHandleT *pcmCopyHandle = malloc(sizeof (AlsaPcmCopyHandleT));
    pcmCopyHandle->info = "pcmCpy";
    pcmCopyHandle->pcmIn = pcmIn->handle;
    pcmCopyHandle->pcmOut = pcmOut->handle;
    pcmCopyHandle->api = source->api;
    pcmCopyHandle->channels = opts->channels;
    pcmCopyHandle->frameSize = opts->channels * opts->sampleSize;
    pcmCopyHandle->frameCount = BUFFER_FRAME_COUNT;
    pcmCopyHandle->buffer = malloc(pcmCopyHandle->frameCount * pcmCopyHandle->frameSize);

    // get FD poll descriptor for capture PCM
    int pcmInCount = snd_pcm_poll_descriptors_count(pcmCopyHandle->pcmIn);
    if (pcmInCount <= 0) {
        AFB_ApiError(source->api, "AlsaPcmCopy: Fail pcmIn=%s get fds count error=%s", ALSA_PCM_UID(pcmIn->handle, string), snd_strerror(error));
        goto OnErrorExit;
    };

    pcmInFds = alloca(sizeof (*pcmInFds) * pcmInCount);
    if ((error = snd_pcm_poll_descriptors(pcmIn->handle, pcmInFds, pcmInCount)) < 0) {
        AFB_ApiError(source->api, "AlsaPcmCopy: Fail pcmIn=%s get pollfds error=%s", ALSA_PCM_UID(pcmOut->handle, string), snd_strerror(error));
        goto OnErrorExit;
    };

    // add poll descriptor to AGL systemd mainloop
    if ((error = sd_event_new(&pcmCopyHandle->sdLoop)) < 0) {
        fprintf(stderr, "LaunchCallRequest: fail pcmin=%s creating a new loop: %s\n", ALSA_PCM_UID(pcmOut->handle, string), strerror(error));
        goto OnErrorExit;
    }

    for (int idx = 0; idx < pcmInCount; idx++) {
        if ((error = sd_event_add_io(pcmCopyHandle->sdLoop, &pcmCopyHandle->evtsrc, pcmInFds[idx].fd, EPOLLIN, AlsaPcmReadCB, pcmCopyHandle)) < 0) {
            AFB_ApiError(source->api, "AlsaPcmCopy: Fail pcmIn=%s sd_event_add_io err=%d", ALSA_PCM_UID(pcmIn->handle, string), error);
            goto OnErrorExit;
        }
    }

    // start a thread with a mainloop to monitor Audio-Agent
    if ((error = pthread_create(&pcmCopyHandle->thread, NULL, &LoopInThread, pcmCopyHandle)) < 0) {
        AFB_ApiError(source->api, "AlsaPcmCopy: Fail create waiting thread pcmIn=%s err=%d", ALSA_PCM_UID(pcmIn->handle, string), error);
        goto OnErrorExit;
    }

    return 0;

OnErrorExit:
    AFB_ApiError(source->api, "AlsaPcmCopy: Fail \n - pcmIn=%s \n - pcmOut=%s", ALSA_PCM_UID(pcmIn->handle, string), ALSA_PCM_UID(pcmOut->handle, string));

    return -1;
}

