// Minimal CLAP host that opens the plugin's X11 editor the way Bitwig does (timer support and,
// optionally, POSIX fd support for the plugin's event loop), drags a knob with synthetic mouse
// events, and reports how far the knob lags behind the mouse.
//
// Usage: plasma_clap_gui_latency PLUGIN.clap use_fd(0|1) "Param Name" knob_x knob_y
// Needs an X display (a headless weston + Xwayland works).

#include <clap/clap.h>

#include <X11/Xlib.h>
#include <dlfcn.h>
#include <poll.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {
  using Clock = std::chrono::steady_clock;

  struct Timer { clap_id id; uint32_t period_ms; Clock::time_point next; };
  struct Fd { int fd; clap_posix_fd_flags_t flags; };

  bool g_use_fd = false;
  std::vector<Timer> g_timers;
  std::vector<Fd> g_fds;
  bool g_callback_requested = false;
  int g_timer_calls = 0;
  std::thread::id g_main_thread;

  bool registerTimer(const clap_host_t*, uint32_t period_ms, clap_id* id) {
    *id = (clap_id)g_timers.size() + 1;
    g_timers.push_back({ *id, period_ms, Clock::now() + std::chrono::milliseconds(period_ms) });
    return true;
  }
  bool unregisterTimer(const clap_host_t*, clap_id id) {
    for (size_t i = 0; i < g_timers.size(); ++i) {
      if (g_timers[i].id == id) {
        g_timers.erase(g_timers.begin() + i);
        return true;
      }
    }
    return false;
  }
  const clap_host_timer_support_t kTimerSupport = { registerTimer, unregisterTimer };

  bool registerFd(const clap_host_t*, int fd, clap_posix_fd_flags_t flags) {
    g_fds.push_back({ fd, flags });
    return true;
  }
  bool modifyFd(const clap_host_t*, int fd, clap_posix_fd_flags_t flags) {
    for (auto& f : g_fds) {
      if (f.fd == fd)
        f.flags = flags;
    }
    return true;
  }
  bool unregisterFd(const clap_host_t*, int fd) {
    for (size_t i = 0; i < g_fds.size(); ++i) {
      if (g_fds[i].fd == fd) {
        g_fds.erase(g_fds.begin() + i);
        return true;
      }
    }
    return false;
  }
  const clap_host_posix_fd_support_t kFdSupport = { registerFd, modifyFd, unregisterFd };

  bool isMainThread(const clap_host_t*) { return std::this_thread::get_id() == g_main_thread; }
  bool isAudioThread(const clap_host_t*) { return false; }
  const clap_host_thread_check_t kThreadCheck = { isMainThread, isAudioThread };

  void guiResizeHintsChanged(const clap_host_t*) { }
  bool guiRequestResize(const clap_host_t*, uint32_t, uint32_t) { return true; }
  bool guiRequestShow(const clap_host_t*) { return true; }
  bool guiRequestHide(const clap_host_t*) { return true; }
  void guiClosed(const clap_host_t*, bool) { }
  const clap_host_gui_t kGui = { guiResizeHintsChanged, guiRequestResize, guiRequestShow, guiRequestHide, guiClosed };

  void paramsRescan(const clap_host_t*, clap_param_rescan_flags) { }
  void paramsClear(const clap_host_t*, clap_id, clap_param_clear_flags) { }
  void paramsRequestFlush(const clap_host_t*) { }
  const clap_host_params_t kParams = { paramsRescan, paramsClear, paramsRequestFlush };

  void logMessage(const clap_host_t*, clap_log_severity, const char*) { }
  const clap_host_log_t kLog = { logMessage };

  const void* getExtension(const clap_host_t*, const char* id) {
    if (!strcmp(id, CLAP_EXT_TIMER_SUPPORT))
      return &kTimerSupport;
    if (!strcmp(id, CLAP_EXT_POSIX_FD_SUPPORT))
      return g_use_fd ? &kFdSupport : nullptr;
    if (!strcmp(id, CLAP_EXT_THREAD_CHECK))
      return &kThreadCheck;
    if (!strcmp(id, CLAP_EXT_GUI))
      return &kGui;
    if (!strcmp(id, CLAP_EXT_PARAMS))
      return &kParams;
    if (!strcmp(id, CLAP_EXT_LOG))
      return &kLog;
    return nullptr;
  }
  void requestRestart(const clap_host_t*) { }
  void requestProcess(const clap_host_t*) { }
  void requestCallback(const clap_host_t*) { g_callback_requested = true; }

  clap_host_t g_host = {
    CLAP_VERSION, nullptr, "latency-test", "plasma", "", "1.0",
    getExtension, requestRestart, requestProcess, requestCallback
  };

  struct Plugin {
    const clap_plugin_t* plugin = nullptr;
    const clap_plugin_timer_support_t* timers = nullptr;
    const clap_plugin_posix_fd_support_t* fds = nullptr;
    const clap_plugin_params_t* params = nullptr;
  };

  // One pass of the host's main loop: services due timers and readable fds for up to max_wait_ms.
  void runHostLoop(Plugin& p, int max_wait_ms) {
    auto now = Clock::now();
    int wait = max_wait_ms;
    for (auto& t : g_timers)
      wait = std::min<int>(wait, std::max<long>(0, std::chrono::duration_cast<std::chrono::milliseconds>(t.next - now).count()));

    std::vector<pollfd> pfds;
    for (auto& f : g_fds)
      pfds.push_back({ f.fd, POLLIN, 0 });
    if (!pfds.empty())
      poll(pfds.data(), pfds.size(), wait);
    else
      std::this_thread::sleep_for(std::chrono::milliseconds(wait));

    for (auto& pfd : pfds) {
      if ((pfd.revents & POLLIN) && p.fds)
        p.fds->on_fd(p.plugin, pfd.fd, CLAP_POSIX_FD_READ);
    }

    now = Clock::now();
    for (size_t i = 0; i < g_timers.size(); ++i) {
      if (now >= g_timers[i].next) {
        g_timers[i].next = now + std::chrono::milliseconds(g_timers[i].period_ms);
        if (p.timers) {
          p.timers->on_timer(p.plugin, g_timers[i].id);
          ++g_timer_calls;
        }
      }
    }

    if (g_callback_requested) {
      g_callback_requested = false;
      p.plugin->on_main_thread(p.plugin);
    }
  }

  void pumpFor(Plugin& p, double seconds) {
    auto end = Clock::now() + std::chrono::duration<double>(seconds);
    while (Clock::now() < end)
      runHostLoop(p, 5);
  }

  // The JUCE peer is the child of our parent window; its OpenGL child window doesn't take input.
  Window findEditorWindow(Display* display, Window window, int depth = 0) {
    Window root, parent, *children = nullptr;
    unsigned int count = 0;
    Window found = 0;
    if (XQueryTree(display, window, &root, &parent, &children, &count)) {
      for (unsigned int i = 0; i < count; ++i) {
        XWindowAttributes attributes;
        XGetWindowAttributes(display, children[i], &attributes);
        if (getenv("LATENCY_VERBOSE"))
          std::printf("%*swindow 0x%lx %dx%d events 0x%lx\n", depth * 2, "", children[i], attributes.width,
                      attributes.height, attributes.all_event_masks);
        if (!found && (attributes.all_event_masks & ButtonPressMask))
          found = children[i];
        Window inner = findEditorWindow(display, children[i], depth + 1);
        if (!found && inner)
          found = inner;
      }
      XFree(children);
    }
    return found;
  }

  void sendMouse(Display* display, Window window, int type, int x, int y, unsigned int state, unsigned int button) {
    XEvent event;
    memset(&event, 0, sizeof(event));
    event.xbutton.type = type;
    event.xbutton.display = display;
    event.xbutton.window = window;
    event.xbutton.root = DefaultRootWindow(display);
    event.xbutton.x = x;
    event.xbutton.y = y;
    Window child;
    XTranslateCoordinates(display, window, DefaultRootWindow(display), x, y,
                          &event.xbutton.x_root, &event.xbutton.y_root, &child);
    event.xbutton.state = state;
    event.xbutton.button = button;
    event.xbutton.same_screen = True;
    long mask = type == MotionNotify ? PointerMotionMask : (type == ButtonPress ? ButtonPressMask : ButtonReleaseMask);
    XSendEvent(display, window, True, mask, &event);
    XFlush(display);
  }
}

int main(int argc, char** argv) {
  if (argc < 6) {
    std::printf("usage: %s PLUGIN.clap use_fd \"Param Name\" knob_x knob_y\n", argv[0]);
    return 2;
  }
  const char* path = argv[1];
  g_use_fd = atoi(argv[2]) != 0;
  std::string param_name = argv[3];
  int knob_x = atoi(argv[4]), knob_y = atoi(argv[5]);
  g_main_thread = std::this_thread::get_id();

  void* library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!library) {
    std::printf("dlopen failed: %s\n", dlerror());
    return 1;
  }
  auto* entry = (const clap_plugin_entry_t*)dlsym(library, "clap_entry");
  entry->init(path);
  auto* factory = (const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
  const clap_plugin_descriptor_t* descriptor = factory->get_plugin_descriptor(factory, 0);

  Plugin p;
  p.plugin = factory->create_plugin(factory, &g_host, descriptor->id);
  p.plugin->init(p.plugin);
  p.timers = (const clap_plugin_timer_support_t*)p.plugin->get_extension(p.plugin, CLAP_EXT_TIMER_SUPPORT);
  p.fds = (const clap_plugin_posix_fd_support_t*)p.plugin->get_extension(p.plugin, CLAP_EXT_POSIX_FD_SUPPORT);
  p.params = (const clap_plugin_params_t*)p.plugin->get_extension(p.plugin, CLAP_EXT_PARAMS);
  auto* gui = (const clap_plugin_gui_t*)p.plugin->get_extension(p.plugin, CLAP_EXT_GUI);

  clap_id param_id = CLAP_INVALID_ID;
  for (uint32_t i = 0; i < p.params->count(p.plugin); ++i) {
    clap_param_info_t info;
    p.params->get_info(p.plugin, i, &info);
    if (param_name == info.name)
      param_id = info.id;
  }
  if (param_id == CLAP_INVALID_ID) {
    std::printf("parameter '%s' not found\n", param_name.c_str());
    return 1;
  }

  Display* display = XOpenDisplay(nullptr);
  if (!display) {
    std::printf("no X display\n");
    return 1;
  }

  gui->create(p.plugin, CLAP_WINDOW_API_X11, false);
  uint32_t width = 0, height = 0;
  gui->get_size(p.plugin, &width, &height);
  Window parent = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, width, height, 0, 0, 0);
  XMapWindow(display, parent);
  XFlush(display);
  clap_window_t window = { CLAP_WINDOW_API_X11, { nullptr } };
  window.x11 = parent;
  gui->set_parent(p.plugin, &window);
  gui->show(p.plugin);
  std::printf("editor %ux%u, fd support %s, %zu fds registered\n", width, height, g_use_fd ? "on" : "off", g_fds.size());

  pumpFor(p, 4.0);
  Window editor = findEditorWindow(display, parent);
  if (!editor) {
    std::printf("no editor window accepting mouse input\n");
    return 1;
  }
  if (const char* shot = getenv("LATENCY_SCREENSHOT")) {
    std::string command = "import -window " + std::to_string(parent) + " " + shot;
    if (system(command.c_str()) != 0)
      std::printf("screenshot failed\n");
  }

  auto value = [&]() {
    double v = 0.0;
    p.params->get_value(p.plugin, param_id, &v);
    return v;
  };

  // Press the control, then move it one step at a time and time how long each mouse event takes
  // to reach it. The host loop sleeps in poll() between events like a real host would.
  sendMouse(display, editor, MotionNotify, knob_x, knob_y, 0, 0);
  pumpFor(p, 0.1);
  sendMouse(display, editor, ButtonPress, knob_x, knob_y, 0, Button1);
  pumpFor(p, 0.1);

  constexpr int kSteps = 40;
  double start_value = value();
  std::vector<double> latencies;
  int missed = 0;
  for (int step = 1; step <= kSteps; ++step) {
    int y = knob_y - 3 * step;
    double before = value();
    auto sent = Clock::now();
    sendMouse(display, editor, MotionNotify, knob_x, y, Button1Mask, 0);
    bool changed = false;
    while (Clock::now() - sent < std::chrono::milliseconds(200)) {
      runHostLoop(p, 1);
      if (value() != before) {
        changed = true;
        break;
      }
    }
    if (changed)
      latencies.push_back(std::chrono::duration<double, std::milli>(Clock::now() - sent).count());
    else
      ++missed;
    // Space the steps out by a random-ish amount so they don't line up with the plugin's timer.
    pumpFor(p, 0.013 + 0.007 * (step % 3));
  }
  sendMouse(display, editor, ButtonRelease, knob_x, knob_y - 3 * kSteps, Button1Mask, Button1);
  pumpFor(p, 0.3);

  std::sort(latencies.begin(), latencies.end());
  double sum = 0.0;
  for (double l : latencies)
    sum += l;
  bool moved = value() != start_value && !latencies.empty();
  if (moved) {
    std::printf("fd support %s: %zu of %d steps reached the control; latency avg %.1f ms, median %.1f ms, "
                "worst %.1f ms\n", g_use_fd ? "on " : "off", latencies.size(), kSteps, sum / latencies.size(),
                latencies[latencies.size() / 2], latencies.back());
  }
  else
    std::printf("the control did not move\n");

  gui->destroy(p.plugin);
  p.plugin->destroy(p.plugin);
  entry->deinit();
  XCloseDisplay(display);
  return moved ? 0 : 1;
}
