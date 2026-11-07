#include "ArchHooks_MacOSX.h"
#include "ProductInfo.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "SpecialFiles.h"
#include "archutils/Unix/CrashHandler.h"
#include "archutils/Unix/SignalHandler.h"
#include "global.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <ApplicationServices/ApplicationServices.h>
#include <CoreServices/CoreServices.h>
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <sys/types.h>
extern "C" {
#include <IOKit/graphics/IOGraphicsLib.h>
#include <mach/mach_time.h>
}
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/network/IOEthernetController.h>
#include <IOKit/network/IOEthernetInterface.h>
#include <IOKit/network/IONetworkInterface.h>

#import <AppKit/NSScreen.h>
#import <Foundation/Foundation.h>

static bool IsFatalSignal(int signal) {
  switch (signal) {
    case SIGINT:
    case SIGTERM:
    case SIGHUP:
      return false;
    default:
      return true;
  }
}

static bool DoCleanShutdown(int signal, siginfo_t* si, const ucontext_t* uc) {
  if (IsFatalSignal(signal)) {
    return false;
  }

  // ^C.
  ArchHooks::SetUserQuit();
  return true;
}

static bool DoCrashSignalHandler(int signal, siginfo_t* si, const ucontext_t* uc) {
  // Don't dump a debug file if the user just hit ^C.
  if (!IsFatalSignal(signal)) {
    return true;
  }

  CrashHandler::CrashSignalHandler(signal, si, uc);
  return true;  // Unreached
}

static bool DoEmergencyShutdown(int signal, siginfo_t* si, const ucontext_t* us) {
  if (IsFatalSignal(signal)) {
    _exit(1);  // We ran the crash handler already
  }
  return false;
}

void ArchHooks_MacOSX::Init() {
  // First, handle non-fatal termination signals.
  SignalHandler::OnClose(DoCleanShutdown);
  CrashHandler::CrashHandlerHandleArgs(g_argc, g_argv);
  CrashHandler::InitializeCrashHandler();
  SignalHandler::OnClose(DoCrashSignalHandler);
  SignalHandler::OnClose(DoEmergencyShutdown);

  // Now that the crash handler is set up, disable crash reporter.
  // Breaks gdb
  // task_set_exception_ports( mach_task_self(), EXC_MASK_ALL, MACH_PORT_NULL, EXCEPTION_DEFAULT, 0
  // );

  // CF*Copy* functions' return values need to be released, CF*Get* functions' do not.
  CFStringRef key = CFSTR("ApplicationBundlePath");

  CFBundleRef bundle = CFBundleGetMainBundle();
  CFStringRef app_id = CFBundleGetIdentifier(bundle);
  if (app_id == nil) {
    // We were probably launched through a symlink. Don't bother hunting down the real path.
    return;
  }
  CFStringRef version =
      CFStringRef(CFBundleGetValueForInfoDictionaryKey(bundle, kCFBundleVersionKey));
  CFPropertyListRef old = CFPreferencesCopyAppValue(key, app_id);
  CFURLRef path = CFBundleCopyBundleURL(bundle);
  CFPropertyListRef value = CFURLCopyFileSystemPath(path, kCFURLPOSIXPathStyle);
  CFMutableDictionaryRef new_dict = nil;

  if (old && CFGetTypeID(old) != CFDictionaryGetTypeID()) {
    CFRelease(old);
    old = nil;
  }

  if (!old) {
    new_dict = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionaryAddValue(new_dict, version, value);
  } else {
    CFTypeRef old_value;
    CFDictionaryRef dict = CFDictionaryRef(old);

    if (!CFDictionaryGetValueIfPresent(dict, version, &old_value) || !CFEqual(old_value, value)) {
      // The value is either not present or it is but it is different
      new_dict = CFDictionaryCreateMutableCopy(kCFAllocatorDefault, 0, dict);
      CFDictionarySetValue(new_dict, version, value);
    }
    CFRelease(old);
  }

  if (new_dict) {
    CFPreferencesSetAppValue(key, new_dict, app_id);
    if (!CFPreferencesAppSynchronize(app_id)) {
      LOG->Warn("Failed to record the run path.");
    }
    CFRelease(new_dict);
  }
  CFRelease(value);
  CFRelease(path);
}

std::string ArchHooks_MacOSX::GetArchName() const {
#if defined(__x86_64__)
  return "macOS (x86_64)";
#elif defined(__aarch64__) || defined(__arm64__)
  return "macOS (arm64)";
#else
#error What arch?
#endif
}

void ArchHooks_MacOSX::DumpDebugInfo() {
  // Get system version (like 10.x.x)
  std::string system_version;
  {
    // http://stackoverflow.com/a/891336
    NSDictionary* version = [NSDictionary
        dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"];
    NSString* product_version = version[@"ProductVersion"];
    system_version = ssprintf(
        "macOS %s", [product_version cStringUsingEncoding:[NSString defaultCStringEncoding]]);
  }

  size_t size;
#define GET_PARAM(name, var) (size = sizeof(var), sysctlbyname(name, &var, &size, nil, 0))
  // Get memory
  float f_ram;
  char ram_power;
  {
    uint64_t i_ram = 0;
    GET_PARAM("hw.memsize", i_ram);

    f_ram = float(double(i_ram) / 1073741824.0);
    ram_power = 'G';
  }

  // Get processor information
  int max_cpus = 0;
  int cpus = 0;
  float f_freq;
  char freq_power;
  std::string s_model("Unknown");
  do {
    char sz_model[128];
    uint64_t i_freq;

    GET_PARAM("hw.logicalcpu_max", max_cpus);
    GET_PARAM("hw.logicalcpu", cpus);
    GET_PARAM("hw.cpufrequency", i_freq);

    f_freq = float(double(i_freq) / 1000000000.0);
    freq_power = 'G';

    if (GET_PARAM("hw.model", sz_model) != 0) {
      break;
    }

    s_model = sz_model;

    NSURL* url =
        [NSURL fileURLWithPath:@"//System/Library/PrivateFrameworks/ServerInformation.framework/"
                               @"Versions/A/Resources/en.lproj/SIMachineAttributes.plist"];
    NSDictionary* machine_attributes = [NSDictionary dictionaryWithContentsOfURL:url];
    if (machine_attributes == nil) {
      break;
    }

    NSString* key = [NSString stringWithUTF8String:sz_model];
    NSString* val = machine_attributes[key][@"_LOCALIZABLE_"][@"marketingModel"];
    if (val != nil) {
      s_model = [val UTF8String];
    }
  } while (false);
#undef GET_PARAM

  // Send all of the information to the log
  LOG->Info("Model: %s (%d/%d)", s_model.c_str(), cpus, max_cpus);
  LOG->Info("Clock speed %.2f %cHz", f_freq, freq_power);
  LOG->Info("%s", system_version.c_str());
  LOG->Info("Memory: %.2f %cB", f_ram, ram_power);
}

std::string ArchHooks::GetPreferredLanguage() {
  CFStringRef app = kCFPreferencesCurrentApplication;
  CFTypeRef t = CFPreferencesCopyAppValue(CFSTR("AppleLanguages"), app);
  std::string ret = "en";

  if (t == nil) {
    return ret;
  }
  if (CFGetTypeID(t) != CFArrayGetTypeID()) {
    CFRelease(t);
    return ret;
  }

  CFArrayRef languages = CFArrayRef(t);
  CFStringRef lang;

  if (CFArrayGetCount(languages) > 0 &&
      (lang = (CFStringRef)CFArrayGetValueAtIndex(languages, 0)) != nil) {
    // MacRoman agrees with ASCII in the low-order 7 bits.
    const char* str = CFStringGetCStringPtr(lang, kCFStringEncodingMacRoman);
    if (str) {
      ret = std::string(str, 2);
      if (ret == "zh") {
        ret = std::string(str, 7);
        ret[2] = '-';
      }
    } else {
      LOG->Warn("Unable to determine system language. Using English.");
    }
  }

  CFRelease(languages);
  return ret;
}

int64_t ArchHooks::GetSystemTimeInMicroseconds() {
  // http://developer.apple.com/qa/qa2004/qa1398.html
  static double factor = 0.0;

  if (unlikely(factor == 0.0)) {
    mach_timebase_info_data_t time_base;

    mach_timebase_info(&time_base);
    factor = time_base.numer / (1000.0 * time_base.denom);
  }
  return int64_t(mach_absolute_time() * factor);
}

#include "RageFileManager.h"

void MountDirectories(const std::string& base_dir) {
  const std::vector<std::string> kMacDirectoryStructureItgm = {
      "/Announcers",
      "/BGAnimations",
      "/BackgroundEffects",
      "/BackgroundTransitions",
      "/Cache",
      "/CDTitles",
      "/Characters",
      "/Courses",
      "/Downloads",
      "/Logs",
      "/NoteSkins",
      "/Packages",
      "/Save",
      "/Screenshots",
      "/Songs",
      "/RandomMovies",
      "/Themes"};

  for (const std::string& dir : kMacDirectoryStructureItgm) {
    FILEMAN->Mount("dir", base_dir + dir, dir);
  }
}

void ArchHooks::MountInitialFilesystems(const std::string& dir_of_executable) {
  FILEMAN->Mount("dirro", dir_of_executable, "/");

  bool portable = DoesFileExist("/Portable.ini");

  NSString* resource_path = [[NSBundle mainBundle] resourcePath];
  if (resource_path) {
    const char* resource_path_utf8_string = [resource_path UTF8String];
    FILEMAN->Mount("dirro", ssprintf("%s/Announcers", resource_path_utf8_string), "/Announcers");
    FILEMAN->Mount(
        "dirro", ssprintf("%s/BGAnimations", resource_path_utf8_string), "/BGAnimations");
    FILEMAN->Mount(
        "dirro", ssprintf("%s/BackgroundEffects", resource_path_utf8_string), "/BackgroundEffects");
    FILEMAN->Mount(
        "dirro", ssprintf("%s/BackgroundTransitions", resource_path_utf8_string),
        "/BackgroundTransitions");
    FILEMAN->Mount("dirro", ssprintf("%s/CDTitles", resource_path_utf8_string), "/CDTitles");
    FILEMAN->Mount("dirro", ssprintf("%s/Characters", resource_path_utf8_string), "/Characters");
    FILEMAN->Mount("dirro", ssprintf("%s/Courses", resource_path_utf8_string), "/Courses");
    FILEMAN->Mount("dirro", ssprintf("%s/NoteSkins", resource_path_utf8_string), "/NoteSkins");
    FILEMAN->Mount("dirro", ssprintf("%s/Packages", resource_path_utf8_string), "/Packages");
    FILEMAN->Mount("dirro", ssprintf("%s/Songs", resource_path_utf8_string), "/Songs");
    FILEMAN->Mount(
        "dirro", ssprintf("%s/RandomMovies", resource_path_utf8_string), "/RandomMovies");
    FILEMAN->Mount("dirro", ssprintf("%s/Themes", resource_path_utf8_string), "/Themes");
    FILEMAN->Mount("dirro", ssprintf("%s/Data", resource_path_utf8_string), "/Data");
  }

  CFURLRef data_url =
      CFBundleCopyResourceURL(CFBundleGetMainBundle(), CFSTR("StepMania"), CFSTR("smzip"), nil);
  if (data_url) {
    char dir[PATH_MAX];

    CFStringRef data_path = CFURLCopyFileSystemPath(data_url, kCFURLPOSIXPathStyle);
    CFStringGetCString(data_path, dir, PATH_MAX, kCFStringEncodingUTF8);

    if (strncmp(dir_of_executable.c_str(), dir, dir_of_executable.length()) == 0) {
      FILEMAN->Mount("zip", dir + dir_of_executable.length(), "/");
    }
    CFRelease(data_path);
    CFRelease(data_url);
  }

  if (portable) {
    MountDirectories(dir_of_executable);
  }
}

static std::string PathForDirectory(NSSearchPathDirectory directory) {
  NSFileManager* file_manager = [NSFileManager defaultManager];
  NSURL* url = [file_manager URLForDirectory:directory
                                    inDomain:NSUserDomainMask
                           appropriateForURL:nil
                                      create:NO
                                       error:nil];
  if (url == nil) {
    FAIL_M("URLForDirectory() failed.");
  }

  return [url fileSystemRepresentation];
}

static void MigrateLegacyDirectory(const std::string& old_path, const std::string& new_path) {
  NSFileManager* file_manager = [NSFileManager defaultManager];
  NSString* source = [NSString stringWithUTF8String:old_path.c_str()];
  NSString* destination = [NSString stringWithUTF8String:new_path.c_str()];

  if (source == nil || destination == nil) {
    return;
  }

  BOOL source_is_directory = NO;
  if (![file_manager fileExistsAtPath:source isDirectory:&source_is_directory] ||
      !source_is_directory) {
    return;
  }

  NSString* destination_parent = [destination stringByDeletingLastPathComponent];
  NSError* error = nil;
  if (![file_manager createDirectoryAtPath:destination_parent
               withIntermediateDirectories:YES
                                attributes:nil
                                     error:&error]) {
    return;
  }

  BOOL destination_exists = [file_manager fileExistsAtPath:destination];
  BOOL destination_is_directory = NO;
  if (destination_exists && ![file_manager fileExistsAtPath:destination
                                                isDirectory:&destination_is_directory]) {
    return;
  }

  // Fast path: if destination does not exist, a full move preserves directory contents.
  if (!destination_exists) {
    error = nil;
    if ([file_manager moveItemAtPath:source toPath:destination error:&error]) {
      return;
    }
    return;
  }

  if (!destination_is_directory) {
    return;
  }

  // Merge source into destination so migration still works when startup created folders already.
  NSArray<NSString*>* source_entries = [file_manager contentsOfDirectoryAtPath:source error:&error];
  if (source_entries == nil) {
    return;
  }

  // Iterate through the source directory, moving each file/folder individually.
  // If the destination entry already exists, recurse if it's a directory, otherwise skip (to
  // prevent overwriting).
  for (NSString* entry in source_entries) {
    NSString* source_entry_path = [source stringByAppendingPathComponent:entry];
    NSString* destination_entry_path = [destination stringByAppendingPathComponent:entry];

    BOOL destination_entry_exists = [file_manager fileExistsAtPath:destination_entry_path];
    if (!destination_entry_exists) {
      [file_manager moveItemAtPath:source_entry_path toPath:destination_entry_path error:nil];
      continue;
    }

    BOOL source_entry_is_directory = NO;
    BOOL destination_entry_is_directory = NO;
    BOOL source_entry_exists = [file_manager fileExistsAtPath:source_entry_path
                                                  isDirectory:&source_entry_is_directory];
    [file_manager fileExistsAtPath:destination_entry_path
                       isDirectory:&destination_entry_is_directory];

    if (source_entry_exists && source_entry_is_directory && destination_entry_is_directory) {
      std::string source_child = [source_entry_path fileSystemRepresentation];
      std::string destination_child = [destination_entry_path fileSystemRepresentation];
      MigrateLegacyDirectory(source_child, destination_child);
    }
  }

  // Clean up if nothing is left after merge.
  [file_manager removeItemAtPath:source error:nil];
}

static void RemoveLegacyDirectory(const std::string& old_path) {
  NSFileManager* file_manager = [NSFileManager defaultManager];
  NSString* source = [NSString stringWithUTF8String:old_path.c_str()];
  NSError* error = nil;

  if (source == nil) {
    return;
  }

  BOOL source_is_directory = NO;
  if (![file_manager fileExistsAtPath:source isDirectory:&source_is_directory] ||
      !source_is_directory) {
    return;
  }

  [file_manager removeItemAtPath:source error:&error];
}

void ArchHooks::MountUserFilesystems(const std::string& dir_of_executable) {
  std::string library_dir = PathForDirectory(NSLibraryDirectory);
  std::string caches_dir = PathForDirectory(NSCachesDirectory);
  std::string app_support_dir = PathForDirectory(NSApplicationSupportDirectory);
  std::string app_support_path = ssprintf("%s/" PRODUCT_ID, app_support_dir.c_str());

  // Do a best effort to migrate the save folder and screenshots.
  MigrateLegacyDirectory(library_dir + "/Preferences/" PRODUCT_ID, app_support_path + "/Save");
  MigrateLegacyDirectory(
      caches_dir + "/" PRODUCT_ID " Screenshots", app_support_path + "/Screenshots");
  // It's fine to just delete the old Cache directory, and just let the game regenerate it on boot.
  RemoveLegacyDirectory(caches_dir + "/" PRODUCT_ID);
  // It's fine to just delete the old Logs directory, as the contents of the data will get
  // regenerated on boot. In that case, we can just avoid the complexity of migrating directories.
  RemoveLegacyDirectory(library_dir + "/Logs/" PRODUCT_ID);

  MountDirectories(app_support_path);
}

static inline int GetIntValue(CFTypeRef r) {
  int ret;

  if (!r || CFGetTypeID(r) != CFNumberGetTypeID() ||
      !CFNumberGetValue(CFNumberRef(r), kCFNumberIntType, &ret)) {
    return 0;
  }
  return ret;
}

/*
 * (c) 2003-2006 Steve Checkoway
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
