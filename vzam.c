/*
 * vzam - Composite Overlay Window (COW) Compositor for vxwm
 * Requires linking: -lX11 -lXcomposite -lXdamage -lXrender -lXfixes -lXext -lm
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include "config.h"

typedef struct WindowNode {
    Window id;
    Pixmap pixmap;
    Picture picture;
    Damage damage;
    float x, y, width, height;
    float target_x, target_y, target_w, target_h;
    float opacity, target_opacity;
    int is_dock;
    int alive;
    struct WindowNode *next;
} WindowNode;

static Display *dpy;
static int screen;
static Window root;
static Window overlay = None;
static Picture overlay_picture = None;
static Pixmap backbuffer_pixmap = None;
static Picture backbuffer_picture = None;
static WindowNode *window_list = NULL;

static float current_zoom = 1.0f;
static float target_zoom = 1.0f;

// Global Viewport Camera for Smooth Workspace Sliding
static float current_viewport_x = 0.0f;
static float target_viewport_x = 0.0f;
static float current_viewport_y = 0.0f;
static float target_viewport_y = 0.0f;

// Lock to disable opening pop-in animations during desktop switches (Super+1-9)
static int desktop_switch_lock = 0;

static Atom net_current_desktop_atom;

static int safe_error_handler(Display *d, XErrorEvent *e) {
    (void)d;
    (void)e;
    return 0;
}

static Picture get_alpha_picture(float opacity) {
    static Pixmap p = None;
    static Picture pic = None;
    if (p == None) {
        p = XCreatePixmap(dpy, root, 1, 1, 32);
        XRenderPictFormat *fmt = XRenderFindStandardFormat(dpy, PictStandardARGB32);
        XRenderPictureAttributes pa;
        pa.repeat = RepeatNormal;
        pic = XRenderCreatePicture(dpy, p, fmt, CPRepeat, &pa);
    }
    XRenderColor col = { .red = 0, .green = 0, .blue = 0, .alpha = (unsigned short)(opacity * 0xffff) };
    XRenderFillRectangle(dpy, PictOpSrc, pic, &col, 0, 0, 1, 1);
    return pic;
}

static void update_desktop_viewport() {
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;

    if (XGetWindowProperty(dpy, root, net_current_desktop_atom, 0, 1, False, XA_CARDINAL,
                           &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success && prop) {
        if (nitems > 0) {
            long desktop = *(long *)prop;
            int screen_w = DisplayWidth(dpy, screen);
            target_viewport_x = (float)(desktop * screen_w);
        }
        XFree(prop);
    }
}

static Pixmap get_root_pixmap() {
    Atom act_type;
    int act_format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;
    Pixmap root_pixmap = None;

    Atom _XROOTPMAP_ID = XInternAtom(dpy, "_XROOTPMAP_ID", False);
    Atom ESETROOT_PMAP_ID = XInternAtom(dpy, "ESETROOT_PMAP_ID", False);

    XErrorHandler old_handler = XSetErrorHandler(safe_error_handler);

    if (XGetWindowProperty(dpy, root, _XROOTPMAP_ID, 0, 1, False, XA_PIXMAP,
                           &act_type, &act_format, &nitems, &bytes_after, &prop) == Success && prop) {
        if (nitems > 0 && act_type == XA_PIXMAP) {
            root_pixmap = *(Pixmap *)prop;
        }
        XFree(prop);
        prop = NULL;
    }

    if (root_pixmap == None) {
        if (XGetWindowProperty(dpy, root, ESETROOT_PMAP_ID, 0, 1, False, XA_PIXMAP,
                               &act_type, &act_format, &nitems, &bytes_after, &prop) == Success && prop) {
            if (nitems > 0 && act_type == XA_PIXMAP) {
                root_pixmap = *(Pixmap *)prop;
            }
            XFree(prop);
            prop = NULL;
        }
    }

    XSync(dpy, False);
    XSetErrorHandler(old_handler);
    return root_pixmap;
}

static int is_dock_window(Window win) {
    XWindowAttributes attr;
    if (XGetWindowAttributes(dpy, win, &attr)) {
        int screen_w = DisplayWidth(dpy, screen);
        int screen_h = DisplayHeight(dpy, screen);

        if ((attr.height <= 100 && attr.width >= screen_w - 50) ||
            (attr.width <= 100 && attr.height >= screen_h - 50) ||
            (attr.y == 0 && attr.height <= 120) ||
            (attr.y + attr.height >= screen_h - 5 && attr.height <= 120)) {
            return 1;
        }
    }

    Atom net_wm_window_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", True);
    Atom net_wm_window_type_dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", True);
    Atom net_wm_window_type_toolbar = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_TOOLBAR", True);
    Atom net_wm_window_type_menu = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_MENU", True);
    Atom net_wm_window_type_notification = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NOTIFICATION", True);

    if (net_wm_window_type != None) {
        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char *prop = NULL;

        if (XGetWindowProperty(dpy, win, net_wm_window_type, 0, 32, False, XA_ATOM,
                               &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success && prop) {
            Atom *atoms = (Atom *)prop;
            for (unsigned long i = 0; i < nitems; i++) {
                if (atoms[i] == net_wm_window_type_dock ||
                    atoms[i] == net_wm_window_type_toolbar ||
                    atoms[i] == net_wm_window_type_menu ||
                    atoms[i] == net_wm_window_type_notification) {
                    XFree(prop);
                    return 1;
                }
            }
            XFree(prop);
        }
    }

    XClassHint ch = { NULL, NULL };
    if (XGetClassHint(dpy, win, &ch)) {
        for (int i = 0; ignore_classes[i] != NULL; i++) {
            if ((ch.res_name && strcasecmp(ch.res_name, ignore_classes[i]) == 0) ||
                (ch.res_class && strcasecmp(ch.res_class, ignore_classes[i]) == 0)) {
                if (ch.res_name) XFree(ch.res_name);
                if (ch.res_class) XFree(ch.res_class);
                return 1;
            }
        }
        if (ch.res_name) XFree(ch.res_name);
        if (ch.res_class) XFree(ch.res_class);
    }

    return 0;
}

static int should_ignore_window(Window win) {
    XClassHint ch = { NULL, NULL };
    if (XGetClassHint(dpy, win, &ch)) {
        for (int i = 0; ignore_classes[i] != NULL; i++) {
            if ((ch.res_name && strcasecmp(ch.res_name, ignore_classes[i]) == 0) ||
                (ch.res_class && strcasecmp(ch.res_class, ignore_classes[i]) == 0)) {
                if (ch.res_name) XFree(ch.res_name);
                if (ch.res_class) XFree(ch.res_class);
                return 1;
            }
        }
        if (ch.res_name) XFree(ch.res_name);
        if (ch.res_class) XFree(ch.res_class);
    }
    return 0;
}

static int is_renderable_window(Window win) {
    if (win == root || win == overlay || win == None) return 0;

    Window root_ret, parent_ret, *children = NULL;
    unsigned int nchildren = 0;
    if (!XQueryTree(dpy, win, &root_ret, &parent_ret, &children, &nchildren)) {
        return 0;
    }
    if (children) XFree(children);

    XWindowAttributes attr;
    XErrorHandler old_handler = XSetErrorHandler(safe_error_handler);
    int ok = XGetWindowAttributes(dpy, win, &attr);
    XSync(dpy, False);
    XSetErrorHandler(old_handler);

    if (!ok || attr.class == InputOnly || attr.map_state != IsViewable) return 0;
    if (should_ignore_window(win)) return 0;

    if (parent_ret != root && !attr.override_redirect) return 0;

    return 1;
}

static void init_backbuffer() {
    int width = DisplayWidth(dpy, screen);
    int height = DisplayHeight(dpy, screen);

    if (backbuffer_picture != None) XRenderFreePicture(dpy, backbuffer_picture);
    if (backbuffer_pixmap != None) XFreePixmap(dpy, backbuffer_pixmap);

    backbuffer_pixmap = XCreatePixmap(dpy, root, width, height, 32);
    XRenderPictFormat *format = XRenderFindStandardFormat(dpy, PictStandardARGB32);

    XRenderPictureAttributes pa;
    pa.repeat = RepeatNone;
    backbuffer_picture = XRenderCreatePicture(dpy, backbuffer_pixmap, format, CPRepeat, &pa);
}

static void update_window_pixmap(WindowNode *w) {
    if (!w) return;

    XErrorHandler old_handler = XSetErrorHandler(safe_error_handler);
    w->is_dock = is_dock_window(w->id);

    if (w->picture != None) {
        XRenderFreePicture(dpy, w->picture);
        w->picture = None;
    }
    if (w->pixmap != None) {
        XFreePixmap(dpy, w->pixmap);
        w->pixmap = None;
    }

    XWindowAttributes attr;
    if (XGetWindowAttributes(dpy, w->id, &attr) && attr.map_state == IsViewable) {
        Window child_ret;
        int abs_x, abs_y;
        int bw = attr.border_width;

        if (XTranslateCoordinates(dpy, w->id, root, -bw, -bw, &abs_x, &abs_y, &child_ret)) {
            w->target_x = (float)abs_x;
            w->target_y = (float)abs_y;
        } else {
            w->target_x = (float)(attr.x - bw);
            w->target_y = (float)(attr.y - bw);
        }

        int total_w = attr.width + 2 * bw;
        int total_h = attr.height + 2 * bw;
        w->target_w = (float)(total_w > 1 ? total_w : 1);
        w->target_h = (float)(total_h > 1 ? total_h : 1);

        w->pixmap = XCompositeNameWindowPixmap(dpy, w->id);
        if (w->pixmap != None) {
            XRenderPictFormat *format = XRenderFindVisualFormat(dpy, attr.visual);
            if (!format) format = XRenderFindStandardFormat(dpy, PictStandardARGB32);

            if (format) {
                XRenderPictureAttributes pa;
                pa.subwindow_mode = IncludeInferiors;
                pa.repeat = RepeatNone;
                w->picture = XRenderCreatePicture(dpy, w->pixmap, format, CPSubwindowMode | CPRepeat, &pa);
            }
        }
    }

    XSync(dpy, False);
    XSetErrorHandler(old_handler);
}

static WindowNode *find_window(Window win) {
    for (WindowNode *w = window_list; w != NULL; w = w->next) {
        if (w->id == win) return w;
    }
    return NULL;
}

static void start_window_close(WindowNode *w) {
    if (!w || !w->alive) return;
    w->alive = 0;
    w->target_opacity = 0.0f;
    if (!w->is_dock) {
        float old_w = w->target_w;
        float old_h = w->target_h;
        w->target_w *= 0.85f;
        w->target_h *= 0.85f;
        w->target_x += (old_w - w->target_w) * 0.5f;
        w->target_y += (old_h - w->target_h) * 0.5f;
    }
}

static WindowNode *add_or_update_window(Window win) {
    if (!is_renderable_window(win)) {
        WindowNode *w = find_window(win);
        if (w) start_window_close(w);
        return NULL;
    }

    WindowNode *w = find_window(win);
    if (w) {
        w->alive = 1;
        w->target_opacity = 1.0f;
        update_window_pixmap(w);
        return w;
    }

    WindowNode *node = calloc(1, sizeof(WindowNode));
    if (!node) return NULL;

    node->id = win;
    node->damage = XDamageCreate(dpy, win, XDamageReportRawRectangles);
    node->alive = 1;
    node->opacity = 0.0f;
    node->target_opacity = 1.0f;

    update_window_pixmap(node);

    // OPENING ANIMATION: Pop-in scale effect if not switching workspaces
    if (!node->is_dock && desktop_switch_lock == 0 && fabsf(target_viewport_x - current_viewport_x) < 0.1f) {
        node->width = node->target_w * 0.90f;
        node->height = node->target_h * 0.90f;
        node->x = node->target_x + (node->target_w - node->width) * 0.5f;
        node->y = node->target_y + (node->target_h - node->height) * 0.5f;
    } else {
        node->x = node->target_x;
        node->y = node->target_y;
        node->width = node->target_w;
        node->height = node->target_h;
    }

    if (node->picture == None) {
        if (node->damage != None) XDamageDestroy(dpy, node->damage);
        free(node);
        return NULL;
    }

    node->next = window_list;
    window_list = node;
    return node;
}

static void sort_windows_by_z_order() {
    Window root_ret, parent_ret, *children = NULL;
    unsigned int nchildren = 0;

    if (!XQueryTree(dpy, root, &root_ret, &parent_ret, &children, &nchildren) || nchildren == 0) {
        if (children) XFree(children);
        return;
    }

    WindowNode *sorted_head = NULL;
    WindowNode *sorted_tail = NULL;

    for (unsigned int i = 0; i < nchildren; i++) {
        WindowNode **curr = &window_list;
        while (*curr) {
            if ((*curr)->id == children[i]) {
                WindowNode *match = *curr;
                *curr = match->next;
                match->next = NULL;

                if (!sorted_head) {
                    sorted_head = match;
                    sorted_tail = match;
                } else {
                    sorted_tail->next = match;
                    sorted_tail = match;
                }
                break;
            }
            curr = &(*curr)->next;
        }
    }

    WindowNode *stray = window_list;
    while (stray) {
        WindowNode *next = stray->next;
        XErrorHandler old_handler = XSetErrorHandler(safe_error_handler);
        if (stray->damage != None) XDamageDestroy(dpy, stray->damage);
        if (stray->picture != None) XRenderFreePicture(dpy, stray->picture);
        if (stray->pixmap != None) XFreePixmap(dpy, stray->pixmap);
        XSync(dpy, False);
        XSetErrorHandler(old_handler);
        free(stray);
        stray = next;
    }

    window_list = sorted_head;
    if (children) XFree(children);
}

static void grab_keys() {
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_equal), Mod4Mask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_plus), Mod4Mask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_minus), Mod4Mask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_0), Mod4Mask, root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, XKeysymToKeycode(dpy, XK_r), Mod1Mask, root, True, GrabModeAsync, GrabModeAsync);

    XGrabButton(dpy, Button4, Mod4Mask, root, False, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None);
    XGrabButton(dpy, Button5, Mod4Mask, root, False, ButtonPressMask, GrabModeAsync, GrabModeAsync, None, None);
}

static void render_frame() {
    int screen_w = DisplayWidth(dpy, screen);
    int screen_h = DisplayHeight(dpy, screen);

    if (desktop_switch_lock > 0) desktop_switch_lock--;

    if (backbuffer_picture == None || overlay_picture == None) return;

    XErrorHandler old_handler = XSetErrorHandler(safe_error_handler);

    // Zoom interpolation
    current_zoom += (target_zoom - current_zoom) * 0.20f;
    if (fabsf(target_zoom - current_zoom) < 0.0005f) {
        current_zoom = target_zoom;
    }

    // Viewport camera sliding interpolation
    current_viewport_x += (target_viewport_x - current_viewport_x) * anim_step;
    current_viewport_y += (target_viewport_y - current_viewport_y) * anim_step;
    if (fabsf(target_viewport_x - current_viewport_x) < 0.1f) current_viewport_x = target_viewport_x;
    if (fabsf(target_viewport_y - current_viewport_y) < 0.1f) current_viewport_y = target_viewport_y;

    sort_windows_by_z_order();

    XRenderColor solid_black = { .red = 0x0000, .green = 0x0000, .blue = 0x0000, .alpha = 0xffff };
    XRenderFillRectangle(dpy, PictOpSrc, backbuffer_picture, &solid_black, 0, 0, screen_w, screen_h);

    WindowNode **curr = &window_list;
    while (*curr) {
        WindowNode *w = *curr;

        // Opacity interpolation
        w->opacity += (w->target_opacity - w->opacity) * anim_step;
        if (fabsf(w->target_opacity - w->opacity) < 0.005f) {
            w->opacity = w->target_opacity;
        }

        if (!w->alive && w->opacity <= 0.01f) {
            *curr = w->next;
            if (w->damage != None) XDamageDestroy(dpy, w->damage);
            if (w->picture != None) XRenderFreePicture(dpy, w->picture);
            if (w->pixmap != None) XFreePixmap(dpy, w->pixmap);
            free(w);
            continue;
        }

        if (w->is_dock) {
            w->x = w->target_x;
            w->y = w->target_y;
            w->width = w->target_w;
            w->height = w->target_h;
        } else {
            // Smoothly interpolate position and size (handles both open scale-up and close shrink-down)
            w->x += (w->target_x - w->x) * anim_step;
            w->y += (w->target_y - w->y) * anim_step;
            w->width += (w->target_w - w->width) * anim_step;
            w->height += (w->target_h - w->height) * anim_step;
        }

        curr = &(*curr)->next;
    }

    Pixmap root_pm = get_root_pixmap();
    if (root_pm != None) {
        XRenderPictFormat *format = XRenderFindVisualFormat(dpy, DefaultVisual(dpy, screen));
        if (!format) format = XRenderFindStandardFormat(dpy, PictStandardRGB24);
        if (format) {
            Picture root_pic = XRenderCreatePicture(dpy, root_pm, format, 0, NULL);
            if (root_pic != None) {
                XRenderComposite(dpy, PictOpSrc, root_pic, None, backbuffer_picture,
                                 0, 0, 0, 0, (int)-current_viewport_x, (int)-current_viewport_y, screen_w * 5, screen_h);
                XRenderFreePicture(dpy, root_pic);
            }
        }
    }

    float cx = screen_w / 2.0f;
    float cy = screen_h / 2.0f;

    XTransform identity = {{
        { XDoubleToFixed(1.0), 0, 0 },
        { 0, XDoubleToFixed(1.0), 0 },
        { 0, 0, XDoubleToFixed(1.0) }
    }};

    WindowNode *w = window_list;
    while (w) {
        if (w->picture != None && w->opacity > 0.01f) {
            float render_x = w->x - current_viewport_x;
            float render_y = w->y - current_viewport_y;
            Picture alpha_mask = (w->opacity < 0.999f) ? get_alpha_picture(w->opacity) : None;

            if (w->is_dock) {
                XRenderSetPictureTransform(dpy, w->picture, &identity);
                XRenderComposite(dpy, PictOpOver, w->picture, alpha_mask, backbuffer_picture,
                                 0, 0, 0, 0, (int)roundf(w->x), (int)roundf(w->y), (int)roundf(w->width), (int)roundf(w->height));
            } else {
                if (fabsf(current_zoom - 1.0f) > 0.001f) {
                    XTransform xform = {{
                        { XDoubleToFixed(1.0 / current_zoom), 0, 0 },
                        { 0, XDoubleToFixed(1.0 / current_zoom), 0 },
                        { 0, 0, XDoubleToFixed(1.0) }
                    }};
                    XRenderSetPictureTransform(dpy, w->picture, &xform);
                    XRenderSetPictureFilter(dpy, w->picture, FilterBilinear, NULL, 0);

                    int dst_x = (int)roundf(cx + (render_x - cx) * current_zoom);
                    int dst_y = (int)roundf(cy + (render_y - cy) * current_zoom);
                    int dst_w = (int)roundf(w->width * current_zoom);
                    int dst_h = (int)roundf(w->height * current_zoom);

                    if (dst_x + dst_w > 0 && dst_y + dst_h > 0 && dst_x < screen_w && dst_y < screen_h) {
                        XRenderComposite(dpy, PictOpOver, w->picture, alpha_mask, backbuffer_picture,
                                         0, 0, 0, 0, dst_x, dst_y, dst_w, dst_h);
                    }
                } else {
                    XRenderSetPictureTransform(dpy, w->picture, &identity);
                    int dst_x = (int)roundf(render_x);
                    int dst_y = (int)roundf(render_y);
                    int dst_w = (int)roundf(w->width);
                    int dst_h = (int)roundf(w->height);
                    if (dst_x + dst_w > 0 && dst_y + dst_h > 0 && dst_x < screen_w && dst_y < screen_h) {
                        XRenderComposite(dpy, PictOpOver, w->picture, alpha_mask, backbuffer_picture,
                                         0, 0, 0, 0, dst_x, dst_y, dst_w, dst_h);
                    }
                }
            }
        }
        w = w->next;
    }

    XRenderComposite(dpy, PictOpSrc, backbuffer_picture, None, overlay_picture,
                     0, 0, 0, 0, 0, 0, screen_w, screen_h);

    XSync(dpy, False);
    XSetErrorHandler(old_handler);
}

int main() {
    dpy = XOpenDisplay(NULL);
    if (!dpy) {
        fprintf(stderr, "vzam: Failed to open X display\n");
        return 1;
    }

    XSetErrorHandler(safe_error_handler);

    screen = DefaultScreen(dpy);
    root = RootWindow(dpy, screen);

    net_current_desktop_atom = XInternAtom(dpy, "_NET_CURRENT_DESKTOP", False);
    update_desktop_viewport();
    current_viewport_x = target_viewport_x;

    XCompositeRedirectSubwindows(dpy, root, CompositeRedirectManual);
    XSelectInput(dpy, root, SubstructureNotifyMask | PropertyChangeMask | ExposureMask);

    overlay = XCompositeGetOverlayWindow(dpy, root);
    if (overlay == None) {
        fprintf(stderr, "vzam: Failed to acquire Composite Overlay Window\n");
        XCloseDisplay(dpy);
        return 1;
    }

    XserverRegion region = XFixesCreateRegion(dpy, NULL, 0);
    XFixesSetWindowShapeRegion(dpy, overlay, ShapeInput, 0, 0, region);
    XFixesDestroyRegion(dpy, region);

    XWindowAttributes overlay_attr;
    XGetWindowAttributes(dpy, overlay, &overlay_attr);
    XRenderPictFormat *overlay_format = XRenderFindVisualFormat(dpy, overlay_attr.visual);
    if (!overlay_format) overlay_format = XRenderFindStandardFormat(dpy, PictStandardRGB24);
    overlay_picture = XRenderCreatePicture(dpy, overlay, overlay_format, 0, NULL);

    if (overlay_picture == None) {
        fprintf(stderr, "vzam: Failed to create picture for overlay window\n");
        XCloseDisplay(dpy);
        return 1;
    }

    init_backbuffer();
    grab_keys();

    unsigned int nchildren = 0;
    Window root_ret, parent_ret, *children = NULL;
    if (XQueryTree(dpy, root, &root_ret, &parent_ret, &children, &nchildren)) {
        for (unsigned int i = 0; i < nchildren; i++) {
            WindowNode *wn = add_or_update_window(children[i]);
            if (wn) {
                wn->opacity = 1.0f;
                wn->target_opacity = 1.0f;
                wn->width = wn->target_w;
                wn->height = wn->target_h;
                wn->x = wn->target_x;
                wn->y = wn->target_y;
            }
        }
        if (children) XFree(children);
    }

    int damage_event_base, damage_error_base;
    XDamageQueryExtension(dpy, &damage_event_base, &damage_error_base);

    printf("success\n");

    XEvent ev;
    while (1) {
        while (XPending(dpy)) {
            XNextEvent(dpy, &ev);

            if (ev.type == ButtonPress) {
                if (ev.xbutton.button == Button4) {
                    target_zoom += 0.08f;
                    if (target_zoom > 2.5f) target_zoom = 2.5f;
                } else if (ev.xbutton.button == Button5) {
                    target_zoom -= 0.08f;
                    if (target_zoom < 0.35f) target_zoom = 0.35f;
                }
            } else if (ev.type == KeyPress) {
                KeySym keysym = XLookupKeysym(&ev.xkey, 0);
                if (keysym == XK_equal || keysym == XK_plus) {
                    target_zoom += 0.10f;
                    if (target_zoom > 2.5f) target_zoom = 2.5f;
                } else if (keysym == XK_minus) {
                    target_zoom -= 0.10f;
                    if (target_zoom < 0.35f) target_zoom = 0.35f;
                } else if (keysym == XK_0 || keysym == XK_r || keysym == XK_R) {
                    target_zoom = 1.0f;
                }
            } else if (ev.type == PropertyNotify) {
                if (ev.xproperty.atom == net_current_desktop_atom) {
                    update_desktop_viewport();
                    desktop_switch_lock = 60;
                } else {
                    WindowNode *w = find_window(ev.xproperty.window);
                    if (w) {
                        w->is_dock = is_dock_window(w->id);
                    }
                }
            } else if (ev.type == MapNotify) {
                add_or_update_window(ev.xmap.window);
            } else if (ev.type == MapRequest) {
                XMapWindow(dpy, ev.xmaprequest.window);
                add_or_update_window(ev.xmaprequest.window);
            } else if (ev.type == UnmapNotify) {
                WindowNode *w = find_window(ev.xunmap.window);
                if (w) {
                    start_window_close(w);
                }
            } else if (ev.type == DestroyNotify) {
                WindowNode *w = find_window(ev.xdestroywindow.window);
                if (w) {
                    start_window_close(w);
                }
            } else if (ev.type == ConfigureNotify) {
                WindowNode *w = find_window(ev.xconfigure.window);
                if (w) {
                    update_window_pixmap(w);
                } else {
                    add_or_update_window(ev.xconfigure.window);
                }
            } else if (ev.type == ReparentNotify) {
                if (ev.xreparent.parent != root) {
                    WindowNode *w = find_window(ev.xreparent.window);
                    if (w) {
                        start_window_close(w);
                    }
                } else {
                    add_or_update_window(ev.xreparent.window);
                }
            } else if (ev.type == damage_event_base + XDamageNotify) {
                XDamageNotifyEvent *lev = (XDamageNotifyEvent *)&ev;
                XDamageSubtract(dpy, lev->damage, None, None);
            }
        }

        render_frame();
        usleep(idle_sleep_us);
    }

    if (overlay != None) {
        XCompositeReleaseOverlayWindow(dpy, root);
    }
    XCloseDisplay(dpy);
    return 0;
}
