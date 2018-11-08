#ifndef __INC_ALSA_RINGBUF_H
#define __INC_ALSA_RINGBUF_H

#include "ringbuf.h"

#include <alsa/asoundlib.h>
#include <stdbool.h>

typedef struct {
	ringbuf_t rbuf;
	size_t frameSize;
} alsa_ringbuf_t ;

extern alsa_ringbuf_t * alsa_ringbuf_new(snd_pcm_uframes_t capacity, size_t frameSize);
extern snd_pcm_uframes_t alsa_ringbuf_buffer_size(const alsa_ringbuf_t *rb);
extern void alsa_ringbuf_free(alsa_ringbuf_t *rb);
extern void alsa_ringbuf_reset(alsa_ringbuf_t *rb);
extern snd_pcm_uframes_t alsa_ringbuf_capacity(const alsa_ringbuf_t *rb);
extern snd_pcm_uframes_t alsa_ringbuf_frames_remain_capacity(const alsa_ringbuf_t *rb);
extern snd_pcm_uframes_t alsa_ringbuf_frames_used(const alsa_ringbuf_t *rb);
extern bool alsa_ringbuf_is_full(const alsa_ringbuf_t *rb);
extern bool alsa_ringbuf_is_empty(const alsa_ringbuf_t *rb);

extern void alsa_ringbuf_frames_push(alsa_ringbuf_t * rb, const void * buf, snd_pcm_uframes_t nb);
extern void alsa_ringbuf_frames_pop(alsa_ringbuf_t * rb, void * buf, snd_pcm_uframes_t nb);

#endif /* __INC_ALSA_RINGBUF_H */
