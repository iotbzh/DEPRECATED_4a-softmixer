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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "alsa-softmixer.h"

// extracted IOCTLs from <alsa/asoundlib.h>
#define _IOR_HACKED(type,nr,size)       _IOC(_IOC_READ,(type),(nr),size)
#define SNDRV_CTL_IOCTL_CARD_INFO(size) _IOR_HACKED('U', 0x01, size)

// Clone of AlsaLib snd_card_load2 static function
int AlsaCardInfoByPath (const char *devpath) {
    int open_dev;
    snd_ctl_card_info_t *cardinfo = alloca(snd_ctl_card_info_sizeof());

    open_dev = open(devpath, O_RDONLY);
    if (open_dev >= 0) {
        int rc = ioctl(open_dev, SNDRV_CTL_IOCTL_CARD_INFO(snd_ctl_card_info_sizeof()), cardinfo);
        if (rc < 0) {
            int err = -errno;
            close(open_dev);
            return err;
        }
        close(open_dev);
        int cardId = snd_ctl_card_info_get_card(cardinfo);
        return cardId;
    } else {
        return -errno;
    }
}

