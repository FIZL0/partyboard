#include "port/controller_watch.h"

#import <CoreText/CoreText.h>
#import <UIKit/UIKit.h>

// "Tap screen to resume controller" (src/port/tap_resume.c), as a UIView over the whole window: the game's picture and
// RmlUi stop at the safe area (N130), and the dimmed backdrop has to reach the screen's edges. It takes no touches, so
// the tap goes to SDL's view (SDL_EVENT_FINGER_DOWN clears the flag). Drawn by UIKit: nothing reaches the game.

static UIFont *Font(void)
{
    // the RmlUi toasts' font (res/rml/overlay.rcss)
    NSString *path = [NSBundle.mainBundle pathForResource:@"FOT-NewRodin Pro DB" ofType:@"otf" inDirectory:@"res"];
    CGDataProviderRef provider = path != nil ? CGDataProviderCreateWithFilename(path.fileSystemRepresentation) : NULL;
    CGFontRef cg = provider != NULL ? CGFontCreateWithDataProvider(provider) : NULL;
    UIFont *font = cg != NULL ? (__bridge_transfer UIFont *)CTFontCreateWithGraphicsFont(cg, 18, NULL, NULL) : nil;
    if (cg != NULL) {
        CGFontRelease(cg);
    }
    if (provider != NULL) {
        CGDataProviderRelease(provider);
    }
    return font ?: [UIFont boldSystemFontOfSize:18];
}

static UIView *Make(UIWindow *window)
{
    UIView *backdrop = [[UIView alloc] initWithFrame:window.bounds];
    backdrop.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    backdrop.backgroundColor = [UIColor colorWithWhite:0 alpha:0.55];
    backdrop.userInteractionEnabled = NO;

    // toast style: rgba(37, 59, 156) box, 1dp #009dda border, 14dp corners, 18dp x 24dp padding
    UILabel *label = [[UILabel alloc] init];
    label.text = @"Tap screen to resume controller";
    label.font = Font();
    label.textColor = [UIColor colorWithRed:0xE0 / 255.0 green:0xDB / 255.0 blue:0xC8 / 255.0 alpha:1];
    label.textAlignment = NSTextAlignmentCenter;
    label.numberOfLines = 0;
    label.translatesAutoresizingMaskIntoConstraints = NO;

    UIView *box = [[UIView alloc] init];
    box.backgroundColor = [UIColor colorWithRed:37 / 255.0 green:59 / 255.0 blue:156 / 255.0 alpha:1];
    box.layer.borderColor = [UIColor colorWithRed:0 green:0x9d / 255.0 blue:0xda / 255.0 alpha:1].CGColor;
    box.layer.borderWidth = 1;
    box.layer.cornerRadius = 14;
    box.translatesAutoresizingMaskIntoConstraints = NO;
    [box addSubview:label];
    [backdrop addSubview:box];
    [NSLayoutConstraint activateConstraints:@[
        [label.topAnchor constraintEqualToAnchor:box.topAnchor constant:18],
        [label.bottomAnchor constraintEqualToAnchor:box.bottomAnchor constant:-18],
        [label.leadingAnchor constraintEqualToAnchor:box.leadingAnchor constant:24],
        [label.trailingAnchor constraintEqualToAnchor:box.trailingAnchor constant:-24],
        // centred on the window, not the safe area
        [box.centerXAnchor constraintEqualToAnchor:backdrop.centerXAnchor],
        [box.centerYAnchor constraintEqualToAnchor:backdrop.centerYAnchor],
        [box.widthAnchor constraintLessThanOrEqualToAnchor:backdrop.widthAnchor multiplier:0.9],
    ]];
    return backdrop;
}

static void Show(BOOL show)
{
    static UIView *view;
    if (!show) {
        [view removeFromSuperview];
        view = nil;
        return;
    }
    if (view != nil) {
        return;
    }
    UIWindow *window = nil;
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if ([scene isKindOfClass:UIWindowScene.class]) {
            for (UIWindow *w in ((UIWindowScene *)scene).windows) {
                if (w.isKeyWindow || window == nil) {
                    window = w;
                }
            }
        }
    }
    if (window == nil) {
        return;
    }
    view = Make(window);
    [window addSubview:view];
}

void PartyBoard_iOS_TapResumeShow(int show)
{
    static int shown;
    if (show == shown) {
        return;
    }
    shown = show;
    if ([NSThread isMainThread]) {
        Show(show);
        return;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        Show(show);
    });
}
