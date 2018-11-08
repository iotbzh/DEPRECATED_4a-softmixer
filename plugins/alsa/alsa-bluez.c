/*
 * Copyright(C) 2018 "IoT.bzh"
 * Author Thierry Bultel <thierry.bultel@iot.bzh>
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

 * for the specific language governing permissions and
 * limitations under the License.
 *
 *
 */


#include "alsa-bluez.h"

#include <stdbool.h>
#include <dlfcn.h>

#define ALSA_BLUEZ_PROXY_LIB "/usr/lib/alsa-lib/libasound_module_pcm_bluealsa_proxy.so"
#define ALSA_BLUEZ_PROXY_SETDEVICE "bluealsa_proxy_set_remote_device"

typedef	int (*bluealsa_set_remote_device_ptr) (const char * interface, const char * device, const char * profile);

static bluealsa_set_remote_device_ptr bluealsa_proxy_set_remote_device = NULL;

void alsa_bluez_init() {
	static bool initialized = false;
	if (initialized)
		goto failed;

#if SND_LIB_VERSION >= (1<<16|1<<8|6)
	char errbuf[256];
	void * dl = snd_dlopen(ALSA_BLUEZ_PROXY_LIB, RTLD_NOW, errbuf, 256);
#else
	void * dl = snd_dlopen(ALSA_BLUEZ_PROXY_LIB, RTLD_NOW);
#endif
	if (!dl) {
		printf("Failed to open bluealsa proxy plugin\n");
		goto failed;
	}

	void * func = snd_dlsym(dl, ALSA_BLUEZ_PROXY_SETDEVICE, SND_DLSYM_VERSION(SND_PCM_DLSYM_VERSION));
	if (!func) {
		printf("Unable to find %s symbol\n", ALSA_BLUEZ_PROXY_SETDEVICE);
		goto failed;
	}

	bluealsa_proxy_set_remote_device = func;
	initialized = true;

failed:
	return;
}

int alsa_bluez_set_remote_device(const char * interface, const char * device, const char * profile) {
	if (!bluealsa_proxy_set_remote_device)
		return -1;

	return bluealsa_proxy_set_remote_device(interface,device,profile);
}


