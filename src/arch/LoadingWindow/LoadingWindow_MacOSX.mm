#import "LoadingWindow_MacOSX.h"
#import <Cocoa/Cocoa.h>
#import "ProductInfo.h"
#import "RageFile.h"
#import "RageUtil.h"
#include "ThemeManager.h"

#include <vector>

@interface LoadingWindowHelper : NSObject {
 @public
  NSWindow* window_;
  NSTextView* text_;
  NSAutoreleasePool* pool_;
  NSProgressIndicator* progress_indicator_;
}
- (void)setupWindow:(NSImage*)image;
- (void)setProgress:(NSNumber*)progress;
- (void)setTotalWork:(NSNumber*)totalWork;
- (void)setIndeterminate:(NSNumber*)indeterminate;
@end

@implementation LoadingWindowHelper
- (void)setupWindow:(NSImage*)image {
  NSSize size = [image size];
  NSRect viewRect, windowRect;
  float height = 0.0f;
  float padding = 5.0f;

  NSRect progressIndicatorRect;
  progressIndicatorRect = NSMakeRect(padding, padding, size.width - padding * 2.0f, 0);
  progress_indicator_ = [[NSProgressIndicator alloc] initWithFrame:progressIndicatorRect];
  [progress_indicator_ sizeToFit];
  [progress_indicator_ setIndeterminate:YES];
  [progress_indicator_ setMinValue:0];
  [progress_indicator_ setMaxValue:1];
  [progress_indicator_ setDoubleValue:0];
  progressIndicatorRect = [progress_indicator_ frame];
  float progressHeight = progressIndicatorRect.size.height;

  NSFont* font = [NSFont systemFontOfSize:0.0f];
  NSRect textRect;
  // Just give it a size until it is created.
  textRect = NSMakeRect(0, progressHeight + padding, size.width, size.height);
  text_ = [[NSTextView alloc] initWithFrame:textRect];
  [text_ setFont:font];
  height = [[text_ layoutManager] defaultLineHeightForFont:font] * 3 + 4;
  textRect = NSMakeRect(0, progressHeight + padding, size.width, height);

  [text_ setFrame:textRect];
  [text_ setEditable:NO];
  [text_ setSelectable:NO];
  [text_ setDrawsBackground:NO];
  [text_ setBackgroundColor:[NSColor lightGrayColor]];
  [text_ setAlignment:NSTextAlignmentCenter];
  [text_ setHorizontallyResizable:NO];
  [text_ setVerticallyResizable:NO];
  [text_ setString:@"Initializing Hardware..."];

  viewRect = NSMakeRect(0, height + progressHeight + padding, size.width, size.height);
  NSImageView* iView = [[NSImageView alloc] initWithFrame:viewRect];
  [iView setImage:image];
  [iView setImageFrameStyle:NSImageFrameNone];

  windowRect = NSMakeRect(0, 0, size.width, size.height + height + progressHeight + padding);
  window_ = [[NSWindow alloc] initWithContentRect:windowRect
                                        styleMask:NSWindowStyleMaskTitled
                                          backing:NSBackingStoreBuffered
                                            defer:YES];

  NSView* view = [window_ contentView];

  // Set some properties.
  [window_ setReleasedWhenClosed:YES];
  [window_ setExcludedFromWindowsMenu:YES];
  [window_ setTitle:@PRODUCT_FAMILY];
  [window_ center];

  // Set subviews.
  [view addSubview:text_];
  [text_ release];

  [view addSubview:iView];
  [iView release];

  [view addSubview:progress_indicator_];

  // Display the window.
  [window_ makeKeyAndOrderFront:nil];
}

- (void)setProgress:(NSNumber*)progress {
  [progress_indicator_ setDoubleValue:[progress doubleValue]];
}

- (void)setTotalWork:(NSNumber*)totalWork {
  [progress_indicator_ setMaxValue:[totalWork doubleValue]];
}

- (void)setIndeterminate:(NSNumber*)indeterminate {
  [progress_indicator_ setIndeterminate:([indeterminate doubleValue] > 0 ? YES : NO)];
}

@end

static LoadingWindowHelper* g_Helper = nil;

LoadingWindow_MacOSX::LoadingWindow_MacOSX() {}

LoadingWindow_MacOSX::~LoadingWindow_MacOSX() {
  if (!g_Helper) {
    return;
  }
  NSAutoreleasePool* pool = g_Helper->pool_;
  [g_Helper->window_ performSelectorOnMainThread:@selector(close) withObject:nil waitUntilDone:YES];
  [g_Helper release];
  g_Helper = nil;
  [pool release];
}

void LoadingWindow_MacOSX::SetText(std::string str) {
  if (!g_Helper) {
    return;
  }
  NSString* s = [[NSString alloc] initWithUTF8String:(str.c_str())];
  [g_Helper->text_ performSelectorOnMainThread:@selector(setString:)
                                    withObject:(s ? s : @"")waitUntilDone:NO];
  [s release];
}

void LoadingWindow_MacOSX::SetSplash(const RageSurface* pSplash) {
  RageFile f;
  std::string data;
  std::vector<std::string> vs;

  // Try to load a custom splash from the current theme, first.
  GetDirListing(THEME->GetPathG("Common", "splash"), vs, false, true);

  //	if no Common splash file was found in the theme...
  if (vs.empty() || !f.Open(vs[0])) {
    // then try the stock splash.png from ./Data/
    GetDirListing("Data/splash*.png", vs, false, true);
  }

  if (vs.empty() || !f.Open(vs[0])) {
    return;
  }
  f.Read(data);
  if (data.empty()) {
    return;
  }

  NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
  NSImage* image = nil;
  NSData* d = [[NSData alloc] initWithBytes:data.data() length:data.length()];
  image = [[NSImage alloc] initWithData:d];
  [d release];
  if (!image) {
    [pool release];
    return;
  }

  g_Helper = [[LoadingWindowHelper alloc] init];
  g_Helper->pool_ = pool;
  [g_Helper performSelectorOnMainThread:@selector(setupWindow:) withObject:image waitUntilDone:NO];
  [image release];
}

void LoadingWindow_MacOSX::SetProgress(const int progress) {
  [g_Helper performSelectorOnMainThread:@selector(setProgress:)
                             withObject:[NSNumber numberWithDouble:(double)progress]
                          waitUntilDone:NO];
}

void LoadingWindow_MacOSX::SetTotalWork(const int totalWork) {
  [g_Helper performSelectorOnMainThread:@selector(setTotalWork:)
                             withObject:[NSNumber numberWithDouble:(double)totalWork]
                          waitUntilDone:NO];
}

void LoadingWindow_MacOSX::SetIndeterminate(bool indeterminate) {
  double tmp = indeterminate ? 1 : 0;
  [g_Helper performSelectorOnMainThread:@selector(setIndeterminate:)
                             withObject:[NSNumber numberWithDouble:tmp]
                          waitUntilDone:NO];
}

/*
 * (c) 2003-2006, 2008 Steve Checkoway
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
