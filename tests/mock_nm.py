#!/usr/bin/env python3
"""Minimal mock of the NetworkManager D-Bus objects pocket-home reads."""

import os
import subprocess

import dbus
import dbus.mainloop.glib
import dbus.service

NM = "org.freedesktop.NetworkManager"
NM_PATH = "/org/freedesktop/NetworkManager"
DEVICE = NM + ".Device"
WIRELESS = DEVICE + ".Wireless"
ACCESS_POINT = NM + ".AccessPoint"
PROPS = "org.freedesktop.DBus.Properties"
DEV_PATH = NM_PATH + "/Devices/1"
AP1_PATH = NM_PATH + "/AccessPoint/1"
AP2_PATH = NM_PATH + "/AccessPoint/2"


class PropsObject(dbus.service.Object):
    """D-Bus object exposing org.freedesktop.DBus.Properties over a dict."""

    def __init__(self, bus, path, props):
        super().__init__(bus, path)
        self.props = props

    @dbus.service.method(PROPS, in_signature="ss", out_signature="v")
    def Get(self, iface, name):  # pylint: disable=invalid-name
        return self.props[iface][name]

    @dbus.service.method(PROPS, in_signature="s", out_signature="a{sv}")
    def GetAll(self, iface):  # pylint: disable=invalid-name
        return self.props.get(iface, {})

    @dbus.service.signal(PROPS, signature="sa{sv}as")
    def PropertiesChanged(self, iface, changed, invalidated):  # pylint: disable=invalid-name
        """Emitted by set()."""

    def set(self, iface, **changed):
        self.props[iface].update(changed)
        self.PropertiesChanged(iface, changed, [])


def ssid(name):
    return dbus.Array(name.encode(), signature="y")


class MockNetworkManager:
    """Root, one wifi device and two access points; AP1 is active."""

    def __init__(self, bus):
        self.bus = bus
        self.root = PropsObject(
            bus,
            NM_PATH,
            {NM: {"WirelessEnabled": True, "Devices": dbus.Array([DEV_PATH], signature="o")}},
        )
        self.device = PropsObject(
            bus,
            DEV_PATH,
            {
                DEVICE: {"DeviceType": dbus.UInt32(2), "State": dbus.UInt32(100)},
                WIRELESS: {"ActiveAccessPoint": dbus.ObjectPath(AP1_PATH), "LastScan": 0},
            },
        )
        self.ap1 = PropsObject(
            bus, AP1_PATH, {ACCESS_POINT: {"Ssid": ssid("chipnet"), "Strength": dbus.Byte(70)}}
        )
        self.ap2 = PropsObject(
            bus, AP2_PATH, {ACCESS_POINT: {"Ssid": ssid("other"), "Strength": dbus.Byte(20)}}
        )
        self.acquire()

    def acquire(self):
        self.bus.request_name(NM, dbus.bus.NAME_FLAG_DO_NOT_QUEUE)

    def release(self):
        self.bus.release_name(NM)

    def set_connected(self, connected):
        self.device.set(DEVICE, State=dbus.UInt32(100 if connected else 30))
        self.device.set(WIRELESS, ActiveAccessPoint=dbus.ObjectPath(AP1_PATH if connected else "/"))


def start_bus():
    """Start a private dbus-daemon to stand in for the system bus; returns (proc, address)."""
    proc = subprocess.Popen(
        ["dbus-daemon", "--session", "--nofork", "--print-address=1"],
        stdout=subprocess.PIPE,
        text=True,
    )
    address = proc.stdout.readline().strip()
    os.environ["DBUS_SYSTEM_BUS_ADDRESS"] = address
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    return proc, dbus.bus.BusConnection(address)
