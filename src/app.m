#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#include "aorus-led.h"

static NSString *configFilePath(void) {
    NSString *dir = [NSHomeDirectory() stringByAppendingPathComponent:@".config/aorus-led"];
    [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    return [dir stringByAppendingPathComponent:@"config.json"];
}

static NSString *launchAgentPath(void) {
    NSString *dir = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/LaunchAgents"];
    [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    return [dir stringByAppendingPathComponent:@"com.aorus.led.plist"];
}

static NSString *cliBinaryPath(void) {
    NSString *bundled = [[[NSBundle mainBundle] bundlePath] stringByAppendingPathComponent:@"Contents/MacOS/aorus-led"];
    if ([[NSFileManager defaultManager] isExecutableFileAtPath:bundled]) {
        return bundled;
    }
    return [[NSFileManager defaultManager].currentDirectoryPath stringByAppendingPathComponent:@"aorus-led"];
}

@interface AppDelegate : NSObject <NSApplicationDelegate, WKScriptMessageHandler>
@property (strong) NSWindow *window;
@property (strong) WKWebView *webView;
@property (strong) dispatch_queue_t hidQueue;
@property (atomic, assign) uint64_t previewSeq;
@end

@implementation AppDelegate

- (instancetype)init {
    self = [super init];
    if (self) {
        _hidQueue = dispatch_queue_create("com.aorus.hid.serial", DISPATCH_QUEUE_SERIAL);
        _previewSeq = 0;
    }
    return self;
}

- (NSDictionary *)defaultConfig {
    NSDictionary *defZone = @{@"r": @255, @"g": @170, @"b": @0, @"mode": @1, @"brightness": @10, @"speed": @5};
    NSMutableArray *zones = [NSMutableArray array];
    for (int i = 0; i < ITE_ZONE_COUNT; i++) [zones addObject:defZone];
    return @{
        @"syncMode": @NO,
        @"case": defZone,
        @"mbZones": zones
    };
}

- (NSDictionary *)loadConfig {
    NSData *data = [NSData dataWithContentsOfFile:configFilePath()];
    if (data) {
        NSDictionary *dict = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
        if ([dict isKindOfClass:[NSDictionary class]] && dict[@"case"] && dict[@"mbZones"]) {
            return dict;
        }
    }
    return [self defaultConfig];
}

- (void)saveConfig:(NSDictionary *)cfg {
    NSData *data = [NSJSONSerialization dataWithJSONObject:cfg options:NSJSONWritingPrettyPrinted error:nil];
    if (data) [data writeToFile:configFilePath() atomically:YES];
}

- (BOOL)isAutoStartEnabled {
    return [[NSFileManager defaultManager] fileExistsAtPath:launchAgentPath()];
}

- (void)setAutoStart:(BOOL)enabled {
    NSString *plistPath = launchAgentPath();
    if (enabled) {
        NSDictionary *plist = @{
            @"Label": @"com.aorus.led",
            @"ProgramArguments": @[cliBinaryPath(), @"--apply-saved"],
            @"RunAtLoad": @YES,
            @"StandardOutPath": @"/tmp/aorus-led.log",
            @"StandardErrorPath": @"/tmp/aorus-led.log"
        };
        [plist writeToFile:plistPath atomically:YES];
    } else {
        [[NSFileManager defaultManager] removeItemAtPath:plistPath error:nil];
    }
}

- (void)evalJSWithJSON:(NSString *)funcName payload:(NSDictionary *)payload {
    NSData *json = [NSJSONSerialization dataWithJSONObject:payload options:0 error:nil];
    NSString *jsonStr = [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding];
    NSString *js = [NSString stringWithFormat:@"%@(%@);", funcName, jsonStr];
    [self.webView evaluateJavaScript:js completionHandler:nil];
}

- (void)sendInitialState {
    uint8_t maj = 0, min = 0;
    int cpid = 0;
    const char *cname = NULL;
    BOOL caseOk = (aorus_get_info(&maj, &min, &cpid, &cname) == 0);
    NSString *caseName = (caseOk && cname) ? [NSString stringWithUTF8String:cname] : @"AORUS Case";

    char prod[32] = {0};
    uint32_t mbFw = 0;
    int mpid = 0;
    BOOL mbOk = (ite_get_info(prod, sizeof(prod), &mbFw, &mpid) == 0);
    NSString *mbName = mbOk ? [NSString stringWithUTF8String:prod] : @"";

    NSDictionary *payload = @{
        @"caseConnected": @(caseOk),
        @"caseName": caseName,
        @"caseFw": [NSString stringWithFormat:@"%d.%d", maj, min],
        @"mbConnected": @(mbOk),
        @"mbName": mbName,
        @"autoStart": @([self isAutoStartEnabled]),
        @"config": [self loadConfig]
    };
    [self evalJSWithJSON:@"window.onNativeState" payload:payload];
}

- (void)userContentController:(WKUserContentController *)userContentController
      didReceiveScriptMessage:(WKScriptMessage *)message {
    if (![message.body isKindOfClass:[NSDictionary class]]) return;
    NSDictionary *body = (NSDictionary *)message.body;
    NSString *action = body[@"action"];

    if ([action isEqualToString:@"init"]) {
        [self sendInitialState];
    } else if ([action isEqualToString:@"setAutoStart"]) {
        [self setAutoStart:[body[@"enabled"] boolValue]];
    } else if ([action isEqualToString:@"apply"] || [action isEqualToString:@"preview"]) {
        BOOL shouldSave = [action isEqualToString:@"apply"];
        NSDictionary *cfg = body[@"config"];
        NSString *target = body[@"target"] ?: @"all";
        int zoneMask = [body[@"zoneMask"] intValue];
        if (zoneMask <= 0) zoneMask = 0x7F;

        if (shouldSave && [cfg isKindOfClass:[NSDictionary class]]) {
            [self saveConfig:cfg];
        }

        NSDictionary *caseDict = cfg[@"case"];
        NSArray *mbArr = cfg[@"mbZones"];
        uint64_t mySeq = ++self.previewSeq;

        dispatch_async(self.hidQueue, ^{
            if (!shouldSave && mySeq != self.previewSeq) return;

            int rcCase = 0, rcMb = 0;
            BOOL didCase = NO, didMb = NO;

            if (([target isEqualToString:@"case"] || [target isEqualToString:@"all"]) &&
                [caseDict isKindOfClass:[NSDictionary class]]) {
                didCase = YES;
                uint8_t r = (uint8_t)[caseDict[@"r"] intValue];
                uint8_t g = (uint8_t)[caseDict[@"g"] intValue];
                uint8_t b = (uint8_t)[caseDict[@"b"] intValue];
                int mode = [caseDict[@"mode"] intValue];
                if (mode == 6) mode = 5;
                int bri = [caseDict[@"brightness"] intValue];
                int spd = [caseDict[@"speed"] intValue];
                rcCase = aorus_apply(r, g, b, mode, bri, spd, shouldSave ? 1 : 0);
            }

            if (([target isEqualToString:@"mb"] || [target isEqualToString:@"all"]) &&
                [mbArr isKindOfClass:[NSArray class]]) {
                didMb = YES;
                MbZoneConfig zones[ITE_ZONE_COUNT];
                for (int i = 0; i < ITE_ZONE_COUNT; i++) {
                    NSDictionary *zd = (i < (int)mbArr.count) ? mbArr[i] : caseDict;
                    zones[i].r = (uint8_t)[zd[@"r"] intValue];
                    zones[i].g = (uint8_t)[zd[@"g"] intValue];
                    zones[i].b = (uint8_t)[zd[@"b"] intValue];
                    zones[i].mode = [zd[@"mode"] intValue];
                    zones[i].brightness = [zd[@"brightness"] intValue];
                    zones[i].speed = [zd[@"speed"] intValue];
                }
                rcMb = ite_apply_zones(zones, (uint8_t)zoneMask, shouldSave ? 1 : 0);
            }

            BOOL ok = (!didCase || rcCase == 0) && (!didMb || rcMb == 0);
            dispatch_async(dispatch_get_main_queue(), ^{
                [self evalJSWithJSON:@"window.onApplyResult" payload:@{
                    @"ok": @(ok),
                    @"saved": @(shouldSave),
                    @"target": target,
                    @"rcCase": @(rcCase),
                    @"rcMb": @(rcMb)
                }];
            });
        });
    }
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    NSRect frame = NSMakeRect(0, 0, 960, 610);
    NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                              NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskFullSizeContentView;
    self.window = [[NSWindow alloc] initWithContentRect:frame
                                              styleMask:style
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    self.window.title = @"AORUS RGB Fusion";
    self.window.titleVisibility = NSWindowTitleHidden;
    self.window.titlebarAppearsTransparent = YES;
    self.window.movableByWindowBackground = YES;
    self.window.backgroundColor = [NSColor colorWithRed:0.043 green:0.043 blue:0.051 alpha:1.0];
    [self.window center];

    WKWebViewConfiguration *cfg = [[WKWebViewConfiguration alloc] init];
    [cfg.userContentController addScriptMessageHandler:self name:@"aorus"];

    self.webView = [[WKWebView alloc] initWithFrame:self.window.contentView.bounds configuration:cfg];
    self.webView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [self.webView setValue:@NO forKey:@"drawsBackground"];
    [self.window.contentView addSubview:self.webView];

    NSString *htmlPath = [[NSBundle mainBundle] pathForResource:@"ui" ofType:@"html"];
    if (!htmlPath) {
        htmlPath = [[NSFileManager defaultManager].currentDirectoryPath stringByAppendingPathComponent:@"src/ui.html"];
    }
    NSURL *fileURL = [NSURL fileURLWithPath:htmlPath];
    [self.webView loadFileURL:fileURL allowingReadAccessToURL:[fileURL URLByDeletingLastPathComponent]];

    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    return YES;
}

@end

int main(void) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        AppDelegate *delegate = [[AppDelegate alloc] init];
        [app setDelegate:delegate];
        [app run];
    }
    return 0;
}
