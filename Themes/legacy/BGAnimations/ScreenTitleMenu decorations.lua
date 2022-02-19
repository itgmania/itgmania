InitUserPrefs();

local t = Def.ActorFrame {}

t[#t+1] = StandardDecorationFromFileOptional("Footer","Footer");
t[#t+1] = StandardDecorationFromFileOptional("Logo","Logo");
t[#t+1] = StandardDecorationFromFileOptional("VersionInfo","VersionInfo");
t[#t+1] = StandardDecorationFromFileOptional("CurrentGametype","CurrentGametype");
t[#t+1] = StandardDecorationFromFileOptional("LifeDifficulty","LifeDifficulty");
t[#t+1] = StandardDecorationFromFileOptional("TimingDifficulty","TimingDifficulty");
t[#t+1] = StandardDecorationFromFileOptional("NetworkStatus","NetworkStatus");
t[#t+1] = StandardDecorationFromFileOptional("SystemDirection","SystemDirection");

t[#t+1] = StandardDecorationFromFileOptional("NumSongs","NumSongs") .. {
	SetCommand=function(self)
		local InstalledSongs, InstalledCourses, Groups, Unlocked = 0;
		if SONGMAN:GetRandomSong() then
			InstalledSongs, InstalledCourses, Groups, Unlocked =
				SONGMAN:GetNumSongs(),
				SONGMAN:GetNumCourses(),
				SONGMAN:GetNumSongGroups(),
				SONGMAN:GetNumUnlockedSongs();
		else
			return
		end

		self:settextf(THEME:GetString("ScreenTitleMenu","%i Songs (%i Groups), %i Courses"), InstalledSongs, Groups, InstalledCourses);
	end;
};

return t
