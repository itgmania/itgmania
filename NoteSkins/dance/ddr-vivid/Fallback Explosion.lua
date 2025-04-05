--If a Command has "NOTESKIN:GetMetricA" in it, that means it gets the command from the metrics.ini, else use cmd(); to define command.

local t = Def.ActorFrame {
	--Hold Explosion Commands
	NOTESKIN:LoadActor( Var "Button", "Hold Explosion" ) .. {
		HoldingOnCommand=NOTESKIN:GetMetricA("HoldGhostArrow", "HoldingOnCommand");
		HoldingOffCommand=NOTESKIN:GetMetricA("HoldGhostArrow", "HoldingOffCommand");
		InitCommand=cmd(playcommand,"HoldingOff";finishtweening);
	};
	--Roll Explosion Commands
	NOTESKIN:LoadActor( Var "Button", "Roll Explosion" ) .. {
		RollOnCommand=NOTESKIN:GetMetricA("HoldGhostArrow", "RollOnCommand");
		RollOffCommand=NOTESKIN:GetMetricA("HoldGhostArrow", "RollOffCommand");
		InitCommand=cmd(playcommand,"RollOff";finishtweening);
	};
	--Dim Explosion Commands
	NOTESKIN:LoadActor( Var "Button", "Tap Explosion Dim" ) .. {
		InitCommand=cmd(diffusealpha,0);
		W5Command=NOTESKIN:GetMetricA("GhostArrowDim", "W5Command");
		W4Command=NOTESKIN:GetMetricA("GhostArrowDim", "W4Command");
		W3Command=NOTESKIN:GetMetricA("GhostArrowDim", "W3Command");
		W2Command=NOTESKIN:GetMetricA("GhostArrowDim", "W2Command");
		BrightCommand=NOTESKIN:GetMetricA("GhostArrowDim", "W1Command");
		JudgmentCommand=cmd(finishtweening);
	};
	--Bright Explosion Commands
	NOTESKIN:LoadActor( Var "Button", "Tap Explosion Bright" ) .. {
		InitCommand=cmd(diffusealpha,0);
		HeldCommand=NOTESKIN:GetMetricA("GhostArrowBright", "HeldCommand");
		W1Command=NOTESKIN:GetMetricA("GhostArrowBright", "W1Command");
		JudgmentCommand=cmd(finishtweening);
	};
	--Mine Explosion Commands
	NOTESKIN:LoadActor( Var "Button", "HitMine Explosion" ) .. {
		InitCommand=cmd(blend,"BlendMode_Add";diffusealpha,0);
		HitMineCommand=NOTESKIN:GetMetricA("GhostArrowBright", "HitMineCommand");
	};
}
return t;