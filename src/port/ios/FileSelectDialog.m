#include "FileSelectDialog.h"

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <objc/runtime.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>

static void *g_picker_delegate_key = &g_picker_delegate_key;

static void RunOnMainThread(void (^block)(void))
{
    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_sync(dispatch_get_main_queue(), block);
    }
}

static NSError *MakeError(NSString *message)
{
    return [NSError errorWithDomain:@"com.mariopartyrd.partyboard.file-select"
                               code:1
                           userInfo:@{NSLocalizedDescriptionKey: message}];
}

static UIViewController *FindTopViewController(UIViewController *controller)
{
    UIViewController *current = controller;
    while (current.presentedViewController != nil) {
        current = current.presentedViewController;
    }
    return current;
}

static UIViewController *PresenterFromWindow(SDL_Window *window)
{
    if (window == nil) {
        return nil;
    }

    const SDL_PropertiesID props = SDL_GetWindowProperties(window);
    if (props == 0) {
        return nil;
    }

    UIWindow *uiwindow = (__bridge UIWindow *)SDL_GetPointerProperty(
        props, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, NULL);
    if (uiwindow == nil || uiwindow.rootViewController == nil) {
        return nil;
    }

    return FindTopViewController(uiwindow.rootViewController);
}

static NSURL *InitialDirectoryURL(const char *default_location)
{
    if (default_location == NULL || *default_location == '\0') {
        return nil;
    }

    NSString *path = [NSString stringWithUTF8String:default_location];
    NSURL *url = [NSURL fileURLWithPath:path];
    if ([path hasSuffix:@"/"]) {
        return url;
    }

    return [url URLByDeletingLastPathComponent];
}

static NSString *BookmarkKey(NSString *path)
{
    return [@"partyboard.bookmark." stringByAppendingString:path];
}

static void SaveBookmark(NSURL *url)
{
    NSError *error = nil;
    NSData *bookmark = [url bookmarkDataWithOptions:0
                     includingResourceValuesForKeys:nil
                                      relativeToURL:nil
                                              error:&error];
    if (bookmark == nil) {
        NSLog(@"[ShowFileSelect] no bookmark for %@: %@", url.path, error);
        return;
    }
    [[NSUserDefaults standardUserDefaults] setObject:bookmark forKey:BookmarkKey(url.path)];
}

// The app container's path has a per-install UUID that changes on updates and reinstalls: move a saved path under
// .../Data/Application/<UUID>/ onto the current home directory.
static NSString *RebaseContainerPath(NSString *path)
{
    NSString *marker = @"/Data/Application/";
    NSRange at = [path rangeOfString:marker];
    if (at.location == NSNotFound) {
        return nil;
    }
    NSUInteger uuid_start = at.location + at.length;
    NSRange slash = [path rangeOfString:@"/" options:0 range:NSMakeRange(uuid_start, path.length - uuid_start)];
    if (slash.location == NSNotFound) {
        return nil;
    }
    return [NSHomeDirectory() stringByAppendingString:[path substringFromIndex:slash.location]];
}

static bool CopyOut(NSString *path, char *out, size_t out_size)
{
    const char *utf8 = path.fileSystemRepresentation;
    if (out == NULL || out_size == 0 || strlen(utf8) >= out_size) {
        return false;
    }
    memcpy(out, utf8, strlen(utf8) + 1);
    return true;
}

bool PartyBoard_iOS_RestoreFileAccess(const char *path, char *out, size_t out_size)
{
    if (path == NULL || *path == '\0') {
        return false;
    }
    @autoreleasepool {
        NSString *saved = [NSString stringWithUTF8String:path];
        NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
        NSData *bookmark = [defaults dataForKey:BookmarkKey(saved)];
        if (bookmark != nil) {
            BOOL stale = NO;
            NSError *error = nil;
            NSURL *url = [NSURL URLByResolvingBookmarkData:bookmark
                                                   options:0
                                             relativeToURL:nil
                                       bookmarkDataIsStale:&stale
                                                     error:&error];
            if (url != nil) {
                [url startAccessingSecurityScopedResource];
                if (stale || ![url.path isEqualToString:saved]) {
                    [defaults removeObjectForKey:BookmarkKey(saved)];
                    SaveBookmark(url);
                }
                return CopyOut(url.path, out, out_size);
            }
            NSLog(@"[RestoreFileAccess] bookmark for %@ did not resolve: %@", saved, error);
        }
        if ([[NSFileManager defaultManager] isReadableFileAtPath:saved]) {
            return CopyOut(saved, out, out_size);
        }
        NSString *rebased = RebaseContainerPath(saved);
        if (rebased != nil && [[NSFileManager defaultManager] isReadableFileAtPath:rebased]) {
            return CopyOut(rebased, out, out_size);
        }
        return false;
    }
}

@interface DocumentPickerDelegate : NSObject <UIDocumentPickerDelegate>

@property(nonatomic, assign) IOSFileCallback callback;
@property(nonatomic, assign) void *userdata;

@end

@implementation DocumentPickerDelegate

- (void)finishWithPath:(const char *)path error:(const char *)error {
    if (self.callback != NULL) {
        self.callback(self.userdata, path, error);
    }
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller
didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls
{
    NSURL *url = urls.firstObject;
    if (url == nil) {
        [self finishWithPath:NULL error:NULL];
        return;
    }

    // Opened in place (asCopy:NO, N100): keep the security scope for the whole run, as the disc stays open, and
    // bookmark it so the next launch can reopen the same file.
    [url startAccessingSecurityScopedResource];
    SaveBookmark(url);
    [self finishWithPath:url.path.UTF8String error:NULL];
    (void)controller;
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller
{
    [self finishWithPath:NULL error:NULL];
    (void)controller;
}

@end

void PartyBoard_iOS_ShowFileSelect(IOSFileCallback callback, void *userdata,
                               SDL_Window *window,
                               const SDL_DialogFileFilter *filters, int nfilters,
                               const char *default_location,
                               bool allow_many)
{
    RunOnMainThread(^{
        @autoreleasepool {
            UIViewController *presenter = PresenterFromWindow(window);
            if (presenter == nil) {
                callback(userdata, NULL, "Failed to find an iOS view controller for the file picker.");
                return;
            }

            NSLog(@"[ShowFileSelect] presenting picker from %@", NSStringFromClass([presenter class]));

            UIDocumentPickerViewController *picker =
                [[UIDocumentPickerViewController alloc]
                    initForOpeningContentTypes:@[ UTTypeItem ]
                                         asCopy:NO];
            picker.allowsMultipleSelection = allow_many ? YES : NO;
            picker.shouldShowFileExtensions = YES;

            NSURL *directory_url = InitialDirectoryURL(default_location);
            if (directory_url != nil) {
                picker.directoryURL = directory_url;
            }

            DocumentPickerDelegate *delegate = [DocumentPickerDelegate new];
            delegate.callback = callback;
            delegate.userdata = userdata;
            picker.delegate = delegate;
            objc_setAssociatedObject(picker, g_picker_delegate_key, delegate,
                                     OBJC_ASSOCIATION_RETAIN_NONATOMIC);

            [presenter presentViewController:picker animated:YES completion:nil];
            (void)filters;
            (void)nfilters;
        }
    });
}
