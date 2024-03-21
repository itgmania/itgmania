ITGmania fork :3



here are my messy notes for this fork. massive greetz to Squirrel from outfox for pointing us on the right direction here.



most important:

- windows uses non monolithic time  but  linux/mac uses monolithic time which is directly tied in with the issue and why windows users are seeing it worst

- get rid of std:lrint() / lrintf, replace with int(roundf  [optimizes a lot faster than lrintf]
  
  - definitely change any in ragesound, but Squirrel suggests changing ANY lrint except in the arrow effects math
  - if youre running on vsync, you only have 1 tick to catch up on; if the game lags 8ms or more with vsync off it cant catch up until the next tick

- lines 17-18 in RageSoundReader.cpp causes stutter by messing up the posmap if the read time is slow, causing the 'Approximate sound time' issue, https://github.com/itgmania/itgmania/blob/04647648765b082414993e6f2ce420b2d950840b/src/RageSoundReader.cpp#L17-L18  
  proposed fix. this is not needed anymore because the problem this was addressing is not present since sm3.9:

```
if ( fRate )
    *fRate = this->GetStreamToSourceRatio();
if ( iSourceFrame )
    *iSourceFrame = this->GetNextSourceFrame();

int iGotFrames = this->Read( pBuffer, iFrames );

if( iGotFrames == RageSoundReader::STREAM_LOOPED )
    iGotFrames = 0;

if( iGotFrames != 0 )
    return iGotFrames;
```

- in RageSoundPosMap, change int m_iFrames to int64, https://github.com/itgmania/itgmania/blob/04647648765b082414993e6f2ce420b2d950840b/src/RageSoundPosMap.cpp#L20
  
  - refer to screenshot for replacement code at lines 129-152
  - pClosestBlock isn't used at all so its safe to comment out anywhere in this file
  - also comment out 181-189, dont do the deltatime here
  - make sure to get the lrint's too, use a fastround here
  - line 98ish,   you dont need to mplace the whole map, make it so its   `->m_Queue.emplace_back();`

- Note for testing on Dimo's setup: please try with this code commented out https://github.com/itgmania/itgmania/blob/04647648765b082414993e6f2ce420b2d950840b/src/GameLoop.cpp#L295
  
  - note in outfox there is no `LIGHTSMAN->Update(fDeltaTime);` in the line after the SCREENMAN->Draw

other stuff related to the sync bug:

- suggested to comment out the following code in the main GameLoop.cpp, https://github.com/itgmania/itgmania/blob/04647648765b082414993e6f2ce420b2d950840b/src/GameLoop.cpp#L324-L331 - all you need is CheckFocus(); and then UpdateAllButDraw, OR move SCREENMAN->Draw before input devices changed, this will reduce stuttering

other stuff to note:

- ragesoundreader has to stay even with a new sound backend
- DR_MP3_IMPLEMENTATION in RageSoundReader_MP3 should be changed to get rid of the libmad dependency
- they found ragesoundreader cant actually handle audio drivers bc year 2001 code where it counts audio frames processed by the cpu which are then sent to the sound card. they estimate ~8 months constant work to remove this but intend to open source it in the future
- outfox has a separate clock that uses chrono time that hooks back into ragetimer, this is to prevent the clock from going backwards on windows (which it cant do on mac or linux), windows has like 6 different clocks but you dont actually know what clock you get hooked onto, so it can snap back and forth trying to compensate for this. their chrono based clock prevents that
- screenman->draw can cause stuttering on popups possibly
- set this to HIGH_PRIORITY_CLASS to prevent OBS from stealing the rendering thread https://github.com/itgmania/itgmania/blob/04647648765b082414993e6f2ce420b2d950840b/src/arch/ArchHooks/ArchHooks_Win32.cpp#L157-L158
  
  

noted for din's image, instantly kills any drift on linux!!!: 

- in ArchHooks_Unix, there is a bug with the test code for g_Clock,  force return it as CLOCK_MONOTONIC, OpenGetTime() is not providing the right answer so just have GetClock() return CLOCK_MONOTONIC. this prevents pulseaudio from skewing. Line 164  is the line that causes stutter,  because it takes negative time into account, if CLOCK_MONOTONIC is forced then this can be removed.
