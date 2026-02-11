#include "ScreenServiceAction.h"

#include <algorithm>
#include <string>
#include <vector>

#include "Bookkeeper.h"
#include "GameConstantsAndTypes.h"
#include "GameState.h"
#include "LocalizedString.h"
#include "MemoryCardManager.h"
#include "NotesLoaderSSC.h"
#include "PlayerNumber.h"
#include "PrefsManager.h"
#include "Profile.h"
#include "ProfileManager.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Screen.h"
#include "ScreenPrompt.h"
#include "StepMania.h"
#include "ThemeManager.h"
#include "global.h"

static LocalizedString BOOKKEEPING_DATA_CLEARED( "ScreenServiceAction", "Bookkeeping data cleared." );
static std::string ClearBookkeepingData()
{
	BOOKKEEPER->ClearAll();
	BOOKKEEPER->WriteToDisk();
	return BOOKKEEPING_DATA_CLEARED.GetValue();
}

static LocalizedString MACHINE_STATS_CLEARED( "ScreenServiceAction", "Machine stats cleared." );
std::string ClearMachineStats()
{
	Profile* pProfile = PROFILEMAN->GetMachineProfile();
	pProfile->ClearStats();
	PROFILEMAN->SaveMachineProfile();
	return MACHINE_STATS_CLEARED.GetValue();
}

static LocalizedString MACHINE_EDITS_CLEARED( "ScreenServiceAction", "%d edits cleared, %d errors." );
static std::string ClearMachineEdits()
{
	std::vector<std::string> vsEditFiles;
	GetDirListing( PROFILEMAN->GetProfileDir(ProfileSlot_Machine)+EDIT_STEPS_SUBDIR+"*.edit", vsEditFiles, false, true );
	GetDirListing( PROFILEMAN->GetProfileDir(ProfileSlot_Machine)+EDIT_COURSES_SUBDIR+"*.crs", vsEditFiles, false, true );

	int editCount = vsEditFiles.size();
	int removedCount = std::count_if(vsEditFiles.begin(), vsEditFiles.end(), [](std::string const &i) { return FILEMAN->Remove(i); });

	// reload the machine profile
	PROFILEMAN->SaveMachineProfile();
	PROFILEMAN->LoadMachineProfile();

	int errorCount = editCount - removedCount;
	return ssprintf(MACHINE_EDITS_CLEARED.GetValue().c_str(), editCount, errorCount);
}

static PlayerNumber GetFirstReadyMemoryCard()
{
	FOREACH_PlayerNumber( pn )
	{
		if( MEMCARDMAN->GetCardState(pn) != MemoryCardState_Ready )
			continue;	// skip

		if( !MEMCARDMAN->IsMounted(pn) )
			MEMCARDMAN->MountCard(pn);
		return pn;
	}

	return PLAYER_INVALID;
}

static LocalizedString MEMORY_CARD_EDITS_NOT_CLEARED	( "ScreenServiceAction", "Edits not cleared - No memory cards ready." );
static LocalizedString EDITS_CLEARED			( "ScreenServiceAction", "%d edits cleared, %d errors." );
static std::string ClearMemoryCardEdits()
{
	PlayerNumber pn = GetFirstReadyMemoryCard();
	if( pn == PLAYER_INVALID )
		return MEMORY_CARD_EDITS_NOT_CLEARED.GetValue();

	if( !MEMCARDMAN->IsMounted(pn) )
		MEMCARDMAN->MountCard(pn);

	std::string sDir = MEM_CARD_MOUNT_POINT[pn] + (std::string)PREFSMAN->m_sMemoryCardProfileSubdir + "/";
	std::vector<std::string> vsEditFiles;
	GetDirListing( sDir+EDIT_STEPS_SUBDIR+"*.edit", vsEditFiles, false, true );
	GetDirListing( sDir+EDIT_COURSES_SUBDIR+"*.crs", vsEditFiles, false, true );
	int editCount = vsEditFiles.size();
	int removedCount = std::count_if(vsEditFiles.begin(), vsEditFiles.end(), [](std::string const &i) { return FILEMAN->Remove(i); });

	MEMCARDMAN->UnmountCard(pn);

	return ssprintf(EDITS_CLEARED.GetValue().c_str(), editCount, editCount - removedCount);
}


static LocalizedString STATS_NOT_SAVED			( "ScreenServiceAction", "Stats not saved - No memory cards ready." );
static LocalizedString MACHINE_STATS_SAVED		( "ScreenServiceAction", "Machine stats saved to P%d card." );
static LocalizedString ERROR_SAVING_MACHINE_STATS	( "ScreenServiceAction", "Error saving machine stats to P%d card." );
static std::string TransferStatsMachineToMemoryCard()
{
	PlayerNumber pn = GetFirstReadyMemoryCard();
	if( pn == PLAYER_INVALID )
		return STATS_NOT_SAVED.GetValue();

	if( !MEMCARDMAN->IsMounted(pn) )
		MEMCARDMAN->MountCard(pn);

	std::string sDir = MEM_CARD_MOUNT_POINT[pn];
	sDir += "MachineProfile/";

	bool bSaved = PROFILEMAN->GetMachineProfile()->SaveAllToDir( sDir, PREFSMAN->m_bSignProfileData );

	MEMCARDMAN->UnmountCard(pn);

	if( bSaved )
		return ssprintf(MACHINE_STATS_SAVED.GetValue().c_str(),pn+1);
	else
		return ssprintf(ERROR_SAVING_MACHINE_STATS.GetValue().c_str(),pn+1);
}

static LocalizedString STATS_NOT_LOADED		( "ScreenServiceAction", "Stats not loaded - No memory cards ready." );
static LocalizedString MACHINE_STATS_LOADED	( "ScreenServiceAction", "Machine stats loaded from P%d card." );
static LocalizedString THERE_IS_NO_PROFILE	( "ScreenServiceAction", "There is no machine profile on P%d card." );
static LocalizedString PROFILE_CORRUPT		( "ScreenServiceAction", "The profile on P%d card contains corrupt or tampered data." );
static std::string TransferStatsMemoryCardToMachine()
{
	PlayerNumber pn = GetFirstReadyMemoryCard();
	if( pn == PLAYER_INVALID )
		return STATS_NOT_LOADED.GetValue();

	if( !MEMCARDMAN->IsMounted(pn) )
		MEMCARDMAN->MountCard(pn);

	std::string sDir = MEM_CARD_MOUNT_POINT[pn];
	sDir += "MachineProfile/";

	Profile backup = *PROFILEMAN->GetMachineProfile();

	ProfileLoadResult lr = PROFILEMAN->GetMachineProfile()->LoadAllFromDir( sDir, PREFSMAN->m_bSignProfileData );
	std::string s;
	switch( lr )
	{
	case ProfileLoadResult_Success:
		s = ssprintf(MACHINE_STATS_LOADED.GetValue().c_str(),pn+1);
		break;
	case ProfileLoadResult_FailedNoProfile:
		*PROFILEMAN->GetMachineProfile() = backup;
		s = ssprintf(THERE_IS_NO_PROFILE.GetValue().c_str(),pn+1);
		break;
	case ProfileLoadResult_FailedTampered:
		*PROFILEMAN->GetMachineProfile() = backup;
		s = ssprintf(PROFILE_CORRUPT.GetValue().c_str(),pn+1);
		break;
	default:
		FAIL_M(ssprintf("Invalid profile load result: %i", lr));
	}

	MEMCARDMAN->UnmountCard(pn);

	return s;
}

static void CopyEdits( const std::string &sFromProfileDir, const std::string &sToProfileDir, int &iNumSucceeded, int &iNumOverwritten, int &iNumIgnored, int &iNumErrored )
{
	iNumSucceeded = 0;
	iNumOverwritten = 0;
	iNumIgnored = 0;
	iNumErrored = 0;

	{
		std::string sFromDir = sFromProfileDir + EDIT_STEPS_SUBDIR;
		std::string sToDir = sToProfileDir + EDIT_STEPS_SUBDIR;

		std::vector<std::string> vsFiles;
		GetDirListing( sFromDir+"*.edit", vsFiles, false, false );
		for (std::string const &i : vsFiles)
		{
			if( DoesFileExist(sToDir + i) )
				iNumOverwritten++;
			bool bSuccess = FileCopy( sFromDir + i, sToDir + i );
			if( bSuccess )
				iNumSucceeded++;
			else
				iNumErrored++;

			// Test whether the song we need for this edit is present and ignore this edit if not present.
			SSCLoader loaderSSC;
			if( !loaderSSC.LoadEditFromFile( sFromDir + i, ProfileSlot_Machine, false ) )
			{
				iNumIgnored++;
				continue;
			}
		}
	}

	// TODO: Seprarate copying stats for steps and courses

	{
		std::string sFromDir = sFromProfileDir + EDIT_COURSES_SUBDIR;
		std::string sToDir = sToProfileDir + EDIT_COURSES_SUBDIR;

		std::vector<std::string> vsFiles;
		GetDirListing( sFromDir+"*.crs", vsFiles, false, false );
		for (std::string const &i : vsFiles)
		{
			if( DoesFileExist(sToDir + i) )
				iNumOverwritten++;
			bool bSuccess = FileCopy( sFromDir + i, sToDir + i );
			if( bSuccess )
				iNumSucceeded++;
			else
				iNumErrored++;
		}
	}
}

static LocalizedString EDITS_NOT_COPIED		( "ScreenServiceAction", "Edits not copied - No memory cards ready." );
static LocalizedString COPIED_TO_CARD		( "ScreenServiceAction", "Copied to P%d card:" );
static LocalizedString COPIED			( "ScreenServiceAction", "%d copied" );
static LocalizedString OVERWRITTEN		( "ScreenServiceAction", "%d overwritten" );
static LocalizedString ADDED			( "ScreenServiceAction", "%d added" );
static LocalizedString IGNORED			( "ScreenServiceAction", "%d ignored" );
static LocalizedString FAILED			( "ScreenServiceAction", "%d failed" );
static LocalizedString DELETED			( "ScreenServiceAction", "%d deleted" );

static std::string CopyEdits( const std::string &sFromProfileDir, const std::string &sToProfileDir, const std::string &sDisplayDir )
{
	int iNumSucceeded = 0;
	int iNumOverwritten = 0;
	int iNumIgnored = 0;
	int iNumErrored = 0;

	CopyEdits( sFromProfileDir, sToProfileDir, iNumSucceeded, iNumOverwritten, iNumIgnored, iNumErrored );

	std::vector<std::string> vs;
	vs.push_back( sDisplayDir );
	vs.push_back( ssprintf( COPIED.GetValue().c_str(), iNumSucceeded ) + ", " + ssprintf( OVERWRITTEN.GetValue().c_str(), iNumOverwritten ) );
	if( iNumIgnored )
		vs.push_back( ssprintf( IGNORED.GetValue().c_str(), iNumIgnored ) );
	if( iNumErrored )
		vs.push_back( ssprintf( FAILED.GetValue().c_str(), iNumErrored ) );
	return join( "\n", vs );
}

static void SyncFiles( const std::string &sFromDir, const std::string &sToDir, const std::string &sMask, int &iNumAdded, int &iNumDeleted, int &iNumOverwritten, int &iNumFailed )
{
	std::vector<std::string> vsFilesSource;
	GetDirListing( sFromDir+sMask, vsFilesSource, false, false );

	std::vector<std::string> vsFilesDest;
	GetDirListing( sToDir+sMask, vsFilesDest, false, false );

	std::vector<std::string> vsToDelete;
	GetAsNotInBs( vsFilesDest, vsFilesSource, vsToDelete );

	for( unsigned i = 0; i < vsToDelete.size(); ++i )
	{
		std::string sFile = sToDir + vsToDelete[i];
		LOG->Trace( "Delete \"%s\"", sFile.c_str() );

		if( FILEMAN->Remove(sFile) )
			++iNumDeleted;
		else
			++iNumFailed;
	}

	for( unsigned i = 0; i < vsFilesSource.size(); ++i )
	{
		std::string sFileFrom = sFromDir + vsFilesSource[i];
		std::string sFileTo = sToDir + vsFilesSource[i];
		LOG->Trace( "Copy \"%s\"", sFileFrom.c_str() );
		bool bOverwrite = DoesFileExist( sFileTo );
		bool bSuccess = FileCopy( sFileFrom, sFileTo );
		if( bSuccess )
		{
			if( bOverwrite )
				++iNumOverwritten;
			else
				++iNumAdded;
		}
		else
			++iNumFailed;
	}
	FILEMAN->FlushDirCache( sToDir );
}

static void SyncEdits( const std::string &sFromDir, const std::string &sToDir, int &iNumAdded, int &iNumDeleted, int &iNumOverwritten, int &iNumFailed )
{
	iNumAdded = 0;
	iNumDeleted = 0;
	iNumOverwritten = 0;
	iNumFailed = 0;

	SyncFiles( sFromDir + EDIT_STEPS_SUBDIR, sToDir + EDIT_STEPS_SUBDIR, "*.edit", iNumAdded, iNumDeleted, iNumOverwritten, iNumFailed );
	SyncFiles( sFromDir + EDIT_COURSES_SUBDIR, sToDir + EDIT_COURSES_SUBDIR, "*.crs", iNumAdded, iNumDeleted, iNumOverwritten, iNumFailed );
}

static std::string CopyEditsMachineToMemoryCard()
{
	PlayerNumber pn = GetFirstReadyMemoryCard();
	if( pn == PLAYER_INVALID )
		return EDITS_NOT_COPIED.GetValue();

	if( !MEMCARDMAN->IsMounted(pn) )
		MEMCARDMAN->MountCard(pn);

	std::string sFromDir = PROFILEMAN->GetProfileDir(ProfileSlot_Machine);
	std::string sToDir = MEM_CARD_MOUNT_POINT[pn] + (std::string)PREFSMAN->m_sMemoryCardProfileSubdir + "/";

	std::vector<std::string> vs;
	vs.push_back( ssprintf( COPIED_TO_CARD.GetValue().c_str(), pn+1 ) );
	std::string s = CopyEdits( sFromDir, sToDir, PREFSMAN->m_sMemoryCardProfileSubdir );
	vs.push_back( s );

	MEMCARDMAN->UnmountCard(pn);

	return join("\n\n",vs);
}

static std::string SyncEditsMachineToMemoryCard()
{
	PlayerNumber pn = GetFirstReadyMemoryCard();
	if( pn == PLAYER_INVALID )
		return EDITS_NOT_COPIED.GetValue();

	if( !MEMCARDMAN->IsMounted(pn) )
		MEMCARDMAN->MountCard(pn);

	int iNumAdded = 0;
	int iNumDeleted = 0;
	int iNumOverwritten = 0;
	int iNumFailed = 0;

	std::string sFromDir = PROFILEMAN->GetProfileDir(ProfileSlot_Machine);
	std::string sToDir = MEM_CARD_MOUNT_POINT[pn] + (std::string)PREFSMAN->m_sMemoryCardProfileSubdir + "/";
	SyncEdits( sFromDir, sToDir, iNumAdded, iNumDeleted, iNumOverwritten, iNumFailed );

	MEMCARDMAN->UnmountCard(pn);

	std::string sRet = ssprintf( COPIED_TO_CARD.GetValue().c_str(), pn+1 ) + " ";
	sRet += ssprintf( ADDED.GetValue().c_str(), iNumAdded ) + ", " + ssprintf( OVERWRITTEN.GetValue().c_str(), iNumOverwritten );
	if( iNumDeleted )
		sRet += std::string(" ") + ssprintf( DELETED.GetValue().c_str(), iNumDeleted );
	if( iNumFailed )
		sRet += std::string("; ") + ssprintf( FAILED.GetValue().c_str(), iNumFailed );
	return sRet;
}

static LocalizedString COPIED_FROM_CARD( "ScreenServiceAction", "Copied from P%d card:" );
static std::string CopyEditsMemoryCardToMachine()
{
	PlayerNumber pn = GetFirstReadyMemoryCard();
	if( pn == PLAYER_INVALID )
		return EDITS_NOT_COPIED.GetValue();

	if( !MEMCARDMAN->IsMounted(pn) )
		MEMCARDMAN->MountCard(pn);

	std::vector<std::string> vsSubDirs;
	ProfileManager::GetMemoryCardProfileDirectoriesToTry( vsSubDirs );

	std::vector<std::string> vs;
	vs.push_back( ssprintf( COPIED_FROM_CARD.GetValue().c_str(), pn+1 ) );

	for (std::string const &sSubDir : vsSubDirs)
	{
		std::string sFromDir = MEM_CARD_MOUNT_POINT[pn] + sSubDir + "/";
		std::string sToDir = PROFILEMAN->GetProfileDir(ProfileSlot_Machine);

		std::string s = CopyEdits( sFromDir, sToDir, sSubDir );
		vs.push_back( s );
	}

	MEMCARDMAN->UnmountCard(pn);

	// reload the machine profile
	PROFILEMAN->SaveMachineProfile();
	PROFILEMAN->LoadMachineProfile();

	return join("\n\n",vs);
}

static LocalizedString PREFERENCES_RESET( "ScreenServiceAction", "Preferences reset." );
static std::string ResetPreferences()
{
	StepMania::ResetPreferences();
	return PREFERENCES_RESET.GetValue();
}



REGISTER_SCREEN_CLASS( ScreenServiceAction );
void ScreenServiceAction::BeginScreen()
{
	std::string sActions = THEME->GetMetric(m_sName,"Actions");
	std::vector<std::string> vsActions;
	split( sActions, ",", vsActions );

	std::vector<std::string> vsResults;
	for (std::string const &s : vsActions)
	{
		std::string (*pfn)() = nullptr;

		if(	 s == "ClearBookkeepingData" )			pfn = ClearBookkeepingData;
		else if( s == "ClearMachineStats" )			pfn = ClearMachineStats;
		else if( s == "ClearMachineEdits" )			pfn = ClearMachineEdits;
		else if( s == "ClearMemoryCardEdits" )			pfn = ClearMemoryCardEdits;
		else if( s == "TransferStatsMachineToMemoryCard" )	pfn = TransferStatsMachineToMemoryCard;
		else if( s == "TransferStatsMemoryCardToMachine" )	pfn = TransferStatsMemoryCardToMachine;
		else if( s == "CopyEditsMachineToMemoryCard" )		pfn = CopyEditsMachineToMemoryCard;
		else if( s == "CopyEditsMemoryCardToMachine" )		pfn = CopyEditsMemoryCardToMachine;
		else if( s == "SyncEditsMachineToMemoryCard" )		pfn = SyncEditsMachineToMemoryCard;
		else if( s == "ResetPreferences" )			pfn = ResetPreferences;

		ASSERT_M( pfn != nullptr, s );

		std::string sResult = pfn();
		vsResults.push_back( sResult );
	}

	ScreenPrompt::SetPromptSettings( join( "\n", vsResults ), PROMPT_OK );

	ScreenPrompt::BeginScreen();
}

/*
 * (c) 2001-2005 Chris Danford
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, and/or sell copies of the Software, and to permit persons to
 * whom the Software is furnished to do so, provided that the above
 * copyright notice(s) and this permission notice appear in all copies of
 * the Software and that both the above copyright notice(s) and this
 * permission notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS
 * INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT
 * OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
