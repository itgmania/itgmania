--If a Command has "NOTESKIN:GetMetricA" in it, that means it gets the command from the metrics.ini, else use cmd(); to define command.
--If you dont know how "NOTESKIN:GetMetricA" works here is an explanation.
--NOTESKIN:GetMetricA("The [Group] in the metrics.ini", "The actual Command to fallback on in the metrics.ini");

--The NOTESKIN:LoadActor() just tells us the name of the image the Actor redirects on.
--Oh and if you wonder about the "Button" in the "NOTESKIN:LoadActor( )" it means that it will check for that direction.
--So you dont have to do "Down" or "Up" or "Left" etc for every direction which will save space ;)
local t = Def.ActorFrame {
	--Hold Explosion Commands
	NOTESKIN:LoadActor( Var "Button", "Hold Explosion" ) .. {
		HoldingOnCommand=NOTESKIN:GetMetricA("HoldGhostArrow", "HoldingOnCommand");
		HoldingOffCommand=NOTESKIN:GetMetricA("HoldGhostArrow", "HoldingOffCommand");
		InitCommand=function(self)
			self:playcommand("HoldingOff"):finishtweening()
		end;
	};
	--Roll Explosion Commands
	NOTESKIN:LoadActor( Var "Button", "Hold Explosion" ) .. {
		RollOnCommand=NOTESKIN:GetMetricA("HoldGhostArrow", "RollOnCommand");
		RollOffCommand=NOTESKIN:GetMetricA("HoldGhostArrow", "RollOffCommand");
		InitCommand=function(self)
			self:playcommand("RollOff"):finishtweening()
		end;
		BrightCommand=function(self)
			self:visible(false)
		end;
		DimCommand=function(self)
			self:visible(false)
		end;
	};
    --We use this for Seperate Explosions for every Judgement
	Def.ActorFrame {
		--W1 aka Marvelous Dim Explosion Commands
		NOTESKIN:LoadActor( Var "Button", "Tap Explosion Dim W1" ) .. {
			InitCommand=function(self)
				self:diffusealpha(0)
			end;
			W1Command=NOTESKIN:GetMetricA("GhostArrowDim", "W1Command");
			JudgmentCommand=function(self)
				self:finishtweening()
			end;
			BrightCommand=function(self)
				self:visible(false)
			end;
			DimCommand=function(self)
				self:visible(true)
			end;
		};
		--W1 aka Marvelous Bright Explosion Commands
		NOTESKIN:LoadActor( Var "Button", "Tap Explosion Dim W1" ) .. {
			InitCommand=function(self)
				self:diffusealpha(0)
			end;
			W1Command=NOTESKIN:GetMetricA("GhostArrowBright", "W1Command");
			JudgmentCommand=function(self)
				self:finishtweening()
			end;
			BrightCommand=function(self)
				self:visible(true)
			end;
			DimCommand=function(self)
				self:visible(false)
			end;
		};
	};
	Def.ActorFrame {
		--W2 aka Perfect Dim Explosion Commands
		NOTESKIN:LoadActor( Var "Button", "Tap Explosion Dim W2" ) .. {
			InitCommand=function(self)
				self:diffusealpha(0)
			end;
			W2Command=NOTESKIN:GetMetricA("GhostArrowDim", "W1Command");
			HeldCommand=NOTESKIN:GetMetricA("GhostArrowDim", "HeldCommand");
			JudgmentCommand=function(self)
				self:finishtweening()
			end;
			BrightCommand=function(self)
				self:visible(false)
			end;
			DimCommand=function(self)
				self:visible(true)
			end;
		};
		--W2 aka Perfect Bright Explosion Commands
		NOTESKIN:LoadActor( Var "Button", "Tap Explosion Dim W2" ) .. {
			InitCommand=function(self)
				self:diffusealpha(0)
			end;
			W2Command=NOTESKIN:GetMetricA("GhostArrowBright", "W1Command");
			HeldCommand=NOTESKIN:GetMetricA("GhostArrowBright", "HeldCommand");
			JudgmentCommand=function(self)
				self:finishtweening()
			end;
			BrightCommand=function(self)
				self:visible(true)
			end;
			DimCommand=function(self)
				self:visible(false)
			end;
		};
	};
	Def.ActorFrame {
		--W3 aka Great Dim Explosion Commands
		NOTESKIN:LoadActor( Var "Button", "Tap Explosion Dim W3" ) .. {
			InitCommand=function(self)
				self:diffusealpha(0)
			end;
			W3Command=NOTESKIN:GetMetricA("GhostArrowDim", "W1Command");
			JudgmentCommand=function(self)
				self:finishtweening()
			end;
			BrightCommand=function(self)
				self:visible(false)
			end;
			DimCommand=function(self)
				self:visible(true)
			end;
		};
		--W3 aka Great Bright Explosion Commands
		NOTESKIN:LoadActor( Var "Button", "Tap Explosion Dim W3" ) .. {
			InitCommand=function(self)
				self:diffusealpha(0)
			end;
			W3Command=NOTESKIN:GetMetricA("GhostArrowBright", "W1Command");
			JudgmentCommand=function(self)
				self:finishtweening()
			end;
			BrightCommand=function(self)
				self:visible(true)
			end;
			DimCommand=function(self)
				self:visible(false)
			end;
		};
	};
		Def.ActorFrame {
		--W4 aka Good Dim Explosion Commands
		NOTESKIN:LoadActor( Var "Button", "Tap Explosion Dim W4" ) .. {
			InitCommand=function(self)
				self:diffusealpha(0)
			end;
			W4Command=NOTESKIN:GetMetricA("GhostArrowDim", "W1Command");
			JudgmentCommand=function(self)
				self:finishtweening()
			end;
			BrightCommand=function(self)
				self:visible(false)
			end;
			DimCommand=function(self)
				self:visible(true)
			end;
		};
		--W4 aka Good Bright Explosion Commands
		NOTESKIN:LoadActor( Var "Button", "Tap Explosion Dim W4" ) .. {
			InitCommand=function(self)
				self:diffusealpha(0)
			end;
			W4Command=NOTESKIN:GetMetricA("GhostArrowBright", "W1Command");
			JudgmentCommand=function(self)
				self:finishtweening()
			end;
			BrightCommand=function(self)
				self:visible(true)
			end;
			DimCommand=function(self)
				self:visible(false)
			end;
		};
	};
		Def.ActorFrame {
		--W5 aka Boo Dim Explosion Commands
		NOTESKIN:LoadActor( Var "Button", "Tap Explosion Dim W5" ) .. {
			InitCommand=function(self)
				self:diffusealpha(0)
			end;
			W5Command=NOTESKIN:GetMetricA("GhostArrowDim", "W1Command");
			JudgmentCommand=function(self)
				self:finishtweening()
			end;
			BrightCommand=function(self)
				self:visible(false)
			end;
			DimCommand=function(self)
				self:visible(true)
			end;
		};
		--W5 aka Boo Bright Explosion Commands
		NOTESKIN:LoadActor( Var "Button", "Tap Explosion Dim W5" ) .. {
			InitCommand=function(self)
				self:diffusealpha(0)
			end;
			W5Command=NOTESKIN:GetMetricA("GhostArrowBright", "W1Command");
			JudgmentCommand=function(self)
				self:finishtweening()
			end;
			BrightCommand=function(self)
				self:visible(true)
			end;
			DimCommand=function(self)
				self:visible(false)
			end;
		};
	};
	--Mine Explosion Commands
	NOTESKIN:LoadActor( Var "Button", "HitMine Explosion" ) .. {
		InitCommand=function(self)
			self:blend("BlendMode_Add"):diffusealpha(0)
		end;
		HitMineCommand=NOTESKIN:GetMetricA("GhostArrowBright", "HitMineCommand");
	};
}
return t;