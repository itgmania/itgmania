Branch.OptionsEdit = function()
	if SONGMAN:GetNumSongs() == 0 then
		return "ScreenHowToInstallSongs"
	end
	return "ScreenEditMenu"
end
