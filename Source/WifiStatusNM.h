#pragma once

#ifdef LINUX

#include "Utils.h"
#include "WifiStatus.h"
#include "../JuceLibraryCode/JuceHeader.h"

typedef struct _GMainContext GMainContext;
typedef struct _GMainLoop GMainLoop;

/*
 * NetworkManager-backed wifi status. Radio, connection and signal state come
 * from NetworkManager's D-Bus signals, handled on a GDBus thread and delivered
 * to listeners on the message thread. Configuration (radio toggle, scanning,
 * PSKs, connecting) is delegated to nmcli/nmtui on user action.
 */
class WifiStatusNM
: public WifiStatus
, private Thread
, private AsyncUpdater {
public:
  WifiStatusNM();
  ~WifiStatusNM() override;

  OwnedArray<WifiAccessPoint> nearbyAccessPoints() override;
  ScopedPointer<WifiAccessPoint> connectedAccessPoint() const override;
  bool isEnabled() const override;
  bool isConnected() const override;

  void addListener(Listener* listener) override;
  void clearListeners() override;

  void setEnabled() override;
  void setDisabled() override;
  void setConnectedAccessPoint(WifiAccessPoint *ap, String psk = String::empty) override;
  void setDisconnected() override;

  void initializeStatus() override;

  struct State {
    bool enabled = false;
    bool connected = false;
    String ssid;
    int signalStrength = 0;
  };

  void setPending(const State &state);

private:
  void run() override;
  void handleAsyncUpdate() override;

  Array<Listener *> listeners;
  CriticalSection pendingLock;
  State pending, current;
  GMainContext *context;
  GMainLoop *loop;
  WaitableEvent ready;
};

#endif // LINUX
