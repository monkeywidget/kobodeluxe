/*
 * Bridges SDL2's legacy, Scene-unaware UIKit backend onto iOS's now-mandatory
 * UIScene lifecycle.
 *
 * SDL2's own iOS code (src/video/uikit/SDL_uikitwindow.m, see upstream
 * at .src/SDL2-2.32.10/) creates its UIWindow with a plain -initWithFrame:
 * and calls -makeKeyAndVisible directly, exactly as every iOS app did before
 * Scenes existed. It never assigns the window's .windowScene property at
 * all, because the Scene lifecycle is still a no-op aside from one
 * deprecation warning as of SDL 2.32.10. On this iOS version that's no
 * longer just a warning: UIKit actively traps at launch
 * (UIApplicationEvaluateRuntimeIssueForNoSceneLifecycleAdoption, SIGTRAP) if
 * an app's Info.plist declares no scene adoption at all, and if it declares
 * one via a manifest with no code behind it, the app launches but the
 * window is simply never attached to a scene and nothing is ever drawn --
 * both were confirmed by hand against this exact build (see
 * xkobo-port-notes/docs/spec/ipados-port.md).
 *
 * This is not an SDL bug to work around locally so much as a real gap in
 * SDL 2.x: per the SDL developers themselves (discourse.libsdl.org, "SDL2 on
 * iOS: UIKit scene-based life cycle"), SDL2 is in maintenance mode and is
 * not expected to ever adopt Scenes; even SDL3 doesn't yet. The fix here --
 * swizzling -[UIWindow makeKeyAndVisible] to attach whatever UIWindowScene
 * this delegate captured, before the original implementation runs -- is the
 * same technique independently arrived at and verified working by at least
 * one other project bridging a legacy fixed-window engine onto this exact
 * iOS version (marmikshah/bevy_ios_toolkit#42), adapted here from Rust/objc2
 * to plain Objective-C for SDL2's benefit. It is deliberately generic: it
 * does not depend on understanding SDL's internal call ordering between
 * UIApplicationMain, the legacy UIApplicationDelegate SDL still uses, and
 * SDL_main -- whichever UIWindow is made key-and-visible first simply gets
 * this scene attached.
 */
#import "SceneDelegate.h"
#import <objc/runtime.h>

static UIWindowScene *capturedScene = nil;

@implementation SceneDelegate

- (void)scene:(UIScene *)scene
	willConnectToSession:(UISceneSession *)session
	options:(UISceneConnectionOptions *)connectionOptions
{
	if ([scene isKindOfClass:[UIWindowScene class]]) {
		capturedScene = (UIWindowScene *)scene;
	}
}

@end

@interface UIWindow (KoboDeluxeSceneAttach)
- (void)koboDeluxe_makeKeyAndVisible;
@end

@implementation UIWindow (KoboDeluxeSceneAttach)

- (void)koboDeluxe_makeKeyAndVisible
{
	if (self.windowScene == nil && capturedScene != nil) {
		self.windowScene = capturedScene;
	}
	/* Calls through to the original -makeKeyAndVisible: the swizzle below
	 * swaps the two implementations, so despite the name this invokes
	 * UIWindow's real, original implementation. */
	[self koboDeluxe_makeKeyAndVisible];
}

+ (void)load
{
	/* +load runs at image load time, before UIApplicationMain and
	 * therefore before anything can call -makeKeyAndVisible -- this is
	 * the "register early" requirement the bevy_ios_toolkit writeup
	 * flagged as important. */
	Method original = class_getInstanceMethod(self, @selector(makeKeyAndVisible));
	Method replacement = class_getInstanceMethod(self, @selector(koboDeluxe_makeKeyAndVisible));
	method_exchangeImplementations(original, replacement);
}

@end
