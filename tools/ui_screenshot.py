#!/usr/bin/env python3
"""Launches the standalone build, clicks at window-relative points, and saves a screenshot.

Run it against a private X server so it doesn't touch your desktop, e.g. a headless weston:
  weston --backend=headless --socket=plasma-test --xwayland --width=1600 --height=1000 &
  DISPLAY=:N tools/ui_screenshot.py build/Plasma_artefacts/Release/Standalone/Plasma out.png 237,51

Each step is "x,y" (left click), "r:x,y" (right click) or "key:NAME" (an X keysym such as Return).
Input is delivered with XSendEvent because Xwayland ignores XTest input.
Needs ImageMagick's `import` for the capture.
"""
import ctypes
import subprocess
import sys
import time

X = ctypes.cdll.LoadLibrary('libX11.so.6')
c_ul = ctypes.c_ulong
X.XOpenDisplay.restype = ctypes.c_void_p
X.XDefaultRootWindow.restype = c_ul
X.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
X.XQueryTree.argtypes = [ctypes.c_void_p, c_ul, ctypes.POINTER(c_ul), ctypes.POINTER(c_ul),
                         ctypes.POINTER(ctypes.POINTER(c_ul)), ctypes.POINTER(ctypes.c_uint)]
X.XFetchName.argtypes = [ctypes.c_void_p, c_ul, ctypes.POINTER(ctypes.c_char_p)]
X.XTranslateCoordinates.argtypes = [ctypes.c_void_p, c_ul, c_ul, ctypes.c_int, ctypes.c_int,
                                    ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
                                    ctypes.POINTER(c_ul)]
X.XFlush.argtypes = [ctypes.c_void_p]


class XButtonEvent(ctypes.Structure):
    _fields_ = [('type', ctypes.c_int), ('serial', c_ul), ('send_event', ctypes.c_int),
                ('display', ctypes.c_void_p), ('window', c_ul), ('root', c_ul), ('subwindow', c_ul),
                ('time', c_ul), ('x', ctypes.c_int), ('y', ctypes.c_int), ('x_root', ctypes.c_int),
                ('y_root', ctypes.c_int), ('state', ctypes.c_uint), ('button', ctypes.c_uint),
                ('same_screen', ctypes.c_int)]


class XEvent(ctypes.Union):
    _fields_ = [('xbutton', XButtonEvent), ('pad', c_ul * 24)]


X.XSendEvent.argtypes = [ctypes.c_void_p, c_ul, ctypes.c_int, ctypes.c_long, ctypes.POINTER(XEvent)]
X.XStringToKeysym.restype = c_ul
X.XStringToKeysym.argtypes = [ctypes.c_char_p]
X.XKeysymToKeycode.restype = ctypes.c_ubyte
X.XKeysymToKeycode.argtypes = [ctypes.c_void_p, c_ul]
X.XGetInputFocus.argtypes = [ctypes.c_void_p, ctypes.POINTER(c_ul), ctypes.POINTER(ctypes.c_int)]

KEY_PRESS, KEY_RELEASE, MOTION_NOTIFY, BUTTON_PRESS, BUTTON_RELEASE = 2, 3, 6, 4, 5
KEY_PRESS_MASK, KEY_RELEASE_MASK = 1 << 0, 1 << 1
POINTER_MOTION_MASK, BUTTON_PRESS_MASK, BUTTON_RELEASE_MASK = 1 << 6, 1 << 2, 1 << 3
BUTTON1_MASK, BUTTON3_MASK = 1 << 8, 1 << 10


def find_window(display, window, name):
    window_name = ctypes.c_char_p()
    if X.XFetchName(display, window, ctypes.byref(window_name)) and window_name.value == name:
        return window
    root, parent, children, count = c_ul(), c_ul(), ctypes.POINTER(c_ul)(), ctypes.c_uint()
    if X.XQueryTree(display, window, ctypes.byref(root), ctypes.byref(parent),
                    ctypes.byref(children), ctypes.byref(count)):
        for i in range(count.value):
            found = find_window(display, children[i], name)
            if found:
                return found
    return None


def main():
    app, out_path, clicks = sys.argv[1], sys.argv[2], sys.argv[3:]
    display = X.XOpenDisplay(None)
    root = X.XDefaultRootWindow(display)
    process = subprocess.Popen([app], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(8)
        window = find_window(display, root, b'Plasma')
        if window is None:
            sys.exit('Plasma window not found')
        origin_x, origin_y, child = ctypes.c_int(), ctypes.c_int(), c_ul()
        X.XTranslateCoordinates(display, window, root, 0, 0, ctypes.byref(origin_x),
                                ctypes.byref(origin_y), ctypes.byref(child))

        def send(kind, x, y, button, state, mask):
            event = XEvent()
            b = event.xbutton
            b.type, b.display, b.window, b.root = kind, display, window, root
            b.x, b.y = x, y
            b.x_root, b.y_root = origin_x.value + x, origin_y.value + y
            b.button, b.state, b.same_screen = button, state, 1
            X.XSendEvent(display, window, 1, mask, ctypes.byref(event))
            X.XFlush(display)

        for step in clicks:
            if step.startswith('key:'):
                # XButtonEvent and XKeyEvent share a layout; `button` is the keycode field.
                keycode = X.XKeysymToKeycode(display, X.XStringToKeysym(step[4:].encode()))
                send(KEY_PRESS, 0, 0, keycode, 0, KEY_PRESS_MASK)
                time.sleep(0.1)
                send(KEY_RELEASE, 0, 0, keycode, 0, KEY_RELEASE_MASK)
                time.sleep(1.5)
                continue

            button, release_state = 1, BUTTON1_MASK
            if step.startswith('r:'):
                step = step[2:]
                button, release_state = 3, BUTTON3_MASK
            x, y = map(int, step.split(','))
            send(MOTION_NOTIFY, x, y, 0, 0, POINTER_MOTION_MASK)
            time.sleep(0.3)
            send(BUTTON_PRESS, x, y, button, 0, BUTTON_PRESS_MASK)
            time.sleep(0.1)
            send(BUTTON_RELEASE, x, y, button, release_state, BUTTON_RELEASE_MASK)
            time.sleep(1.5)
        subprocess.run(['import', '-window', hex(window), out_path], check=True)
    finally:
        process.terminate()
        time.sleep(1)
        process.kill()


if __name__ == '__main__':
    main()
