# Controller Utilities

* Object: Generic Controller Utilities to handle Policy, Small Business Logic, Glue in between components, ...
* Status: Release Candidate
* Author: Fulup Ar Foll fulup@iot.bzh
* Date  : October-2017

## Usage

0) Dependencies

* AGL Application Framework

1) Clone & build from source (temporally from github)

```bash
git clone --recursive https://github.com/iotbzh/4a-softmixer
cd build
cmake ..
make
```

2) Activate ALSA loop driver

```modprobe snd-aloop enable=1,1 index=4,5 id=loopback,softmix
```

3) Declare your controller config section in your binding

```C
// CtlSectionT syntax:
// key: "section name in config file"
// loadCB: callback to process section
// handle: a void* pass to callback when processing section
static CtlSectionT ctlSections[]= {
    {.key="plugins" , .loadCB= PluginConfig, .handle= &halCallbacks},
    {.key="onload"  , .loadCB= OnloadConfig},
    {.key="halmap"  , .loadCB= MapConfigLoad},
    {.key=NULL}
};

```

3) Do controller config parsing at binding pre-init

```C
   // check if config file exist
    const char *dirList= getenv("CTL_CONFIG_PATH");
    if (!dirList) dirList=CONTROL_CONFIG_PATH;

    ctlConfig = CtlConfigLoad(dirList, ctlSections);
    if (!ctlConfig) goto OnErrorExit;
```

4) Exec controller config during binding init

```C
  int err = CtlConfigExec (ctlConfig);
```

For sample usage look at https://gerrit.automotivelinux.org/gerrit/gitweb?p=apps/app-controller-submodule.git
