#import "Haptics.h"

#import <UIKit/UIKit.h>

// N111: UIKit feedback generators live on the main thread; SDL's iOS main loop is there, so this is a direct call.
static void impact(int heavy)
{
    static UIImpactFeedbackGenerator *light;
    static UIImpactFeedbackGenerator *medium;
    if (light == nil) {
        light = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
        medium = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleMedium];
    }
    UIImpactFeedbackGenerator *generator = heavy ? medium : light;
    [generator impactOccurred];
    // Keeps the Taptic Engine awake for the next press (it sleeps again after a few idle seconds).
    [generator prepare];
}

void PartyBoard_iOS_ImpactHaptic(int heavy)
{
    if ([NSThread isMainThread]) {
        impact(heavy);
        return;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        impact(heavy);
    });
}
