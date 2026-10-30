#include "port/controller_watch.h"

#import <Foundation/Foundation.h>
#import <GameController/GameController.h>
#import <UIKit/UIKit.h>

// N143: Apple's side of a controller connecting, for partyboard.log and src/port/controller_watch.cpp. SDL's MFi
// driver observes the same notifications (SDL_mfijoystick.m, also queue:nil); these lines show whether iOS sent one
// when SDL then added nothing.

static void Note(NSString *what, GCController *controller)
{
    NSString *name = controller.vendorName ?: controller.productCategory ?: @"unknown";
    NSString *profile = controller.extendedGamepad != nil ? @"extended" : controller.microGamepad != nil ? @"micro" : @"none";
    NSString *line = [NSString stringWithFormat:@"iOS: GameController %@ '%@' (profile %@, %lu connected, %@ thread)", what,
                                                name, profile, (unsigned long)GCController.controllers.count,
                                                NSThread.isMainThread ? @"main" : @"other"];
    port_controller_watch_note(line.UTF8String);
}

void PartyBoard_iOS_ControllerWatchStart(void)
{
    static id connectObserver;
    static id disconnectObserver;
    if (connectObserver != nil) {
        return;
    }
    NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
    connectObserver = [center addObserverForName:GCControllerDidConnectNotification
                                          object:nil
                                           queue:nil
                                      usingBlock:^(NSNotification *note) {
                                        Note(@"connected", note.object);
                                      }];
    disconnectObserver = [center addObserverForName:GCControllerDidDisconnectNotification
                                             object:nil
                                              queue:nil
                                         usingBlock:^(NSNotification *note) {
                                           Note(@"disconnected", note.object);
                                         }];
    for (GCController *controller in GCController.controllers) {
        Note(@"present at start", controller);
    }

    // A screenshot gives event focus to its preview; GameController then reads every pad as idle until a touch. Each
    // notification is logged so a device log shows which ones a screenshot sends (src/port/tap_resume.c).
    static BOOL resigned;
    void (^focus)(NSString *, BOOL) = ^(NSString *what, BOOL lost) {
        NSUInteger count = GCController.controllers.count;
        port_controller_watch_note([NSString stringWithFormat:@"iOS: %@ (%lu controller(s))", what, (unsigned long)count].UTF8String);
        if (lost) {
            port_tap_resume_focus_lost(what.UTF8String, (int)count);
        }
    };
    NSOperationQueue *main = NSOperationQueue.mainQueue;
    [center addObserverForName:UIApplicationUserDidTakeScreenshotNotification object:nil queue:main
                    usingBlock:^(NSNotification *note) {
                      focus(@"UIApplicationUserDidTakeScreenshotNotification", YES);
                    }];
    [center addObserverForName:UIApplicationWillResignActiveNotification object:nil queue:main
                    usingBlock:^(NSNotification *note) {
                      resigned = YES;
                      focus(@"UIApplicationWillResignActiveNotification", NO);
                    }];
    [center addObserverForName:UIApplicationDidBecomeActiveNotification object:nil queue:main
                    usingBlock:^(NSNotification *note) {
                      focus(@"UIApplicationDidBecomeActiveNotification", resigned);
                      resigned = NO;
                    }];
}

int PartyBoard_iOS_ControllerCount(void)
{
    return (int)GCController.controllers.count;
}
