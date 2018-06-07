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


-- make variable visible from ::OnExitError::
local error
local result


local printf = function(s,...)
    io.write(s:format(...))
    io.write("\n")
    return
end

-- Display receive arguments and echo them to caller
function _mixer_simple_test_ (source, args)
    do
 
    -- Mixer UID is used as API name

    -- ==================== Default rate ===========================

    local audio_params ={
        defaults = { ["rate"] = 48000 },
        standard = { ["rate"] = 44100 },
        basic    = { ["rate"] = 8000  },
    }  

    local volume_ramps = {
            {["uid"]="ramp-fast",   ["delay"]= 050, ["up"]=10,["down"]=3},
            {["uid"]="ramp-slow",   ["delay"]= 250, ["up"]=03,["down"]=1},
            {["uid"]="ramp-normal", ["delay"]= 100, ["up"]=06,["down"]=2},
    }

    -- ======================= Loop PCM ===========================
    local snd_aloop = {
        ["uid"]     = "Alsa-Loop",
        ["path"]= "/dev/snd/by-path/platform-snd_aloop.0",
        ["devices"] = {["playback"]=0,["capture"]=1},
        ["subdevs"] = {
            {["subdev"]= 0, ["numid"]= 51, ["uid"]= "loop-legacy"},
            {["subdev"]= 1, ["numid"]= 57, ["uid"]= "loop-multimedia"},
            {["subdev"]= 2, ["numid"]= 63},
            {["subdev"]= 3, ["numid"]= 69},
            {["subdev"]= 4, ["numid"]= 75},
            {["subdev"]= 5, ["numid"]= 81},
            {["subdev"]= 6, ["numid"]= 87},
            {["subdev"]= 7, ["numid"]= 93},
        },
    }


    -- ============================= Backend (Sound Cards) ===================  

    local snd_usb_8ch= {
        ["uid"]= "8CH-USB",
        ["cardid"]= "USB",
        ["params"] = audio_params.default,
        ["sink"] = {
            ["controls"]= {
                ["volume"] = {["name"]= "Speaker Playback Volume", ["value"]=80},
                ["mute"]   = {["name"]= "Speaker Playback Switch"},
            },
            ["channels"] = {
                {["uid"]= "front-right", ["port"]= 0},
                {["uid"]= "front-left" , ["port"]= 1},
                {["uid"]= "middle-right", ["port"]= 2},
                {["uid"]= "middle-left" , ["port"]= 3},
                {["uid"]= "back-right", ["port"]= 4},
                {["uid"]= "back-left" , ["port"]= 5},
                {["uid"]= "centre-left" , ["port"]= 6},
                {["uid"]= "centre-left" , ["port"]= 7},
            },
        },
        ["source"] = {
            ["controls"]= {
                ["volume"] = {["name"]= "Capture Volume"},
                ["mute"]   = {["name"]= "Capture Switch"},
            },
            ["channels"] = {
                {["uid"]= "mic-right", ["port"]= 0},
                {["uid"]= "mic-left" , ["port"]= 1},
            },
        }
    }

 
    -- ============================= Zones ===================    
    local zone_stereo={
        ["uid"]  = "full-stereo",
        ["sink"] = {
            {["target"]="front-right",["channel"]=0},
            {["target"]="front-left" ,["channel"]=1},
            {["target"]="middle-right",["channel"]=0},
            {["target"]="middle-left" ,["channel"]=1},
            {["target"]="back-right",["channel"]=0},
            {["target"]="back-left" ,["channel"]=1},
        }
    }

    local zone_front= {
        ["uid"]  = "front-seats",
        ["sink"] = {
            {["target"]="front-right",["channel"]=0},
            {["target"]="front-left" ,["channel"]=1},
        }
    }

    local zone_middle= {
        ["uid"]  = "middle-seats",
        ["sink"] = {
            {["target"]="middle-right",["channel"]=0},
            {["target"]="middle-left" ,["channel"]=1},
        }
    }

    local zone_back= {
        ["uid"]  = "back-seats",
        ["sink"] = {
            {["target"]="back-right",["channel"]=0},
            {["target"]="back-left" ,["channel"]=1},
        }
    }

    local zone_driver= {
        ["uid"]  = "driver-seat",
        ["source"] = {
            {["target"]="mic-right",["channel"]=0},
        },
        ["sink"] = {
            {["target"]="front-right",["channel"]=0},
        }
    }

    -- =================== Audio Streams ============================
    local stream_music= {
        ["uid"] = "stream-multimedia",
        ["zone"]= "full-stereo",
        ["source"]= "loop-multimedia",
        ["volume"]= 80,
        ["mute"]  = false,
        ["params"]= audio_params.standard,
    }
    
    local stream_navigation= {
        ["uid"]   = "stream-navigation",
        ["zone"]= "front-seats",
        ["volume"]= 80,
        ["mute"]  = false,
    }
    
    local stream_emergency= {
        ["uid"]   = "stream-emergency",
        ["zone"]  = "driver-seat",
        ["volume"]= 80,
        ["mute"]  = false,
        --["params"]= audio_params.basic,
    }
        
    local stream_radio= {
        ["uid"]   = "stream-radio",
        ["zone"]  = "full-stereo",
        ["source"]= "radio",
        ["volume"]= 80,
        ["mute"]  = false,
    }
    
    local stream_pulse= {
        ["uid"]   = "stream-pulseaudio",
        ["zone"]  = "back-seats",
        ["source"]= "loop-legacy",
        ["volume"]= 80,
        ["mute"]  = false,
    }
    
    --- ================ Create Mixer =========================
    local MyTestHal= {
        ["uid"]      = "MyMixer",
        ["ramps"]    = volume_ramps,
        ["playbacks"]= {snd_usb_8ch },
        ["captures"] = {snd_usb_8ch },
        ["loops"]    = {snd_aloop},
        ["zones"]    = {zone_stereo, zone_front, zone_back, zone_middle, zone_driver},
        ["streams"]  = {stream_pulse, stream_music, stream_navigation, stream_radio },
    }

    error,result= AFB:servsync(source, "smixer", "attach", MyTestHal)

    if (error) then 
        AFB:error (source, "--InLua-- API MyMixer/attach fail error=%d", error)
        goto OnErrorExit
    else
        AFB:notice (source, "--InLua-- MyMixer/attach done result=%s\n", Dump_Table(result))
    end
  
    -- ================== Happy End =============================
    AFB:notice (source, "--InLua-- Test success")
    return 0 end 

    -- ================= Unhappy End ============================
    ::OnErrorExit::
        local response=result["request"]
        printf ("--InLua-- ------------STATUS= %s --------------", result["status"])
        printf ("--InLua-- ++ INFO= %s", Dump_Table(response["info"]))
        printf ("--InLua-- ----------TEST %s-------------", result["status"])

        AFB:error (source, "--InLua-- Test Fail")
        return 1 -- unhappy end --
end 
