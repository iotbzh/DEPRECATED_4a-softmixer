--[[
  Copyright (C) 2016 "IoT.bzh"
  Author Fulup Ar Foll <fulup@iot.bzh>

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.


  NOTE: strict mode: every global variables should be prefixed by '_'
--]]

-- Static variables should be prefixed with _
_EventHandle={}

-- Call when AlsaCore return HAL active list
function _AlsaPingCB_ (source, result, context)

    AFB:notice (source, "--InLua-- PingCB: result='%s'", Dump_Table(result))

end


-- Display receive arguments and echo them to caller
function _mixer_config_ (source, args)
    local error

    local sndcard_0 = {
        ["uid"]= "YAMAHA-APU70",
        ["path"]= "/dev/snd/by-id/usb-YAMAHA_Corporation_YAMAHA_AP-U70_USB_Audio_00-00",
        ["sink"] = {
            [0]= {["uid"]= "front-right", ["port"]= 0},
            [1]= {["uid"]= "front-left", ["port"]= 1},
        }
    }

    local sndcard_1 = {
        ["uid"]= "Jabra-Solemate",
        ["path"]= "/dev/snd/by-id/usb-0b0e_Jabra_SOLEMATE_v1.34.0-00",
        ["sink"] = {
            [0]= {["uid"]= "front-right", ["port"]= 0},
            [1]= {["uid"]= "front-left", ["port"]= 1},
        }
    }

    local sndcard_2 = {
        ["uid"]= "Jabra-410",
        ["path"]= "/dev/snd/by-id/usb-0b0e_Jabra_SPEAK_410_USB_745C4B15BD11x010900-00",
        ["sink"] = {
            [0]= {["uid"]= "back-right", ["port"]= 0},
            [1]= {["uid"]= "back-left",  ["port"]= 1},
        }
    }

    local snd-aloop = {
        ["uid"]= "Alsa-Loop",
        ["path"]= "/dev/snd/by-path/platform-snd_aloop.0",
        ["capture"] = 1,
        ["playback"]= 0,
        ["subdevs"] = {
            [0]=  {["uid"]="musique"   ["subdev"]= 0, ["numid"]= 51},
            [1]=  {["uid"]="guidance"  ["subdev"]= 1, ["numid"]= 57},
            [2]=  {["uid"]="emergency" ["subdev"]= 2, ["numid"]= 63},
            [3]=  {["uid"]="telephony" ["subdev"]= 3, ["numid"]= 69},
            [4]=  {["uid"]="xxxx"      ["subdev"]= 4, ["numid"]= 75},
            [5]=  {["uid"]="yyyy"      ["subdev"]= 5, ["numid"]= 81},
            [6]=  {["uid"]="zzzz"      ["subdev"]= 6, ["numid"]= 87},
            [7]=  {["uid"]="yyyy"      ["subdev"]= 7, ["numid"]= 93},
        }
    }

    local sndcards= {
        [0] = sndcard_0,
        [1] = sndcard_2,
    }

    local zone_front= {
        ["uid"]  = "front-seat",
        ["type"] = "playback",
        ["mapping"] = {
            [0]= {["target"]="front-right",["channel"]=0},
            [1]= {["target"]="front-left" ,["channel"]=1},
        }
    }

    local zone_back= {
        ["uid"]  = "back-seat",
        ["type"] = "playback",
        ["mapping"] = {
            [0]= {["target"]="back-right",["channel"]=0},
            [1]= {["target"]="back-left" ,["channel"]=1},
        }
    }

    local zone_all= {
        ["uid"]  = "all-seat",
        ["type"] = "playback",
        ["mapping"] = {
            [0]= {["target"]="front-right",["channel"]=0},
            [1]= {["target"]="front-left" ,["channel"]=1},
            [3]= {["target"]="back-right" ,["channel"]=0},
            [4]= {["target"]="back-left"  ,["channel"]=1},
        }
    }

    local zones = {
        [0] = zone-all,
        [1] = zone-front,
        [2] = zone-back,
    }

    local stream_music= {
        ["uid"]   = "multimedia",
        ["zone"]  = "zone-all",
        ["volume"]= 70,
        ["mute"]  = false;
    }
    
    local stream_navigation= {
        ["uid"]   = "navigation",
        ["zone"]  = "zone-front",
        ["volume"]= 80,
        ["mute"]  = false;
    }
    
    local stream_children= {
        ["uid"]   = "children",
        ["zone"]  = "zone-back",
        ["volume"]= 50,
        ["mute"]  = false;
    }
    
    local streams = {
        [0] = stream_music,
        [1] = stream_navigation,
        [2] = stream_children,
    }

    local params = {
        ["rate"]= 48000,
        --["rate"]= 44100,
        ["channels"]= 2,
    }



    -- Call AlsaSoftRouter

    error= L2C:snd_cards (source, sndcards)
    if (error ~= 0) then 
        AFB:error (source, "--InLua-- L2C:sndcards fail to attach sndcards=%s", Dump_Table(sndcards))
        --goto OnErrorExit
    else
        AFB:notice (source, "--InLua-- L2C:sndcards done response=%s", Dump_Table(response))
    end
    
    error= L2C:snd_zones (source, zones)
    if (error ~= 0) then 
        AFB:error (source, "--InLua-- L2C:zones fail to attach sndcards=%s", Dump_Table(sndcards))
        --goto OnErrorExit
    else
        AFB:notice (source, "--InLua-- L2C:zones done response=%s", Dump_Table(response))
    end
    

    error= L2C:snd_stream (source, {["devin"]= devin, ["devout"]= devout, ["params"]= params})

    AFB:notice (source, "--InLua-- _mixer_config_ done")

    do return 0 end -- happy end (hoops: lua syntax) --

::OnErrorExit::
    AFB:error (source, "--InLua-- snd_attach fail")
    return 1 end -- unhappy end --

-- Display receive arguments and echo them to caller
function _init_softmixer_ (source, args)

    -- create event to push change audio roles to potential listeners
    _EventHandle=AFB:evtmake(source, "control")

    _mixer_config_ (source, args)

end
