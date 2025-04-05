#include <vector>


// A very simple pair floats that represent the pixels of the left and right
// sides of a player's notefield margins. 40 is an arbitrary default value for
// the margins.
struct NotefieldMargins {
	float left = 40;
	float right = 40;
};

// Use the lua MarginFunction (defined in metrics.ini) to calculate where the notefields
// should be. This should (but doesn't have to) the engine's CenterP1 preference.
// 
// By default points to GameplayMargins in Themes/_fallback/Scripts/03 Gameplay.lua
std::vector<NotefieldMargins> GetNotefieldMargins();
