#include "alsa-ringbuf.h"

alsa_ringbuf_t * alsa_ringbuf_new(snd_pcm_uframes_t capacity, size_t frameSize) {
	alsa_ringbuf_t * rb = malloc(sizeof(alsa_ringbuf_t));

	rb->rbuf = ringbuf_new(capacity*frameSize);
	rb->frameSize = frameSize;
	return rb;
}

snd_pcm_uframes_t alsa_ringbuf_buffer_size(const alsa_ringbuf_t *rb) {
	return ringbuf_buffer_size(rb->rbuf)/rb->frameSize;
}

void alsa_ringbuf_free(alsa_ringbuf_t *rb) {
	ringbuf_free(&rb->rbuf);
	free(rb);
}

void alsa_ringbuf_reset(alsa_ringbuf_t * rb) {
	ringbuf_reset(rb->rbuf);
}

snd_pcm_uframes_t alsa_ringbuf_capacity(const alsa_ringbuf_t *rb) {
	return ringbuf_capacity(rb->rbuf)/rb->frameSize;
}

snd_pcm_uframes_t alsa_ringbuf_frames_free(const alsa_ringbuf_t *rb) {
	return ringbuf_bytes_free(rb->rbuf)/rb->frameSize;
}

snd_pcm_uframes_t alsa_ringbuf_frames_used(const alsa_ringbuf_t *rb) {
	return ringbuf_bytes_used(rb->rbuf)/rb->frameSize;
}

bool alsa_ringbuf_is_full(const alsa_ringbuf_t *rb) {
	return ringbuf_is_full(rb->rbuf)==1;
}

bool alsa_ringbuf_is_empty(const alsa_ringbuf_t *rb) {
	return ringbuf_is_empty(rb->rbuf)==1;
}

void alsa_ringbuf_frames_push(alsa_ringbuf_t * rb, const void * src, snd_pcm_uframes_t nb) {
	ringbuf_memcpy_into(rb->rbuf, src, nb*rb->frameSize);
}

void alsa_ringbuf_frames_pop(alsa_ringbuf_t * rb, void * dst, snd_pcm_uframes_t nb) {
	ringbuf_memcpy_from(dst, rb->rbuf, nb*rb->frameSize);
}
