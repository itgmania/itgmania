#include "CommandLineActions.h"
#include "RageLog.h"
#include "RageThreads.h"
#include "RageUtil.h"
#include "global.h"

#import <Cocoa/Cocoa.h>
#include "ProductInfo.h"
#include "RageInput.h"
#include "StepMania.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "arch/InputHandler/InputHandler_NSEvent.hpp"
#include "archutils/Darwin/CocoaEventDispatcher.h"

@interface NSApplication (PrivateShutUpWarning)
- (void)setAppleMenu:(NSMenu*)menu;
@end

/* Replacement NSApplication class.
 * Replaces sendEvent so that key down events are only sent to the
 * menu bar. We handle all other input by reading HID events from the
 * keyboard directly. */
@interface SMApplication : NSApplication
- (void)fullscreen:(id)sender;
@end

@implementation SMApplication
- (void)fullscreen:(id)sender {
  // don't use ArchHooks::SetToggleWindowed(), it makes the screen black
  [[self mainWindow] toggleFullScreen:nil];
}

- (void)sendEvent:(NSEvent*)event {
  if (INPUTMAN) {
    auto& dispatcher = CocoaEventDispatcher::shared_dispatcher_;
    dispatcher.DispatchEvent(event);
  }

  if ([event type] == NSEventTypeKeyDown) {
    [[self mainMenu] performKeyEquivalent:event];
  } else {
    [super sendEvent:event];
  }
}
@end

// The main class of the application, the application's delegate.
@interface SMMain : NSObject {
  int argc_;
  char** argv_;
  BOOL application_launched_;
}
- (id)initWithArgc:(int)argc argv:(char**)argv;
- (void)startGame:(id)sender;
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender;
- (BOOL)application:(NSApplication*)app openFile:(NSString*)file;
- (void)application:(NSApplication*)app openFiles:(NSArray*)files;
- (void)setForInstall:(NSArray*)files;
@end

@implementation SMMain

- (id)initWithArgc:(int)argc argv:(char**)argv {
  self = [super init];
  if (argc == 2 && !strncmp(argv[1], "-psn_", 5)) {
    argc = 1;
  }
  argc_ = argc;
  argv_ = new char*[argc];
  for (int i = 0; i < argc; ++i) {
    argv_[i] = argv[i];
  }
  application_launched_ = NO;
  return self;
}

- (void)startGame:(id)sender {
  // Hand off to main application code.
  exit(sm_main(argc_, argv_));
}

/* From here:
 * http://www.cocoadev.com/index.pl?HowToRegisterURLHandler */
- (void)getUrl:(NSAppleEventDescriptor*)event withReplyEvent:(NSAppleEventDescriptor*)reply_event {
  const char* url = [[[event paramDescriptorForKeyword:keyDirectObject] stringValue] UTF8String];

  LOG->Info("Parsing URL: %s", url);

  // I'm not sure this handles everything it needs to. - Colby
  CommandLineActions::CommandLineArgs args;
  args.argv_.push_back(url);

  CommandLineActions::ToProcess.push_back(args);
}

// Called when the internal event loop has just started running.
- (void)applicationDidFinishLaunching:(NSNotification*)note {
  application_launched_ = YES;
  [NSThread detachNewThreadSelector:@selector(startGame:) toTarget:self withObject:nil];

  // Register ourselves as a URL handler.
  [[NSAppleEventManager sharedAppleEventManager] setEventHandler:self
                                                     andSelector:@selector(getUrl:withReplyEvent:)
                                                   forEventClass:kInternetEventClass
                                                      andEventID:kAEGetURL];
}

- (BOOL)application:(NSApplication*)app openFile:(NSString*)file {
  NSArray* files = [NSArray arrayWithObject:file];
  if (application_launched_) {
    [NSTask launchedTaskWithLaunchPath:[NSString stringWithUTF8String:argv_[0]] arguments:files];
  } else {
    [self setForInstall:files];
  }
  return YES;
}

- (void)application:(NSApplication*)app openFiles:(NSArray*)files {
  if (application_launched_) {
    [NSTask launchedTaskWithLaunchPath:[NSString stringWithUTF8String:argv_[0]] arguments:files];
  } else {
    [self setForInstall:files];
  }
  [app replyToOpenOrPrint:NSApplicationDelegateReplySuccess];
}

- (void)setForInstall:(NSArray*)files {
  char** temp = new char*[[files count] + argc_];
  for (int i = 0; i < argc_; ++i) {
    temp[i] = argv_[i];
  }
  for (unsigned i = 0; i < [files count]; ++i, ++argc_) {
    const char* p = [[files objectAtIndex:i] fileSystemRepresentation];
    temp[argc_] = new char[strlen(p) + 1];
    strcpy(temp[argc_], p);
  }
  delete[] argv_;
  argv_ = temp;
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
  ArchHooks::SetUserQuit();
  return NSTerminateCancel;
}
@end

static void HandleNSException(NSException* exception) {
  FAIL_M(ssprintf("%s raised: %s", [[exception name] UTF8String], [[exception reason] UTF8String]));
}

static NSMenuItem* MenuItem(NSString* title, SEL action, NSString* code) {
  // Autorelease these because they'll be retained by the NSMenu.
  return [[[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:code] autorelease];
}

static void SetupMenus(void) {
  // Get the localized strings from the file.
  NSString* window = NSLocalizedString(@"Window", @"Menu title");
  NSString* hide_others_str = NSLocalizedString(@"Hide Others", @"Menu item");
  NSString* about = NSLocalizedString(@"About " PRODUCT_FAMILY, @"Menu item");
  NSString* hide = NSLocalizedString(@"Hide " PRODUCT_FAMILY, @"Menu item");
  NSString* show_all = NSLocalizedString(@"Show All", @"Menu item");
  NSString* quit = NSLocalizedString(@"Quit " PRODUCT_FAMILY, @"Menu item");
  NSString* minimize = NSLocalizedString(@"Minimize", @"Menu item");
  NSString* enter_full_screen = NSLocalizedString(@"Enter Full Screen", @"Menu item");

  NSMenu* main_menu = [[[NSMenu alloc] initWithTitle:@""] autorelease];
  NSMenu* app_menu = [[[NSMenu alloc] initWithTitle:@PRODUCT_FAMILY] autorelease];
  NSMenu* window_menu = [[[NSMenu alloc] initWithTitle:window] autorelease];
  NSMenuItem* hide_others = MenuItem(hide_others_str, @selector(hideOtherApplications:), @"h");

  [hide_others setKeyEquivalentModifierMask:NSEventModifierFlagOption | NSEventModifierFlagCommand];

  [app_menu addItem:MenuItem(about, @selector(orderFrontStandardAboutPanel:), @"")];
  [app_menu addItem:[NSMenuItem separatorItem]];
  [app_menu addItem:MenuItem(hide, @selector(hide:), @"h")];
  [app_menu addItem:hide_others];
  [app_menu addItem:MenuItem(show_all, @selector(unhideAllApplications:), @"")];
  [app_menu addItem:[NSMenuItem separatorItem]];
  [app_menu addItem:MenuItem(quit, @selector(terminate:), @"q")];

  [window_menu addItem:MenuItem(minimize, @selector(performMiniaturize:), @"m")];
  [window_menu addItem:[NSMenuItem separatorItem]];

  // Add a Full Screen item.
  NSMenuItem* item = MenuItem(enter_full_screen, @selector(fullscreen:), @"\n");

  [item setKeyEquivalentModifierMask:NSEventModifierFlagOption];  // opt-enter
  [window_menu addItem:item];

  [[main_menu addItemWithTitle:[app_menu title] action:nil keyEquivalent:@""] setSubmenu:app_menu];
  [[main_menu addItemWithTitle:[window_menu title] action:nil
                 keyEquivalent:@""] setSubmenu:window_menu];

  [NSApp setMainMenu:main_menu];
  [NSApp setAppleMenu:app_menu];  // This isn't the apple menu, but it doesn't work without this.
  [NSApp setWindowsMenu:window_menu];
}

#undef main

int main(int argc, char** argv) {
  RageThreadRegister gui_thread("GUI thread");
  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  SMMain* sm;

  // Ensure the application object is initialised, this sets NSApp.
  [SMApplication sharedApplication];

  // Set up NSException handler.
  NSSetUncaughtExceptionHandler(HandleNSException);

  // Set up the menubar.
  SetupMenus();

  // Create SMMain and make it the app delegate.
  sm = [[SMMain alloc] initWithArgc:argc argv:argv];
  [NSApp setDelegate:static_cast<id<NSApplicationDelegate>>(sm)];

  [pool release];
  // Start the main event loop.
  [NSApp run];
  [sm release];
  return 0;
}

/*
 * (c) 2005-2009 Steve Checkoway
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
