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
#include <sched.h>

#include "time_utils.h"

static int xrun(snd_pcm_t * pcm, int error);
static int suspend(snd_pcm_t * pcm, int error);


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

        case SND_PCM_FORMAT_U24_LE:
        case SND_PCM_FORMAT_U24_BE:
        case SND_PCM_FORMAT_S24_LE:
        case SND_PCM_FORMAT_S24_BE:
            pcmSampleSize = 3;
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

PUBLIC int AlsaPcmConf(SoftMixerT *mixer, AlsaPcmCtlT *pcm, int mode) {
    int error;
    snd_pcm_hw_params_t *pxmHwParams;
    snd_pcm_sw_params_t *pxmSwParams;
    snd_pcm_format_t format;
    snd_pcm_access_t access;

    AlsaPcmHwInfoT * opts = pcm->params;

    const char * modeS = mode==SND_PCM_STREAM_PLAYBACK?"PLAYBACK":"CAPTURE";

    AFB_ApiInfo(mixer->api,
                "%s: mixer info %s uid %s , pcm %s, mode %s",
                __func__, mixer->info, mixer->uid, pcm->cid.cardid, modeS);

    // retrieve hardware config from PCM
    snd_pcm_hw_params_alloca(&pxmHwParams);
    error = snd_pcm_hw_params_any(pcm->handle, pxmHwParams);
    if (error < 0) {
        AFB_ApiError(mixer->api, "%s: Failed to get parameters: %s", __func__, snd_strerror(error));
        goto OnErrorExit;
    }

    AFB_ApiDebug(mixer->api, "PARAMS before:\n");
    AlsaDumpPcmParams(mixer, pxmHwParams);

    if (!opts->access)
         opts->access = SND_PCM_ACCESS_RW_INTERLEAVED;

    snd_pcm_hw_params_get_access(pxmHwParams, &access);
    error = snd_pcm_hw_params_set_access(pcm->handle, pxmHwParams, opts->access);
    if (error) {
        AFB_ApiError(mixer->api,
                     "%s set_access failed (ignore this error): mixer=%s cardid=%s access=%d Fail current=%d mode error=%s",
                     __func__, mixer->uid, pcm->cid.cardid, opts->access, access, snd_strerror(error));
//Fulup        goto OnErrorExit;
    };

    if (opts->format != SND_PCM_FORMAT_UNKNOWN) {
        snd_pcm_hw_params_get_format(pxmHwParams, &format);
        if ((error = snd_pcm_hw_params_set_format(pcm->handle, pxmHwParams, opts->format)) < 0) {
            AFB_ApiError(mixer->api,
                         "%s: mixer=%s cardid=%s Set_Format=%s (%d) FAILED current=%d error=%s",
                         __func__, mixer->uid, pcm->cid.cardid, opts->formatS, opts->format, format, snd_strerror(error));
            AlsaDumpFormats(mixer, pcm->handle);
            goto OnErrorExit;
        }
    }

    if (opts->rate > 0 ) {

    	AFB_ApiInfo(mixer->api," %s: set rate to %d", __func__, opts->rate);
        unsigned int pcmRate = opts->rate;
        /* Attempt to set the rate. Failing on a capture dev is acceptable */
        error = snd_pcm_hw_params_set_rate_near(pcm->handle, pxmHwParams, &opts->rate, 0);
        if ( mode == SND_PCM_STREAM_PLAYBACK && error < 0) {
            AFB_ApiError(mixer->api,
            		     "%s: mixer=%s cardid=%s FailSet_Rate=%d error=%s",
						 __func__, mixer->uid, pcm->cid.cardid, opts->rate, snd_strerror(error));
            goto OnErrorExit;
        }

        // check we got requested rate
        if (mode == SND_PCM_STREAM_PLAYBACK && opts->rate != pcmRate) {
            AFB_ApiError(mixer->api,
            		     "%s: mixer=%s cardid=%s Set_Rate Fail ask=%dHz get=%dHz",
						 __func__, mixer->uid, pcm->cid.cardid,pcmRate, opts->rate);
            goto OnErrorExit;
        }
    }

    if (opts->channels) {
        if ((error = snd_pcm_hw_params_set_channels(pcm->handle, pxmHwParams, opts->channels)) < 0) {
            AFB_ApiError(mixer->api,
            		     "%s: mixer=%s cardid=%s Set_Channels=%d Fail error=%s",
						 __func__, mixer->uid, pcm->cid.cardid, opts->channels, snd_strerror(error));
            goto OnErrorExit;
        };
    }

    /* The following code, that
     * 1) sets period time/size; buffer time/size hardware params
     * 2) sets start and stop threashold in software params
     * ... is taken as such from 'aplay' in alsa-utils */

    unsigned buffer_time = 0;
    unsigned period_time = 0;
    snd_pcm_uframes_t buffer_frames = 0;
    snd_pcm_uframes_t period_frames = 0;

	error = snd_pcm_hw_params_get_buffer_time_max(pxmHwParams, &buffer_time, 0);

	printf("HW_BUFFER_TIME MAX is %d\n", buffer_time);

	if (buffer_time > 500000)
		buffer_time = 500000;

	if (period_time == 0 && period_frames == 0) {
		if (buffer_time > 0)
			period_time = buffer_time / 4;
		else
			period_frames = buffer_frames / 4;
	}

	if (period_time > 0) {
		printf("SET PERIOD TIME to %d\n", period_time);
		error = snd_pcm_hw_params_set_period_time_near(pcm->handle, pxmHwParams, &period_time, 0);
	}
	else {
		printf("SET PERIOD SIZE\n");
		error = snd_pcm_hw_params_set_period_size_near(pcm->handle, pxmHwParams, &period_frames, 0);
	}

	if (error < 0) {
        AFB_ApiError(mixer->api,
        		     "%s: mixer=%s cardid=%s Fail to set period in hwparams error=%s",
        		     __func__, mixer->uid, pcm->cid.cardid, snd_strerror(error));
        goto OnErrorExit;
	}

	if (buffer_time > 0) {
		printf("SET BUFFER TIME to %d\n", buffer_time);
		error = snd_pcm_hw_params_set_buffer_time_near(pcm->handle, pxmHwParams, &buffer_time, 0);
	} else {
		printf("SET BUFFER SIZE\n");
		error = snd_pcm_hw_params_set_buffer_size_near(pcm->handle, pxmHwParams, &buffer_frames);
	}

	if (error < 0) {
        AFB_ApiError(mixer->api,
        		     "%s: mixer=%s cardid=%s Fail to set buffer in hwparams error=%s",
        		     __func__, mixer->uid, pcm->cid.cardid, snd_strerror(error));
        goto OnErrorExit;
	}

    // store selected values
    if ((error = snd_pcm_hw_params(pcm->handle, pxmHwParams)) < 0) {
        AFB_ApiError(mixer->api,
        		     "%s: mixer=%s cardid=%s Fail to apply hwparams error=%s",
        		     __func__, mixer->uid, pcm->cid.cardid, snd_strerror(error));
        goto OnErrorExit;
    }

    AFB_ApiDebug(mixer->api, "PARAMS after:\n");
    AlsaDumpPcmParams(mixer, pxmHwParams);

    // check we effective hw params after optional format change
    snd_pcm_hw_params_get_channels(pxmHwParams, &opts->channels);
    snd_pcm_hw_params_get_format(pxmHwParams, &opts->format);
    snd_pcm_hw_params_get_rate(pxmHwParams, &opts->rate, 0);

	AFB_ApiInfo(mixer->api, "rate is %d", opts->rate);

    opts->sampleSize = AlsaPeriodSize(opts->format);
    if (opts->sampleSize == 0) {
        AFB_ApiError(mixer->api,
                     "%s: mixer=%s cardid=%s Fail unsupported format format=%d",
                     __func__, mixer->uid, pcm->cid.cardid, opts->format);
        goto OnErrorExit;
    }

    snd_pcm_uframes_t chunk_size, buffer_size;

    snd_pcm_hw_params_get_period_size(pxmHwParams, &chunk_size, 0);
    snd_pcm_hw_params_get_buffer_size(pxmHwParams, &buffer_size);
    if (chunk_size == buffer_size) {
    	AFB_ApiError(mixer->api,
    			     "Can't use period equal to buffer size (%lu == %lu)",
    	              chunk_size, buffer_size);
    	goto OnErrorExit;
    }

    int avail_min = -1;
    size_t n;
    int rate = opts->rate;

	if (avail_min < 0)
		n = chunk_size;
	else
		n = (size_t) ((double)rate * avail_min / 1000000);

    // retrieve software config from PCM
    snd_pcm_sw_params_alloca(&pxmSwParams);
    snd_pcm_sw_params_current(pcm->handle, pxmSwParams);

    // available_min is the minimum number of frames available to read,
    // when the call to poll returns. Assume this is the same for playback (not checked that)

    if ((error = snd_pcm_sw_params_set_avail_min(pcm->handle, pxmSwParams, n)) < 0) {
        AFB_ApiError(mixer->api,
                     "%s: mixer=%s cardid=%s Fail set_buffersize error=%s",
					 __func__, mixer->uid, pcm->cid.cardid, snd_strerror(error));
        goto OnErrorExit;
    };

    snd_pcm_sw_params_get_avail_min(pxmSwParams, &pcm->avail_min);

    int start_delay = 0;
	snd_pcm_uframes_t start_threshold;

    /* round up to closest transfer boundary */
    n = buffer_size;
    if (start_delay <= 0) {
    	start_threshold = n + (size_t)((double)rate * start_delay / 1000000);
    } else
    	start_threshold = (size_t)((double)rate * start_delay / 1000000);
    if (start_threshold < 1)
    	start_threshold = 1;

    if (start_threshold > n/2)
    	start_threshold = n/2;

    printf("CALCULATED START THRESHOLD: %ld\n", start_threshold);

	if (mode == SND_PCM_STREAM_PLAYBACK) {
		start_threshold = 1;
	}

	AFB_ApiInfo(mixer->api, "%s: Set start threshold to %ld", modeS, start_threshold);
   	error = snd_pcm_sw_params_set_start_threshold(pcm->handle, pxmSwParams, start_threshold);
   	if (error < 0) {
   		AFB_ApiError(mixer->api,
   	             	 "%s: mixer=%s cardid=%s failed set start_threshold, error=%s",
					 __func__, mixer->uid, pcm->cid.cardid, snd_strerror(error));
   		goto OnErrorExit;
   	}

    // push software params into PCM
    if ((error = snd_pcm_sw_params(pcm->handle, pxmSwParams)) < 0) {
        AFB_ApiError(mixer->api,
        		     "%s: mixer=%s cardid=%s Fail to push SW params error=%s",
					 __func__, mixer->uid, pcm->cid.cardid, snd_strerror(error));
        goto OnErrorExit;
    };

    AFB_ApiNotice(mixer->api,
    		      "%s: mixer=%s cardid=%s Done channels=%d rate=%d format=%d access=%d ... done !",
				  __func__, mixer->uid, pcm->cid.cardid, opts->channels, opts->rate, opts->format, opts->access);
    return 0;

OnErrorExit:
    return -1;
}

STATIC int AlsaPcmReadCB( struct pollfd * pfd, AlsaPcmCopyHandleT * pcmCopyHandle) {
	char string[32];

	snd_pcm_sframes_t availIn;
	snd_pcm_t * pcmIn = pcmCopyHandle->pcmIn->handle;
	alsa_ringbuf_t * rbuf = pcmCopyHandle->rbuf;
	snd_pcm_uframes_t bufSize = alsa_ringbuf_buffer_size(rbuf);

	int err;

	// PCM has was closed
	if ((pfd->revents & POLLHUP) != 0) {
		AFB_ApiNotice(pcmCopyHandle->api,
				"%s PCM=%s hanghup/disconnected",
				__func__, ALSA_PCM_UID(pcmIn, string));
		goto ExitOnSuccess;
	}

	// ignore any non input events. This is not supposed to happen ever
	if ((pfd->revents & EPOLLIN) == 0) {
		goto ExitOnSuccess;
	}

	// do we have waiting frames ?
	availIn = snd_pcm_avail_update(pcmIn);
	if (availIn <= 0) {
		if (availIn == -EPIPE) {
			int ret = xrun(pcmIn, (int)availIn);
			AFB_ApiDebug(pcmCopyHandle->api, "XXX read EPIPE (recov=%d) {%s}!", ret, ALSA_PCM_UID(pcmIn, string));

			// For some (undocumented...) reason, a start is mandatory.
			snd_pcm_start(pcmIn);
		}
		goto ExitOnSuccess;
	}

	pthread_mutex_lock(&pcmCopyHandle->mutex);
	snd_pcm_sframes_t availInBuf = alsa_ringbuf_frames_free(rbuf);
	pthread_mutex_unlock(&pcmCopyHandle->mutex);

	/* we get too much data, take what we can now,
	 * hopefully we will have more luck next time */

	if (availIn > availInBuf) {
//		printf("INCOMING BUFFER TOO SMALL !\n");
		availIn = availInBuf;
	}

	while (true) {

		pthread_mutex_lock(&pcmCopyHandle->mutex);
		snd_pcm_sframes_t r = alsa_ringbuf_frames_free(rbuf);

		if (r <= 0) {
			pthread_mutex_unlock(&pcmCopyHandle->mutex);
			// Wake up the reader, in case it is sleeping,
			// that lets it an opportunity to pop something.
			sem_post(&pcmCopyHandle->sem);
			break;
		}

		if (r < availIn)
			r = availIn;

		char buf[r*pcmCopyHandle->frame_size];
		pthread_mutex_unlock(&pcmCopyHandle->mutex);

		r = snd_pcm_readi(pcmIn, buf, r);

		if (r == 0) {
			break;
		}
		if (r < 0) {
			if (r == -EPIPE) {
				err = xrun(pcmIn, (int)r);
				AFB_ApiDebug(pcmCopyHandle->api, "read EPIPE (%d), recov %d", ++pcmCopyHandle->read_err_count, err);
				goto ExitOnSuccess;
			} else if (r == -ESTRPIPE) {
				AFB_ApiDebug(pcmCopyHandle->api, "read ESTRPIPE");
				if ((err = suspend(pcmIn, (int)r)) < 0)
					goto ExitOnSuccess;
				r = 0;
			} else {
				goto ExitOnSuccess;
			}
		}
		pthread_mutex_lock(&pcmCopyHandle->mutex);
		alsa_ringbuf_frames_push(rbuf, buf, r);
		snd_pcm_uframes_t used = alsa_ringbuf_frames_used(rbuf);
		pthread_mutex_unlock(&pcmCopyHandle->mutex);

		// Wait for having the buffer full enough before waking up the playback
		// else it will starve immediately.
		if (used > 0.8 * (double)bufSize) {
			sem_post(&pcmCopyHandle->sem);
		}

		availIn -= r;

		// completed, we have read everything
		if (availIn <= 0) {
			break;
		}

	}

ExitOnSuccess:
	return 0;
}


static int xrun( snd_pcm_t * pcm, int error)
{
	int err;

	if ((err = snd_pcm_recover(pcm, error, 1)) < 0) {
		return err;
	}
	return 0;
}

static int suspend( snd_pcm_t * pcm, int error)
{
	int err;

	while ((err = snd_pcm_resume(pcm)) == -EAGAIN) {
		usleep(1);
	}
	if (err < 0)
		return xrun(pcm, error);
	return 0;
}


static void readSuspend(AlsaPcmCopyHandleT * pcmCopyHandle) {

	// will be deaf
	pcmCopyHandle->saveFd = pcmCopyHandle->pollFds[1].fd;
	pcmCopyHandle->pollFds[1].fd = -1;

	AFB_ApiNotice(pcmCopyHandle->api, "capture muted");
}

static void readResume(AlsaPcmCopyHandleT * pcmCopyHandle) {

	// undeaf it
	pcmCopyHandle->pollFds[1].fd = pcmCopyHandle->saveFd;
	snd_pcm_prepare(pcmCopyHandle->pcmIn->handle);
	snd_pcm_start(pcmCopyHandle->pcmIn->handle);
	AFB_ApiNotice(pcmCopyHandle->api, "capture unmuted");
}


static void *readThreadEntry(void *handle) {
#define LOOP_TIMEOUT_MSEC	10*1000 /* 10 seconds */

    AlsaPcmCopyHandleT *pcmCopyHandle = (AlsaPcmCopyHandleT*) handle;
    pcmCopyHandle->tid = (int) syscall(SYS_gettid);

    AFB_ApiNotice(pcmCopyHandle->api,
                  "%s :%s/%d Started, muted=%d",
                  __func__, pcmCopyHandle->info, pcmCopyHandle->tid, pcmCopyHandle->pcmIn->mute);

   	struct pollfd * mutePfd =  &pcmCopyHandle->pollFds[0];
   	struct pollfd * framePfd = &pcmCopyHandle->pollFds[1];

   	mutePfd->events  = POLLIN | POLLHUP;
   	framePfd->events = POLLIN | POLLHUP;

   	bool muted = pcmCopyHandle->pcmIn->mute;

  	if (muted)
   		readSuspend(pcmCopyHandle);

    /* loop until end */
    for (;;) {

  	   	int err = poll(pcmCopyHandle->pollFds, pcmCopyHandle->nbPcmFds, LOOP_TIMEOUT_MSEC);
    	if (err < 0) {
    		AFB_ApiError(pcmCopyHandle->api, "%s: poll err %s", __func__, strerror(errno));
    		continue;
    	}

    	if (err == 0) {
    		/* timeout */
    		AFB_ApiDebug(pcmCopyHandle->api, "%s(%s) alive, mute %d", __func__, pcmCopyHandle->pcmIn->cid.cardid, muted );
    		continue;
    	}

    	// handle the un/mute order
    	if ((mutePfd->revents & EPOLLIN) != 0) {
    		bool mute;

    		size_t ret = read(mutePfd->fd, &mute, sizeof(mute));
    		if (ret <= 0)
    			continue;

    		if (mute == muted)
    			continue;

    		muted = mute;

    		if (muted) {
    	   		readSuspend(pcmCopyHandle);
    		} else {
    			readResume(pcmCopyHandle);
    		}
    		continue;
    	}

    	unsigned short revents;

    	int ret = snd_pcm_poll_descriptors_revents(pcmCopyHandle->pcmIn->handle, &pcmCopyHandle->pollFds[1], 1, &revents);

    	if (ret == -ENODEV) {
    		sleep(1);
    		continue;
    	}

    	if (framePfd->revents & POLLHUP) {
    		AFB_ApiNotice(pcmCopyHandle->api, "Frame POLLHUP");
    		continue;
    	}

   		AlsaPcmReadCB(&pcmCopyHandle->pollFds[1], pcmCopyHandle);
    }

	pthread_exit(0);
	return NULL;
}


static void *writeThreadEntry(void *handle) {
    AlsaPcmCopyHandleT *pcmCopyHandle = (AlsaPcmCopyHandleT*) handle;
	snd_pcm_t * pcmOut = pcmCopyHandle->pcmOut->handle;

	alsa_ringbuf_t * rbuf = pcmCopyHandle->rbuf;

	const snd_pcm_sframes_t threshold = 1000;

	for (;;) {

		sem_wait(&pcmCopyHandle->sem);

		while (true) {
			snd_pcm_sframes_t r;
			snd_pcm_sframes_t availOut = snd_pcm_avail(pcmOut);

			if (availOut < 0) {
				if (availOut == -EPIPE) {
					AFB_ApiDebug(pcmCopyHandle->api, "write update EPIPE");
					xrun(pcmOut, (int)availOut);
					continue;
				}
				if (availOut == -ESTRPIPE) {
					AFB_ApiDebug(pcmCopyHandle->api, "write update ESTRPIPE");
					suspend(pcmOut, (int)availOut);
					continue;
				}
			}

			// no space for output
			if (availOut <= threshold) {
				usleep(500);
				continue;
			}

			pthread_mutex_lock(&pcmCopyHandle->mutex);
			r = alsa_ringbuf_frames_used(rbuf);
			if (r <= 0) {
				pthread_mutex_unlock(&pcmCopyHandle->mutex);
				break; // will wait again
			}

			if (r > availOut)
				r = availOut;

			char buf[r*pcmCopyHandle->frame_size];
			alsa_ringbuf_frames_pop(rbuf, buf, r);
			pthread_mutex_unlock(&pcmCopyHandle->mutex);

			r = snd_pcm_writei( pcmOut, buf, r);
			if (r <= 0) {
				if (r == -EPIPE) {
					int err = xrun(pcmOut, (int)r);
					AFB_ApiDebug(pcmCopyHandle->api, "XXX write EPIPE (%d), recov %d", ++pcmCopyHandle->write_err_count , err);

					continue;
				} else if (r == -ESTRPIPE) {
					AFB_ApiDebug(pcmCopyHandle->api, "XXX write ESTRPIPE");
					break;
				}
				AFB_ApiDebug(pcmCopyHandle->api, "Unhandled error %s", strerror(errno));
				break;
			}

		}

	}

   	pthread_exit(0);
   	return NULL;
}


PUBLIC int AlsaPcmCopyMuteSignal(SoftMixerT *mixer, AlsaPcmCtlT *pcmIn, bool mute) {
	ssize_t ret = write(pcmIn->muteFd, &mute, sizeof(mute));
	(void) ret;
	return 0;
}


PUBLIC int AlsaPcmCopy(SoftMixerT *mixer, AlsaStreamAudioT *stream, AlsaPcmCtlT *pcmIn, AlsaPcmCtlT *pcmOut, AlsaPcmHwInfoT * opts) {
    char string[32];
    int error;

    // Fulup need to check https://www.alsa-project.org/alsa-doc/alsa-lib/group___p_c_m___direct.html

    AlsaDumpPcmInfo(mixer,"PcmIn",pcmIn->handle);
    AlsaDumpPcmInfo(mixer,"PcmOut",pcmOut->handle);

    AFB_ApiInfo(mixer->api, "%s: Configure CAPTURE PCM", __func__);

    /* remember configuration of capture */
    pcmIn->params = (AlsaPcmHwInfoT*)malloc(sizeof(AlsaPcmHwInfoT));
    memcpy(pcmIn->params, opts, sizeof(AlsaPcmHwInfoT));

    pcmOut->params = (AlsaPcmHwInfoT*)malloc(sizeof(AlsaPcmHwInfoT));
    memcpy(pcmOut->params, opts, sizeof(AlsaPcmHwInfoT));

    pcmIn->mixer = mixer;
    pcmOut->mixer = mixer;

    AFB_ApiInfo(mixer->api, "%s: Configure CAPTURE PCM", __func__);

    // prepare PCM for capture and replay
    error = AlsaPcmConf(mixer, pcmIn, SND_PCM_STREAM_CAPTURE);
    if (error) {
    	AFB_ApiError(mixer->api, "%s: PCM configuration for capture failed", __func__);
    	goto OnErrorExit;
    }

    AFB_ApiInfo(mixer->api, "%s: Configure PLAYBACK PCM", __func__);

    // input and output should match
    error = AlsaPcmConf(mixer, pcmOut, SND_PCM_STREAM_PLAYBACK);
    if (error) {
    	AFB_ApiError(mixer->api, "%s: PCM configuration for playback failed", __func__);
    	goto OnErrorExit;
    }

    // Prepare PCM for usage
    if ((error = snd_pcm_prepare(pcmOut->handle)) < 0) {
        AFB_ApiError(mixer->api, "%s: Fail to prepare PLAYBACK PCM=%s error=%s", __func__, ALSA_PCM_UID(pcmOut->handle, string), snd_strerror(error));
        goto OnErrorExit;
    };

    // Prepare PCM for usage
    if ((error = snd_pcm_prepare(pcmIn->handle)) < 0) {
        AFB_ApiError(mixer->api, "%s: Fail to prepare CAPTURE PCM=%s error=%s", __func__, ALSA_PCM_UID(pcmOut->handle, string), snd_strerror(error));
        goto OnErrorExit;
    };

    // Start PCM
    if ((error = snd_pcm_start(pcmOut->handle)) < 0) {
        AFB_ApiError(mixer->api, "%s: Fail to start PLAYBACK PCM=%s error=%s", __func__, ALSA_PCM_UID(pcmIn->handle, string), snd_strerror(error));
        goto OnErrorExit;
    };

    // Start PCM
    if ((error = snd_pcm_start(pcmIn->handle)) < 0) {
        AFB_ApiError(mixer->api, "%s: Fail to start CAPTURE PCM=%s error=%s", __func__, ALSA_PCM_UID(pcmIn->handle, string), snd_strerror(error));
        goto OnErrorExit;
    };

    AlsaPcmCopyHandleT *cHandle= calloc(1, sizeof(AlsaPcmCopyHandleT));

    cHandle->info = "pcmCpy";
    cHandle->pcmIn = pcmIn;
    cHandle->pcmOut = pcmOut;
    cHandle->api = mixer->api;
    cHandle->channels = opts->channels;

	cHandle->frame_size = (snd_pcm_format_physical_width(opts->format) / 8) * opts->channels;

	AFB_ApiInfo(mixer->api, "%s: Frame size is %zu", __func__, cHandle->frame_size);

	snd_pcm_uframes_t nbFrames = 2 * opts->rate; // Exactly 2 second of buffer

    cHandle->rbuf = alsa_ringbuf_new(nbFrames, cHandle->frame_size);

    cHandle->read_err_count  = 0;
    cHandle->write_err_count = 0;

    AFB_ApiInfo(mixer->api, "%s Copy buffer nbframes is %zu", __func__, nbFrames);

    // get FD poll descriptor for capture PCM
    int pcmInCount = snd_pcm_poll_descriptors_count(pcmIn->handle);
    if (pcmInCount <= 0) {
        AFB_ApiError(mixer->api,
                     "%s: Fail pcmIn=%s get fds count error=%s",
                     __func__, ALSA_PCM_UID(pcmIn->handle, string), snd_strerror(error));
        goto OnErrorExit;
    };

    if (pcmInCount > 1) {
    	AFB_ApiError(mixer->api,
    	             "%s: Fail, pcmIn=%s; having more than one FD on capture PCM is not supported (here, %d)",
    	             __func__, ALSA_PCM_UID(pcmOut->handle, string) , pcmInCount);
    	goto OnErrorExit;
    }

    struct pollfd pcmInFd;
    if ((error = snd_pcm_poll_descriptors(pcmIn->handle, &pcmInFd, 1)) < 0) {
        AFB_ApiError(mixer->api,
                     "%s: Fail pcmIn=%s get pollfds error=%s",
                     __func__, ALSA_PCM_UID(pcmOut->handle, string), snd_strerror(error));
        goto OnErrorExit;
    };

    // create the mute pipe
    int pMuteFd[2];
    error = pipe(pMuteFd);
    if (error < 0) {
        AFB_ApiError(mixer->api,
                     "Unable to create the mute signaling pipe");
        goto OnErrorExit;
    }

    struct pollfd mutePFd;
    // read end
    mutePFd.fd = pMuteFd[0];
    mutePFd.events = POLLIN;
    mutePFd.revents = 0;

    // write end
    pcmIn->muteFd = pMuteFd[1];

    cHandle->pollFds[0] = mutePFd;
   	cHandle->pollFds[1] = pcmInFd;

    cHandle->nbPcmFds = pcmInCount+1;

    error = sem_init(&cHandle->sem, 0 , 0);
    if (error < 0) {
    	AFB_ApiError(mixer->api,
    	                     "%s Fail initialize loop semaphore pcmIn=%s err=%d",
    	                     __func__, ALSA_PCM_UID(pcmIn->handle, string), error);
    	goto OnErrorExit;
    }

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);

    error = pthread_mutex_init(&cHandle->mutex, &attr);
    if (error < 0) {
    	AFB_ApiError(mixer->api,
    	                     "%s Fail initialize loop mutex pcmIn=%s err=%d",
    	                     __func__, ALSA_PCM_UID(pcmIn->handle, string), error);
    }


    /// start a thread for writing
    if ((error = pthread_create(&cHandle->wthread, NULL, &readThreadEntry, cHandle)) < 0) {
        AFB_ApiError(mixer->api,
                     "%s Fail create write thread pcmOut=%s err=%d",
                     __func__, ALSA_PCM_UID(pcmOut->handle, string), error);
        goto OnErrorExit;
    }

    // start a thread for reading
    if ((error = pthread_create(&cHandle->rthread, NULL, &writeThreadEntry, cHandle)) < 0) {
        AFB_ApiError(mixer->api,
                     "%s Fail create read thread pcmIn=%s err=%d",
                     __func__, ALSA_PCM_UID(pcmIn->handle, string), error);
        goto OnErrorExit;
    }

    // request a higher priority for each audio stream thread
    struct sched_param params;
    params.sched_priority = sched_get_priority_max(SCHED_FIFO);

    error= pthread_setschedparam(cHandle->rthread, SCHED_FIFO, &params);
    if (error) {
        AFB_ApiWarning(mixer->api,
                       "%s: Failed to increase stream read thread priority pcmIn=%s err=%s",
                       __func__, ALSA_PCM_UID(pcmIn->handle, string), strerror(error));
    }

    error= pthread_setschedparam(cHandle->wthread, SCHED_FIFO, &params);
    if (error) {
        AFB_ApiWarning(mixer->api,
                       "%s: Failed to increase stream write thread priority pcmIn=%s err=%s",
                       __func__, ALSA_PCM_UID(pcmOut->handle, string), strerror(error));
    }

    return 0;

OnErrorExit:
    AFB_ApiError(mixer->api, "%s: - pcmIn=%s" , __func__, ALSA_PCM_UID(pcmIn->handle, string));
    AFB_ApiError(mixer->api, "%s: - pcmOut=%s", __func__, ALSA_PCM_UID(pcmOut->handle, string));
    return -1;
}

