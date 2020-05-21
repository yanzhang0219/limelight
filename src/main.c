#include <Carbon/Carbon.h>
#include <signal.h>

//
// PRIVATE CORECRAPHICS / SKYLIGHT FUNCTION DECLARATIONS
//

extern void NSApplicationLoad(void);
extern AXError _AXUIElementGetWindow(AXUIElementRef ref, uint32_t *wid);
extern int SLSMainConnectionID(void);
extern CGError SLSGetWindowBounds(int cid, uint32_t wid, CGRect *frame);
extern OSStatus _SLPSGetFrontProcess(ProcessSerialNumber *psn);
extern CGError SLSDisableUpdate(int cid);
extern CGError SLSReenableUpdate(int cid);
extern CGError SLSNewWindow(int cid, int type, float x, float y, CFTypeRef region, uint32_t *wid);
extern CGError SLSReleaseWindow(int cid, uint32_t wid);
extern CGError SLSSetWindowTags(int cid, uint32_t wid, uint32_t tags[2], int tag_size);
extern CGError SLSSetWindowShape(int cid, uint32_t wid, float x_offset, float y_offset, CFTypeRef shape);
extern CGError SLSSetWindowOpacity(int cid, uint32_t wid, bool isOpaque);
extern CGError SLSOrderWindow(int cid, uint32_t wid, int mode, uint32_t relativeToWID);
extern CGError SLSSetWindowLevel(int cid, uint32_t wid, int level);
extern CGContextRef SLWindowContextCreate(int cid, uint32_t wid, CFDictionaryRef options);
extern CGError CGSNewRegionWithRect(CGRect *rect, CFTypeRef *outRegion);
extern CGError SLSRegisterConnectionNotifyProc(int cid, void *handler, uint32_t event, void *context);
extern CGError SLSMoveWindowsToManagedSpace(int cid, CFArrayRef window_list, uint64_t sid);
extern uint64_t SLSGetActiveSpace(int cid);

//
// CALLBACK FUNCTION TYPES
//

#define PROCESS_EVENT_HANDLER(name) OSStatus name(EventHandlerCallRef ref, EventRef event, void *user_data)
typedef PROCESS_EVENT_HANDLER(process_event_handler);

#define OBSERVER_CALLBACK(name) void name(AXObserverRef observer, AXUIElementRef element, CFStringRef notification, void *context)
typedef OBSERVER_CALLBACK(observer_callback);

#define CONNECTION_CALLBACK(name) void name(uint32_t type, void *data, size_t data_length, void *context, int cid)
typedef CONNECTION_CALLBACK(connection_callback);

//
// FORWARD DECLARATIONS
//

static CFArrayRef cfarray_of_cfnumbers(void *values, size_t size, int count, CFNumberType type);
static inline void is_mission_control_active(void);
static inline uint32_t ax_window_id(AXUIElementRef ref);
static inline pid_t psn_to_pid(ProcessSerialNumber *psn);
static inline pid_t focused_application(void);
static inline AXUIElementRef focused_window(void);
static inline CGRect window_frame(AXUIElementRef ref, uint32_t id);
static inline void subscribe_notifications(pid_t pid);
static inline void unsubscribe_notifications(void);
static inline void border_hide(void);
static void border_refresh(void);
static void border_move_to_active_space(void);

//
// GLOBAL VARIABLES
//

static int g_connection;
static uint32_t g_border_id;
static CGContextRef g_border_context;
static AXObserverRef g_observer;
static AXUIElementRef g_application;
static AXUIElementRef g_window;
static uint32_t g_window_id;

//
// EVENT HANDLERS
//

static bool should_move_to_space;

static CONNECTION_CALLBACK(connection_handler)
{
    if (type == 1401) {
        //
        // Active space changed
        //

        if (g_border_id) {
            border_hide();
            border_refresh();
            border_move_to_active_space();
        }
    } else if (type == 1308) {
        //
        // Active display changed
        //

        if (g_border_id) {
            border_hide();
            should_move_to_space = true;
            border_refresh();
        }
    } else if (type == 1204) {
        //
        // Mission Control was activated.
        //

        border_hide();

        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 0.1f * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            is_mission_control_active();
        });
    }
}

static OBSERVER_CALLBACK(notification_handler)
{
    if (CFEqual(notification, kAXFocusedWindowChangedNotification)) {
        border_refresh();
    } else if (CFEqual(notification, kAXUIElementDestroyedNotification)) {
        if (g_window && CFEqual(g_window, element)) border_refresh();
    } else {
        uint32_t id = ax_window_id(element);
        if (id && g_window_id == id) border_refresh();
    }
}

#if 0

//
// :application_activated
//
// Enable this to subscribe to application activated events.
// This event can sometimes trigger before an application is finished launching.
// and we therefore end up in a situation where we are unable to properly
// update and draw the border for the focused window of said application.
//
// To get around this issue, we will utilize yabai's signal system to notify
// us when the focused application has changed. This is because yabai handles
// the situation when an application has a delayed launch and whatever.
//
// Add the following signal to the end of your yabairc:
// yabai -m signal --add event=application_activated action="pkill -SIGUSR1 limelight &> /dev/null"
//
// This is the simplest way to solve https://github.com/koekeishiya/limelight/issues/3
//

static PROCESS_EVENT_HANDLER(process_handler)
{
    ProcessSerialNumber psn;
    if (GetEventParameter(event, kEventParamProcessID, typeProcessSerialNumber, NULL, sizeof(psn), NULL, &psn) != noErr) {
        return -1;
    }

    switch (GetEventKind(event)) {
    case kEventAppFrontSwitched: {
        border_hide();
        unsubscribe_notifications();
        pid_t pid = psn_to_pid(&psn);
        subscribe_notifications(pid);
        border_refresh();
    } break;
    }

    return noErr;
}
#endif

static void sigusr1_handler(int signal)
{
    border_hide();
    unsubscribe_notifications();
    pid_t pid = focused_application();
    subscribe_notifications(pid);
    border_refresh();
}

//
// HELPER FUNCTIONS
//

static inline void is_mission_control_active(void)
{
    CFArrayRef window_list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, 0);
    int window_count = CFArrayGetCount(window_list);
    bool found = false;

    for (int i = 0; i < window_count; ++i) {
        CFDictionaryRef dictionary = CFArrayGetValueAtIndex(window_list, i);

        CFStringRef name = CFDictionaryGetValue(dictionary, kCGWindowName);
        if (name) continue;

        CFStringRef owner = CFDictionaryGetValue(dictionary, kCGWindowOwnerName);
        if (!owner) continue;

        CFNumberRef layer_ref = CFDictionaryGetValue(dictionary, kCGWindowLayer);
        if (!layer_ref) continue;

        uint64_t layer = 0;
        CFNumberGetValue(layer_ref, CFNumberGetType(layer_ref), &layer);
        if (layer != 18) continue;

        if (CFEqual(CFSTR("Dock"), owner)) {
            found = true;
            break;
        }
    }

    if (found) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 0.1f * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            is_mission_control_active();
        });
    } else {
        border_refresh();
    }

    CFRelease(window_list);
}

static CFArrayRef cfarray_of_cfnumbers(void *values, size_t size, int count, CFNumberType type)
{
    CFNumberRef temp[count];

    for (int i = 0; i < count; ++i) {
        temp[i] = CFNumberCreate(NULL, type, ((char *)values) + (size * i));
    }

    CFArrayRef result = CFArrayCreate(NULL, (const void **)temp, count, &kCFTypeArrayCallBacks);

    for (int i = 0; i < count; ++i) {
        CFRelease(temp[i]);
    }

    return result;
}

static inline uint32_t ax_window_id(AXUIElementRef ref)
{
    uint32_t wid = 0;
    _AXUIElementGetWindow(ref, &wid);
    return wid;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static inline pid_t psn_to_pid(ProcessSerialNumber *psn)
{
    pid_t pid = 0;
    GetProcessPID(psn, &pid);
    return pid;
}
#pragma clang diagnostic pop

static inline pid_t focused_application(void)
{
    ProcessSerialNumber psn;
    _SLPSGetFrontProcess(&psn);
    return psn_to_pid(&psn);
}

static inline AXUIElementRef focused_window(void)
{
    CFTypeRef window = NULL;
    AXUIElementCopyAttributeValue(g_application, kAXFocusedWindowAttribute, &window);
    return window;
}

static inline CGRect window_frame(AXUIElementRef ref, uint32_t id)
{
    CGRect frame;
#if 0
    SLSGetWindowBounds(g_connection, id, &frame);
#else
    CFTypeRef position_ref = NULL;
    CFTypeRef size_ref = NULL;

    AXUIElementCopyAttributeValue(ref, kAXPositionAttribute, &position_ref);
    AXUIElementCopyAttributeValue(ref, kAXSizeAttribute, &size_ref);

    if (position_ref != NULL) {
        AXValueGetValue(position_ref, kAXValueTypeCGPoint, &frame.origin);
        CFRelease(position_ref);
    }

    if (size_ref != NULL) {
        AXValueGetValue(size_ref, kAXValueTypeCGSize, &frame.size);
        CFRelease(size_ref);
    }
#endif
    return frame;
}

//
// MANAGE NOTIFICATIONS
//

static char *ax_error_str[] =
{
    [-kAXErrorSuccess]                           = "kAXErrorSuccess",
    [-kAXErrorFailure]                           = "kAXErrorFailure",
    [-kAXErrorIllegalArgument]                   = "kAXErrorIllegalArgument",
    [-kAXErrorInvalidUIElement]                  = "kAXErrorInvalidUIElement",
    [-kAXErrorInvalidUIElementObserver]          = "kAXErrorInvalidUIElementObserver",
    [-kAXErrorCannotComplete]                    = "kAXErrorCannotComplete",
    [-kAXErrorAttributeUnsupported]              = "kAXErrorAttributeUnsupported",
    [-kAXErrorActionUnsupported]                 = "kAXErrorActionUnsupported",
    [-kAXErrorNotificationUnsupported]           = "kAXErrorNotificationUnsupported",
    [-kAXErrorNotImplemented]                    = "kAXErrorNotImplemented",
    [-kAXErrorNotificationAlreadyRegistered]     = "kAXErrorNotificationAlreadyRegistered",
    [-kAXErrorNotificationNotRegistered]         = "kAXErrorNotificationNotRegistered",
    [-kAXErrorAPIDisabled]                       = "kAXErrorAPIDisabled",
    [-kAXErrorNoValue]                           = "kAXErrorNoValue",
    [-kAXErrorParameterizedAttributeUnsupported] = "kAXErrorParameterizedAttributeUnsupported",
    [-kAXErrorNotEnoughPrecision]                = "kAXErrorNotEnoughPrecision"
};

static void subscribe_notifications(pid_t pid)
{
    printf("---- %s(%d) ----\n", __FUNCTION__, pid);
    g_application = AXUIElementCreateApplication(pid);

    AXError error = AXObserverCreate(pid, notification_handler, &g_observer);
    printf("AXObserverCreate = %s\n", ax_error_str[-error]);

    error = AXObserverAddNotification(g_observer, g_application, kAXFocusedWindowChangedNotification, NULL);
    printf("AXObserverAddNotification(kAXFocusedWindowChangedNotification) = %s\n", ax_error_str[-error]);

    error = AXObserverAddNotification(g_observer, g_application, kAXWindowMovedNotification, NULL);
    printf("AXObserverAddNotification(kAXWindowMovedNotification) = %s\n", ax_error_str[-error]);

    error = AXObserverAddNotification(g_observer, g_application, kAXWindowResizedNotification, NULL);
    printf("AXObserverAddNotification(kAXWindowResizedNotification) = %s\n", ax_error_str[-error]);

    error = AXObserverAddNotification(g_observer, g_application, kAXWindowMiniaturizedNotification, NULL);
    printf("AXObserverAddNotification(kAXWindowMiniaturizedNotification) = %s\n", ax_error_str[-error]);

    error = AXObserverAddNotification(g_observer, g_application, kAXUIElementDestroyedNotification, NULL);
    printf("AXObserverAddNotification(kAXUIElementDestroyedNotification) = %s\n", ax_error_str[-error]);

    CFRunLoopAddSource(CFRunLoopGetMain(), AXObserverGetRunLoopSource(g_observer), kCFRunLoopDefaultMode);
}

static void unsubscribe_notifications(void)
{
    if (!g_observer) return;

    AXObserverRemoveNotification(g_observer, g_application, kAXUIElementDestroyedNotification);
    AXObserverRemoveNotification(g_observer, g_application, kAXWindowMiniaturizedNotification);
    AXObserverRemoveNotification(g_observer, g_application, kAXWindowResizedNotification);
    AXObserverRemoveNotification(g_observer, g_application, kAXWindowMovedNotification);
    AXObserverRemoveNotification(g_observer, g_application, kAXFocusedWindowChangedNotification);
    CFRunLoopSourceInvalidate(AXObserverGetRunLoopSource(g_observer));

    CFRelease(g_observer);
    CFRelease(g_application);

    g_observer = NULL;
    g_application = NULL;
}

//
// BORDER WINDOW STUFF
//

static inline void border_create(CFTypeRef frame_region)
{
    uint32_t tags[2] = { (1 << 7) | (1 << 9) /*| (1 << 11)*/, 0 };
    SLSNewWindow(g_connection, 2, 0, 0, frame_region, &g_border_id);
    SLSSetWindowTags(g_connection, g_border_id, tags, 64);
    SLSSetWindowOpacity(g_connection, g_border_id, 0);
    SLSSetWindowLevel(g_connection, g_border_id, CGWindowLevelForKey(6));
    g_border_context = SLWindowContextCreate(g_connection, g_border_id, 0);
    CGContextSetLineWidth(g_border_context, 4);
    CGContextSetRGBStrokeColor(g_border_context,
                               0.831f,
                               0.824f,
                               0.196f,
                               1.000f);
}

static inline void border_hide(void)
{
    if (g_border_id) SLSOrderWindow(g_connection, g_border_id, 0, 0);
}

static void border_move_to_active_space(void)
{
    uint64_t sid = SLSGetActiveSpace(g_connection);
    CFArrayRef border_id_ref = cfarray_of_cfnumbers(&g_border_id, sizeof(uint32_t), 1, kCFNumberSInt32Type);
    SLSMoveWindowsToManagedSpace(g_connection, border_id_ref, sid);
    CFRelease(border_id_ref);
}

static void border_refresh(void)
{
    //
    // If there is no focused window, hide border (if we have one).
    //

    if (g_window) CFRelease(g_window);

    g_window = focused_window();
    if (!g_window) {
        border_hide();
        return;
    }

    g_window_id = ax_window_id(g_window);
    if (!g_window_id) {
        border_hide();
        return;
    }

    //
    // Grab the frame of the currently focused window.
    //

    CFTypeRef frame_region;
    CGRect frame = window_frame(g_window, g_window_id);
    CGSNewRegionWithRect(&frame, &frame_region);

    //
    // Create a border window if one does not already exist.
    //

    if (!g_border_id) border_create(frame_region);

    //
    // Resize the border window and draw the actual outline of the target frame.
    //

    frame.origin.x = 0; frame.origin.y = 0;
    CGMutablePathRef insert = CGPathCreateMutable();
    CGFloat minx = CGRectGetMinX(frame), maxx = CGRectGetMaxX(frame);
    CGFloat miny = CGRectGetMinY(frame), maxy = CGRectGetMaxY(frame);

    CGPathMoveToPoint(insert, NULL, minx, miny);
    CGPathAddLineToPoint(insert, NULL, maxx, miny);
    CGPathAddLineToPoint(insert, NULL, maxx, maxy);
    CGPathAddLineToPoint(insert, NULL, minx, maxy);
    CGPathAddLineToPoint(insert, NULL, minx, miny);

    SLSDisableUpdate(g_connection);
    SLSOrderWindow(g_connection, g_border_id, 0, 0);

    if (should_move_to_space) {
        border_move_to_active_space();
        should_move_to_space = false;
    }

    SLSSetWindowShape(g_connection, g_border_id, 0.0f, 0.0f, frame_region);

    CGContextClearRect(g_border_context, frame);
    CGContextAddPath(g_border_context, insert);
    CGContextStrokePath(g_border_context);

    CGContextFlush(g_border_context);

    SLSOrderWindow(g_connection, g_border_id, 1, 0);
    SLSReenableUpdate(g_connection);
    CGPathRelease(insert);
    CFRelease(frame_region);
}

//
//
//

int main(int argc, char **argv)
{
    bool success;

    const void *keys[] = { kAXTrustedCheckOptionPrompt };
    const void *values[] = { kCFBooleanTrue };
    CFDictionaryRef options = CFDictionaryCreate(NULL, keys, values, 1, &kCFCopyStringDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    success = AXIsProcessTrustedWithOptions(options);
    CFRelease(options);
    if (!success) return 1;

#if 0
    //
    // :application_activated
    //
    // Enable this to subscribe to application activated events.
    // This event can sometimes trigger before an application is finished launching.
    // and we therefore end up in a situation where we are unable to properly
    // update and draw the border for the focused window of said application.
    //
    // To get around this issue, we will utilize yabai's signal system to notify
    // us when the focused application has changed. This is because yabai handles
    // the situation when an application has a delayed launch and whatever.
    //
    // Add the following signal to the end of your yabairc:
    // yabai -m signal --add event=application_activated action="pkill -SIGUSR1 limelight &> /dev/null"
    //
    // This is the simplest way to solve https://github.com/koekeishiya/limelight/issues/3
    //

    EventTargetRef target = GetApplicationEventTarget();
    EventHandlerUPP handler = NewEventHandlerUPP(process_handler);
    EventTypeSpec type = { kEventClassApplication,  kEventAppFrontSwitched };
    success = InstallEventHandler(target, handler, 1, &type, NULL, NULL) == noErr;
    if (!success) return 2;
#endif

    //
    // Register a handler for the SIGUSR1 signal, so that we can refresh the border from a yabai signal.
    //

    signal(SIGUSR1, sigusr1_handler);

    NSApplicationLoad();
    g_connection = SLSMainConnectionID();
    SLSRegisterConnectionNotifyProc(g_connection, (void*)connection_handler, 1204, NULL);
    SLSRegisterConnectionNotifyProc(g_connection, (void*)connection_handler, 1401, NULL);
    SLSRegisterConnectionNotifyProc(g_connection, (void*)connection_handler, 1308, NULL);
    pid_t pid = focused_application();
    subscribe_notifications(pid);
    border_refresh();
    CFRunLoopRun();
    return 0;
}
