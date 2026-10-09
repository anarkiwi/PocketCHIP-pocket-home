#pragma once

#include "../JuceLibraryCode/JuceHeader.h"

#include "BatteryMonitor.h"
#include "LauncherBarComponent.h"
#include "PageStackComponent.h"
#include "WifiStatus.h"

class LauncherComponent;
class AppsPageComponent;

class BatteryIconTimer : public Timer {
public:
    BatteryIconTimer() {};
    void timerCallback();
    LauncherComponent* launcherComponent;
};

class WifiIconListener : public WifiStatus::Listener {
public:
  WifiIconListener(){};
  void update();
  void handleWifiEnabled() override {
    update();
  }
  void handleWifiDisabled() override {
    update();
  }
  void handleWifiConnected() override {
    update();
  }
  void handleWifiDisconnected() override {
    update();
  }
  void handleWifiSignalChanged() override {
    update();
  }
  LauncherComponent* launcherComponent;
};

class LauncherComponent : public Component, private Button::Listener {
public:
    BatteryMonitor batteryMonitor;
    ScopedPointer<LauncherBarComponent> botButtons;
    ScopedPointer<LauncherBarComponent> topButtons;

    Array<Image> batteryIconImages;
    Array<Image> batteryIconChargingImages;
    Array<Image> wifiIconImages;

    BatteryIconTimer batteryIconTimer;
    WifiIconListener wifiIconListener;
    Component* defaultPage;
  
    // FIXME: we have no need for the pages/pagesByName if we're using scoped pointers for each page.
    // All these variables do is add an extra string key the compiler can't see through.
    OwnedArray<Component> pages;
    ScopedPointer<PageStackComponent> pageStack;
    HashMap<String, Component *> pagesByName;
    
    bool resize = false;
    
    StretchableLayoutManager categoryButtonLayout;
    
    LauncherComponent(const var &configJson);
    ~LauncherComponent();
    
    void paint(Graphics &) override;
    void resized() override;

private:
    Colour bgColor;
    Image bgImage;
  
    void buttonClicked(Button *) override;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LauncherComponent)
};