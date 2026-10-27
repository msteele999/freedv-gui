#import <Foundation/Foundation.h>

namespace FreeDVTheme
{
bool SystemUsesDarkAppearance()
{
    // NSApp.effectiveAppearance reflects FreeDV's startup override. System
    // selection needs the desktop preference even before the app restarts.
    @autoreleasepool
    {
        NSString* style = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
        return style != nil && [style caseInsensitiveCompare:@"Dark"] == NSOrderedSame;
    }
}
}
