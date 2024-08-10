
#include "global.h"

#include "ActorFrame.h"
#include "GameplayAssist.h"
#include "Player.h"
#include "NoteData.h"

#include <iterator>
#include <map>
#include <unordered_map>
#include <vector>

// This class is intended to manage player state during edit mode playback,
// aka practice mode.
class EditModePlayerManager
{
public:

	// Adds players based on the current gamestate.
	void AddPlayers(const NoteData& note_data);

	// Adds the players (and their notefields) to the desired actor frame.
	void AddPlayersToActorFrame(ActorFrame& frame);

	// Reload the note data for each player. Intended to be called
	// just before playback begins.
	void ReloadNoteData(const NoteData& note_data);

	// Toggles visiblity of the player(s) notefield.
	void SetVisible(bool visible);

	// During playback, if a player hits a button, handle the input. Only looks
	// at inputs related to gameplay, not menuing.
	bool HandleGameplayInput(const InputEventPlus& input, const GameButtonType& gbt);

	// If autoplay is enabled, force the player's state into autoplay mode.
	void SetupAutoplay();

	// Player::CacheAllUsedNoteSkins on each player.
	void CacheAllUsedNoteSkins();

	// Play assist ticks.
	void PlayTicks(GameplayAssist& gameplay_assist);

private:
	// All players that the manager is looking at. Indexable by PlayerNumber.
	std::unordered_map<PlayerNumber, std::shared_ptr<PlayerPlus>> players_;
};
