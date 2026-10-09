#ifdef LINUX

#include <gio/gio.h>
#include <memory>

#include "WifiStatus.h"
#include "../JuceLibraryCode/JuceHeader.h"

namespace {

// Run a command, capture stdout. Blocks -- the nmcli queries here are quick.
// ChildProcess::start(String) tokenises on whitespace and execs directly (no
// shell); none of our args contain spaces.
String runCapture(const String& command) {
  ChildProcess proc;
  if (!proc.start(command, ChildProcess::wantStdOut))
    return String::empty;
  const String out = proc.readAllProcessOutput();
  proc.waitForProcessToFinish(5000);
  return out;
}

// Fire-and-forget launch (e.g. a terminal running nmtui). JUCE's ChildProcess
// does not kill the child on destruction, so the launched process keeps running.
void launchDetached(const String& command) {
  ChildProcess proc;
  proc.start(command, 0);
}

// `nmcli -t` terse output escapes ':' inside a field as '\:'. We always put the
// SSID (the only field that can contain ':') LAST and grab the remainder, so the
// leading fixed fields parse cleanly. Returns leadingFields + 1 entries.
StringArray splitTerse(const String& line, int leadingFields) {
  StringArray out;
  String rest = line;
  for (int i = 0; i < leadingFields; ++i) {
    int sep = -1;
    for (int c = 0; c < rest.length(); ++c) {
      if (rest[c] == '\\') { ++c; continue; }      // skip escaped char
      if (rest[c] == ':') { sep = c; break; }
    }
    if (sep < 0) { out.add(rest); rest = String::empty; }
    else { out.add(rest.substring(0, sep)); rest = rest.substring(sep + 1); }
  }
  out.add(rest.replace("\\:", ":"));               // remainder = SSID
  return out;
}

const char *const nmName = "org.freedesktop.NetworkManager";
const char *const nmPath = "/org/freedesktop/NetworkManager";
const char *const nmDevice = "org.freedesktop.NetworkManager.Device";
const char *const nmWireless = "org.freedesktop.NetworkManager.Device.Wireless";
const char *const nmAccessPoint = "org.freedesktop.NetworkManager.AccessPoint";
const guint32 deviceTypeWifi = 2;
const guint32 deviceStateActivated = 100;

using Variant = std::unique_ptr<GVariant, decltype(&g_variant_unref)>;

Variant
getProperty(GDBusConnection *bus, const String &path, const char *iface, const char *name) {
  GVariant *value = nullptr;
  GVariant *reply = g_dbus_connection_call_sync(bus,
                                                nmName,
                                                path.toRawUTF8(),
                                                "org.freedesktop.DBus.Properties",
                                                "Get",
                                                g_variant_new("(ss)", iface, name),
                                                G_VARIANT_TYPE("(v)"),
                                                G_DBUS_CALL_FLAGS_NO_AUTO_START,
                                                2000,
                                                nullptr,
                                                nullptr);
  if (reply) {
    g_variant_get(reply, "(v)", &value);
    g_variant_unref(reply);
  }
  return Variant(value, g_variant_unref);
}

struct NMMonitor {
  WifiStatusNM *owner;
  GDBusConnection *bus;
  String device, accessPoint;

  void refresh() {
    WifiStatusNM::State state;
    device = accessPoint = String::empty;
    Variant enabled = getProperty(bus, nmPath, nmName, "WirelessEnabled");
    state.enabled = enabled && g_variant_get_boolean(enabled.get());
    Variant devices = getProperty(bus, nmPath, nmName, "Devices");
    for (gsize i = 0; devices && i < g_variant_n_children(devices.get()); ++i) {
      Variant path(g_variant_get_child_value(devices.get(), i), g_variant_unref);
      const String p = g_variant_get_string(path.get(), nullptr);
      Variant type = getProperty(bus, p, nmDevice, "DeviceType");
      if (type && g_variant_get_uint32(type.get()) == deviceTypeWifi) {
        device = p;
        break;
      }
    }
    if (device.isNotEmpty()) {
      Variant devState = getProperty(bus, device, nmDevice, "State");
      Variant ap = getProperty(bus, device, nmWireless, "ActiveAccessPoint");
      if (devState && g_variant_get_uint32(devState.get()) == deviceStateActivated && ap)
        accessPoint = g_variant_get_string(ap.get(), nullptr);
    }
    if (accessPoint.isNotEmpty() && accessPoint != "/") {
      Variant ssid = getProperty(bus, accessPoint, nmAccessPoint, "Ssid");
      Variant strength = getProperty(bus, accessPoint, nmAccessPoint, "Strength");
      gsize len = 0;
      const char *raw =
          ssid ? (const char *)g_variant_get_fixed_array(ssid.get(), &len, 1) : nullptr;
      state.connected = true;
      state.ssid = String::fromUTF8(raw, (int)len);
      state.signalStrength = strength ? g_variant_get_byte(strength.get()) : 0;
    }
    owner->setPending(state);
  }

  bool relevant(const String &path, const gchar *member, GVariant *params) const {
    if (path != nmPath && path != device && path != accessPoint) return false;
    if (strcmp(member, "PropertiesChanged") != 0)
      return strcmp(member, "StateChanged") == 0 || strcmp(member, "DeviceAdded") == 0 ||
             strcmp(member, "DeviceRemoved") == 0;
    if (!g_variant_is_of_type(params, G_VARIANT_TYPE("(sa{sv}as)"))) return false;
    Variant changed(g_variant_get_child_value(params, 1), g_variant_unref);
    for (auto key :
         { "WirelessEnabled", "Devices", "State", "ActiveAccessPoint", "Ssid", "Strength" })
      if (Variant(g_variant_lookup_value(changed.get(), key, nullptr), g_variant_unref))
        return true;
    return false;
  }
};

void
onSignal(GDBusConnection *,
         const gchar *,
         const gchar *path,
         const gchar *,
         const gchar *member,
         GVariant *params,
         gpointer data) {
  auto monitor = static_cast<NMMonitor *>(data);
  if (monitor->relevant(path, member, params)) monitor->refresh();
}

void
onNameOwner(GDBusConnection *, const gchar *, const gchar *, gpointer data) {
  static_cast<NMMonitor *>(data)->refresh();
}

void
onNameLost(GDBusConnection *, const gchar *, gpointer data) {
  auto monitor = static_cast<NMMonitor *>(data);
  monitor->device = monitor->accessPoint = String::empty;
  monitor->owner->setPending(WifiStatusNM::State());
}

} // namespace

WifiStatusNM::WifiStatusNM()
: Thread("WifiStatusNM"), context(g_main_context_new()), loop(g_main_loop_new(context, FALSE)) {}

WifiStatusNM::~WifiStatusNM() {
  if (isThreadRunning()) {
    GSource *quit = g_idle_source_new();
    g_source_set_callback(
        quit,
        [](gpointer l) {
          g_main_loop_quit(static_cast<GMainLoop *>(l));
          return G_SOURCE_REMOVE;
        },
        loop,
        nullptr);
    g_source_attach(quit, context);
    g_source_unref(quit);
    stopThread(-1);
  }
  g_main_loop_unref(loop);
  g_main_context_unref(context);
}

void
WifiStatusNM::run() {
  g_main_context_push_thread_default(context);
  GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, nullptr);
  if (bus) {
    NMMonitor monitor{ this, bus };
    const guint sub = g_dbus_connection_signal_subscribe(bus,
                                                         nmName,
                                                         nullptr,
                                                         nullptr,
                                                         nullptr,
                                                         nullptr,
                                                         G_DBUS_SIGNAL_FLAGS_NONE,
                                                         onSignal,
                                                         &monitor,
                                                         nullptr);
    const guint watch = g_bus_watch_name_on_connection(
        bus, nmName, G_BUS_NAME_WATCHER_FLAGS_NONE, onNameOwner, onNameLost, &monitor, nullptr);
    g_main_loop_run(loop);
    g_bus_unwatch_name(watch);
    g_dbus_connection_signal_unsubscribe(bus, sub);
    g_object_unref(bus);
  } else {
    setPending(State());
  }
  g_main_context_pop_thread_default(context);
}

void
WifiStatusNM::setPending(const State &state) {
  {
    const ScopedLock lock(pendingLock);
    pending = state;
  }
  triggerAsyncUpdate();
  ready.signal();
}

void
WifiStatusNM::handleAsyncUpdate() {
  const State prev = current;
  {
    const ScopedLock lock(pendingLock);
    current = pending;
  }
  if (current.enabled != prev.enabled)
    for (auto l : listeners) current.enabled ? l->handleWifiEnabled() : l->handleWifiDisabled();
  if (current.connected != prev.connected)
    for (auto l : listeners)
      current.connected ? l->handleWifiConnected() : l->handleWifiDisconnected();
  else if (current.ssid != prev.ssid || current.signalStrength != prev.signalStrength)
    for (auto l : listeners) l->handleWifiSignalChanged();
}

OwnedArray<WifiAccessPoint> WifiStatusNM::nearbyAccessPoints() {
  OwnedArray<WifiAccessPoint> aps;
  // SIGNAL,SECURITY then SSID last. (Connecting still happens in nmtui; this
  // list is just for display.)
  const String out = runCapture("nmcli -t -f SIGNAL,SECURITY,SSID device wifi list");
  const StringArray lines = StringArray::fromLines(out);
  StringArray seen;
  for (int i = 0; i < lines.size(); ++i) {
    if (lines[i].isEmpty()) continue;
    const StringArray f = splitTerse(lines[i], 2);     // SIGNAL, SECURITY, SSID
    const String ssid = f[2].trim();
    if (ssid.isEmpty() || seen.contains(ssid)) continue;
    seen.add(ssid);
    WifiAccessPoint* ap = new WifiAccessPoint();
    ap->ssid = ssid;
    ap->signalStrength = f[0].getIntValue();
    const String sec = f[1].trim();
    ap->requiresAuth = sec.isNotEmpty() && sec != "--";
    ap->hash = ssid;
    aps.add(ap);
  }
  return aps;
}

ScopedPointer<WifiAccessPoint> WifiStatusNM::connectedAccessPoint() const {
  if (!current.connected) return nullptr;
  return ScopedPointer<WifiAccessPoint>{ new WifiAccessPoint{
      current.ssid, current.signalStrength, false, current.ssid } };
}

bool
WifiStatusNM::isEnabled() const {
  return current.enabled;
}
bool
WifiStatusNM::isConnected() const {
  return current.connected;
}

void WifiStatusNM::addListener(Listener* listener) { listeners.add(listener); }
void WifiStatusNM::clearListeners() { listeners.clear(); }

void WifiStatusNM::setEnabled()  { launchDetached("nmcli radio wifi on"); }
void WifiStatusNM::setDisabled() { launchDetached("nmcli radio wifi off"); }

void WifiStatusNM::setConnectedAccessPoint(WifiAccessPoint* /*ap*/, String /*psk*/) {
  // All connection management goes through nmtui (in a terminal), per design.
  launchDetached("x-terminal-emulator -e nmtui");
}

void WifiStatusNM::setDisconnected() {
  launchDetached("x-terminal-emulator -e nmtui");
}

void WifiStatusNM::initializeStatus() {
  startThread();
  if (ready.wait(2000)) {
    cancelPendingUpdate();
    handleAsyncUpdate();
  }
}

#endif // LINUX
