-- Audio sink (sound device) selection option row
-- Provides a list of available ALSA output devices (plus the default) and saves the selection
-- to the SoundDevice preference so that it is respected by the engine on next launch.

function AudioSink()

    -- Retrieve device list exposed by the new C++ helper.  Always fall back to {"default"}.
    local choices = {}
    local success, devices = pcall(get_sound_device_list)
    if success and type(devices) == "table" and #devices > 0 then
        for i=1,#devices do choices[#choices+1] = devices[i] end
    else
        choices = { "default" }
    end

    local t = {
        Name = "AudioSink", -- text for the header/explanation is looked up with this key
        LayoutType = "ShowAllInRow",
        SelectType = "SelectOne",
        OneChoiceForAllPlayers = true,
        ExportOnChange = false,
        Choices = choices,
        LoadSelections = function(self, list, pn)
            local current = PREFSMAN:GetPreference("SoundDevice") or "default"
            local idx = 1
            for i=1,#self.Choices do
                if self.Choices[i] == current then idx = i break end
            end
            list[idx] = true
        end,
        SaveSelections = function(self, list, pn)
            for i=1,#list do
                if list[i] then
                    local choice = self.Choices[i]
                    PREFSMAN:SetPreference("SoundDevice", choice)
                    -- Changing the sink at runtime is driver dependent; safest is to ask
                    -- user to restart.  Log notice so theme can pick it up if desired.
                    LOG("Audio sink set to " .. choice .. ". Restart game to apply.")
                    break
                end
            end
        end,
    }
    return t
end
