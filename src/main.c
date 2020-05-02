#include <Carbon/Carbon.h>

//
// PRIVATE CORECRAPHICS / SKYLIGHT FUNCTION DECLARATIONS
//

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

//
// CALLBACK FUNCTION TYPES
//

#define PROCESS_EVENT_HANDLER(name) OSStatus name(EventHandlerCallRef ref, EventRef event, void *user_data)
typedef PROCESS_EVENT_HANDLER(process_event_handler);

#define OBSERVER_CALLBACK(name) void name(AXObserverRef observer, AXUIElementRef element, CFStringRef notification, void *context)
typedef OBSERVER_CALLBACK(observer_callback);

//
// FORWARD DECLARATIONS
//

static inline uint32_t ax_window_id(AXUIElementRef ref);
static inline pid_t psn_to_pid(ProcessSerialNumber *psn);
static inline pid_t focused_application(void);
static inline AXUIElementRef focused_window(void);
static inline CGRect window_frame(AXUIElementRef ref, uint32_t id);
static inline void subscribe_notifications(pid_t pid);
static inline void unsubscribe_notifications(void);
static void border_refresh(void);

//
// GLOBAL VARIABLES
//

static int g_connection;
static uint32_t g_border_id;
static CGContextRef g_border_context;
static AXObserverRef g_observer;
static AXUIElementRef g_application;
static uint32_t g_window_id;

//
// EVENT HANDLERS
//

static OBSERVER_CALLBACK(notification_handler)
{
    if (CFEqual(notification, kAXFocusedWindowChangedNotification)) {
        border_refresh();
    } else {
        uint32_t id = ax_window_id(element);
        if (id && g_window_id == id) border_refresh();
    }
}

static PROCESS_EVENT_HANDLER(process_handler)
{
    ProcessSerialNumber psn;
    if (GetEventParameter(event, kEventParamProcessID, typeProcessSerialNumber, NULL, sizeof(psn), NULL, &psn) != noErr) {
        return -1;
    }

    switch (GetEventKind(event)) {
    case kEventAppFrontSwitched: {
        unsubscribe_notifications();
        pid_t pid = psn_to_pid(&psn);
        subscribe_notifications(pid);
        border_refresh();
    } break;
    }

    return noErr;
}

//
// HELPER FUNCTIONS
//

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

static void subscribe_notifications(pid_t pid)
{
    g_application = AXUIElementCreateApplication(pid);
    AXObserverCreate(pid, notification_handler, &g_observer);
    AXObserverAddNotification(g_observer, g_application, kAXFocusedWindowChangedNotification, NULL);
    AXObserverAddNotification(g_observer, g_application, kAXWindowMovedNotification, NULL);
    AXObserverAddNotification(g_observer, g_application, kAXWindowResizedNotification, NULL);
    CFRunLoopAddSource(CFRunLoopGetMain(), AXObserverGetRunLoopSource(g_observer), kCFRunLoopDefaultMode);
}

static void unsubscribe_notifications(void)
{
    if (!g_observer) return;

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
    uint32_t tags[2] = { (1 << 7) | (1 << 9) | (1 << 11), 0 };
    SLSNewWindow(g_connection, 2, 0, 0, frame_region, &g_border_id);
    SLSSetWindowTags(g_connection, g_border_id, tags, 64);
    SLSSetWindowOpacity(g_connection, g_border_id, 0);
    SLSSetWindowLevel(g_connection, g_border_id, 5);
    g_border_context = SLWindowContextCreate(g_connection, g_border_id, 0);
    CGContextSetLineWidth(g_border_context, 4);
    CGContextSetRGBStrokeColor(g_border_context,
                               0.831f,
                               0.824f,
                               0.196f,
                               1.000f);
}

static void border_refresh(void)
{
    AXUIElementRef ref = focused_window();
    if (!ref) {

        //
        // No focused window, hide border if we have one.
        //

        if (g_border_id) SLSOrderWindow(g_connection, g_border_id, 0, 0);
        return;
    }

    //
    // Grab the frame of the currently focused window.
    //

    g_window_id = ax_window_id(ref);
    CGRect frame = window_frame(ref, g_window_id);

    CFTypeRef frame_region;
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

    EventTargetRef target;
    EventHandlerUPP handler;
    EventTypeSpec type;
    EventHandlerRef ref;
    target = GetApplicationEventTarget();
    handler = NewEventHandlerUPP(process_handler);
    type.eventClass = kEventClassApplication;
    type.eventKind  = kEventAppFrontSwitched;
    success = InstallEventHandler(target, handler, 1, &type, NULL, &ref) == noErr;
    if (!success) return 2;

    g_connection = SLSMainConnectionID();
    pid_t pid = focused_application();
    subscribe_notifications(pid);
    border_refresh();
    CFRunLoopRun();
    return 0;
}
