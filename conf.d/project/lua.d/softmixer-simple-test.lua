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
function _mixer_simple_test_ (source, args)
    do
    local error
    local response

    -- ================== Default Alsa snd-aloop numid and subdev config
    local aloop = {
        ["devices"] = {["playback"]=0,["capture"]=1},
        ["subdevs"] = {
            {["subdev"]= 0, ["numid"]= 51},
            {["subdev"]= 1, ["numid"]= 57},
            {["subdev"]= 2, ["numid"]= 63},
            {["subdev"]= 3, ["numid"]= 69},
            {["subdev"]= 4, ["numid"]= 75},
            {["subdev"]= 5, ["numid"]= 81},
            {["subdev"]= 6, ["numid"]= 87},
            {["subdev"]= 7, ["numid"]= 93},
        }
    }

    -- ==================== Default rate ===========================

    local audio_defaults = {
        ["rate"]   = 48000,
    }  

    -- ======================= Loop PCM ===========================
    local snd_aloop = {
        ["uid"]     = "Alsa-Loop",
        ["devpath"] = "/dev/snd/by-path/platform-snd_aloop.0",       
        ["params"]  = audio_defaults,
        ["devices"] = aloop.devices,
        ["subdevs"] = aloop.subdevs,
    }


    -- ============================= Sound Cards ===================  
    local snd_yamaha = {
        ["uid"]= "YAMAHA-APU70",
        ["devpath"]= "/dev/snd/by-id/usb-YAMAHA_Corporation_YAMAHA_AP-U70_USB_Audio_00-00",
        ["params"] = snd_params,
        ["sink"] = {
            [0]= {["uid"]= "front-right", ["port"]= 0},
            [1]= {["uid"]= "front-left", ["port"]= 1},
        }
    }

    local snd_jabra= {
        ["uid"]= "Jabra-Solemate",
        ["devpath"]= "/dev/snd/by-id/usb-0b0e_Jabra_SOLEMATE_v1.34.0-00",
        ["params"] = snd_params,
        ["sink"] = {
            [0]= {["uid"]= "front-right", ["port"]= 0},
            [1]= {["uid"]= "front-left", ["port"]= 1},
        }
    }

 
    -- ============================= Zones ===================    
    local zone_front= {
        ["uid"]  = "front-seats",
        ["type"] = "playback",
        ["mapping"] = {
            {["target"]="front-right",["channel"]=0},
            {["target"]="front-left" ,["channel"]=1},
        }
    }

    -- =================== Audio Stream ============================
    local stream_music= {
        ["uid"]   = "multimedia",
        ["zone"]  = "front-seats",
        ["volume"]= 70,
        ["mute"]  = false,
    }
    
    local stream_navigation= {
        ["uid"]   = "navigation",
        ["zone"]  = "front-seats",
        ["volume"]= 80,
        ["mute"]  = false,
    }
        
    --- ================ Create Mixer =========================
    local MyMixer= {
        ["uid"]="Simple_Mixer",
        ["backend"] = {snd_yamaha},
        ["frontend"]= {snd_aloop},
        ["zones"]   = {zone_front},
        ["streams"] = {stream_music,stream_navigation},
    }

    local error,response= smix:_mixer_new_ (source, MyMixer)
    if (error ~= 0) then 
        AFB:error (source, "--InLua-- smix:_mixer_new_ fail config=%s", Dump_Table(aloop))
        goto OnErrorExit
    else
        AFB:notice (source, "--InLua-- smix:_mixer_new_ done response=%s\n", Dump_Table(response))
    end

  
    -- ================== Happy End =============================
    AFB:notice (source, "--InLua-- _mixer_config_ done")
    return 0 end 

    -- ================= Unhappy End ============================
    ::OnErrorExit::
        AFB:error (source, "--InLua-- snd_attach fail")
        return 1 -- unhappy end --
end 
