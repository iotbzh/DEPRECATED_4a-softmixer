Softmixer controller for 4A (AGL Advance Audio Architecture).
------------------------------------------------------------

 * Object: LUA API Documentation
 * Status: In Progress
 * Author: Fulup Ar Foll fulup@iot.bzh
 * Date  : April-2018

## Sound Cards

User may define as many sound card as needed. All sound cards will be group within one global multi channels card.
As a result, declaring three USB stereo sound card is equivalent to declare one Renesas GEN3 with 6 channels

```
    -- Sound Card Definition
    -- ==========================
    -- local sndcard_sample = {
    --
    -- * Mandatory UID will be the ALSA PCM
    --     ["uid"]= "YAMAHA-APU70",  
    --
    -- * Mandatory Card might be found by either its devpath, its cardid or cardindex (should provide only one)
    --     ["devpath"]= "/dev/snd/by-xxx/xxxx, (any path in /dev/snd pointing to a valid sndcard control works
    --     ["cardid"]  = "hw:xx",  [xx] may either be a card index or a name (eg: hw:USB)
    --     ["cardidx"] = N,
    --
    -- * Optional Device and subdev
    --     ["device"] = N, (default 0)
    --     ["subdev"] = N, (default 0)
    --
    -- * Mandatory List of sink channels attached to the card
    --     ["sink"] = {
    --         [0]= {["uid"]= "front-right", ["port"]= 0},
    --         [1]= {["uid"]= "front-left", ["port"]= 1},
    --     }
    -- }
```

Sound card should be group in a table in order to request grouping as a multi channel PCM. Note that this grouping 
relies on ALSA 'multi' plugin and thus inherit of its limits. 

```
  local sndcards= {
        [0] = sndcard_0,
        [1] = sndcard_2,
        [3] = sndcard_3,
    }
```

Call 

```
    error= L2C:snd_cards (source, sndcards)
    if (error ~= 0) then 
        AFB:error (source, "--InLua-- L2C:sndcards fail to attach sndcards=%s", Dump_Table(sndcards))
        --goto OnErrorExit
    else
        AFB:notice (source, "--InLua-- L2C:sndcards done response=%s", Dump_Table(response))
    end
```