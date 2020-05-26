#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

extern OSStatus _SLPSGetFrontProcess(ProcessSerialNumber *psn);
extern CGError SLSGetWindowOwner(int cid, uint32_t wid, int *wcid);

extern CFStringRef SLSCopyActiveMenuBarDisplayIdentifier(int cid);
extern bool SLSManagedDisplayIsAnimating(int cid, CFStringRef uuid);

extern CFUUIDRef CGDisplayCreateUUIDFromDisplayID(uint32_t did);
extern int SLSSpaceGetType(int cid, uint64_t sid);
extern CFArrayRef SLSCopyWindowsWithOptionsAndTags(int cid, uint32_t owner, CFArrayRef spaces, uint32_t options, uint64_t *set_tags, uint64_t *clear_tags);

extern uint64_t SLSGetActiveSpace(int cid);

struct window_manager
{
    AXUIElementRef system_element;
    struct table application;
    struct table window;
    struct table window_lost_focused_event;
    struct table application_lost_front_switched_event;
    uint32_t focused_window_id;
    ProcessSerialNumber focused_window_psn;
};

struct window *window_manager_focused_window(struct window_manager *wm);
struct application *window_manager_focused_application(struct window_manager *wm);
bool window_manager_find_lost_front_switched_event(struct window_manager *wm, pid_t pid);
void window_manager_remove_lost_front_switched_event(struct window_manager *wm, pid_t pid);
void window_manager_add_lost_front_switched_event(struct window_manager *wm, pid_t pid);
bool window_manager_find_lost_focused_event(struct window_manager *wm, uint32_t window_id);
void window_manager_remove_lost_focused_event(struct window_manager *wm, uint32_t window_id);
void window_manager_add_lost_focused_event(struct window_manager *wm, uint32_t window_id);
struct window *window_manager_find_window(struct window_manager *wm, uint32_t window_id);
void window_manager_remove_window(struct window_manager *wm, uint32_t window_id);
void window_manager_add_window(struct window_manager *wm, struct window *window);
struct application *window_manager_find_application(struct window_manager *wm, pid_t pid);
void window_manager_remove_application(struct window_manager *wm, pid_t pid);
void window_manager_add_application(struct window_manager *wm, struct application *application);
struct window **window_manager_find_application_windows(struct window_manager *wm, struct application *application, int *count);
void window_manager_add_application_windows(struct window_manager *wm, struct application *application);
bool window_manager_refresh_application_windows(struct window_manager *wm);
void window_manager_check_for_windows_on_space(struct window_manager *wm, uint64_t sid);
void window_manager_begin(struct window_manager *window_manager);
void window_manager_init(struct window_manager *window_manager);

CFStringRef display_manager_active_display_uuid(void);
bool display_manager_active_display_is_animating(void);
bool display_manager_display_is_animating(uint32_t did);

uint32_t *space_window_list_for_connection(uint64_t sid, int cid, int *count, bool include_minimized);
uint32_t *space_window_list(uint64_t sid, int *count, bool include_minimized);
int space_type(uint64_t sid);
bool space_is_user(uint64_t sid);
bool space_is_fullscreen(uint64_t sid);
CFStringRef display_uuid(uint32_t did);

#endif
