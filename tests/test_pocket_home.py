"""pocket-home integration tests against a mock NetworkManager on a private bus.

Expects build/Release/{pocket-home,wifitest} (see tests/run.sh).
"""

import os
import shutil
import subprocess
import time
from pathlib import Path

import pytest
from gi.repository import GLib

import mock_nm

ROOT = Path(__file__).resolve().parent.parent
BUILD = Path(os.environ.get("POCKET_HOME_BUILD", ROOT / "build" / "Release"))
STEP_MS = 700


@pytest.fixture(name="nm")
def fixture_nm():
    bus_proc, bus = mock_nm.start_bus()
    yield mock_nm.MockNetworkManager(bus)
    bus_proc.terminate()
    bus_proc.wait()


def drive(steps, settle_ms, pid=None, duration_ms=None):
    """Apply steps to the mock every STEP_MS until pid exits (returns its exit code) or
    duration_ms elapses."""
    loop = GLib.MainLoop()
    status = []
    for i, step in enumerate(steps):
        GLib.timeout_add(settle_ms + i * STEP_MS, lambda s=step: s() and False)
    if pid:
        GLib.child_watch_add(
            GLib.PRIORITY_DEFAULT, pid, lambda _, st: status.append(st) or loop.quit()
        )
    else:
        GLib.timeout_add(duration_ms, loop.quit)
    loop.run()
    return os.waitstatus_to_exitcode(status[0]) if status else None


def test_wifi_status_follows_signals(nm, tmp_path):
    steps = [
        lambda: nm.ap1.set(mock_nm.ACCESS_POINT, Strength=mock_nm.dbus.Byte(40)),
        lambda: nm.ap2.set(mock_nm.ACCESS_POINT, Strength=mock_nm.dbus.Byte(90)),
        lambda: nm.device.set(mock_nm.WIRELESS, LastScan=1),
        lambda: nm.set_connected(False),
        lambda: nm.root.set(mock_nm.NM, WirelessEnabled=False),
        lambda: nm.root.set(mock_nm.NM, WirelessEnabled=True),
        lambda: nm.set_connected(True),
        nm.release,
        nm.acquire,
    ]
    out = tmp_path / "out"
    with out.open("w") as f:
        seconds = 2 + len(steps) * STEP_MS // 1000 + 2
        proc = subprocess.Popen([str(BUILD / "wifitest"), "--monitor", str(seconds)], stdout=f)
        assert drive(steps, 1500, pid=proc.pid) == 0
    lines = [l for l in out.read_text().splitlines() if l and not l.startswith("Executing")]
    assert lines == [
        "initial enabled=1 connected=1 ssid=chipnet strength=70",
        "signal enabled=1 connected=1 ssid=chipnet strength=40",
        "disconnected enabled=1 connected=0 ssid= strength=0",
        "disabled enabled=0 connected=0 ssid= strength=0",
        "enabled enabled=1 connected=0 ssid= strength=0",
        "connected enabled=1 connected=1 ssid=chipnet strength=40",
        "disabled enabled=0 connected=0 ssid= strength=0",
        "disconnected enabled=0 connected=0 ssid= strength=0",
        "enabled enabled=1 connected=1 ssid=chipnet strength=40",
        "connected enabled=1 connected=1 ssid=chipnet strength=40",
    ]


def test_wifi_status_without_bus(tmp_path, monkeypatch):
    monkeypatch.setenv("DBUS_SYSTEM_BUS_ADDRESS", f"unix:path={tmp_path}/none")
    out = subprocess.run(
        [str(BUILD / "wifitest"), "--monitor", "1"],
        capture_output=True,
        text=True,
        timeout=30,
        check=True,
    ).stdout
    assert "initial enabled=0 connected=0 ssid= strength=0" in out


def execs(trace):
    """argv of every execve attempt, one per traced process."""
    argv = {}
    for line in trace.read_text().splitlines():
        pid, _, call = line.partition(" ")
        if call.lstrip().startswith("execve("):
            argv.setdefault(pid, call.split("[", 1)[1].split("]", 1)[0])
    return list(argv.values())


def zombies(pid):
    stats = subprocess.run(
        ["ps", "--ppid", pid, "-o", "stat=,args="], capture_output=True, text=True, check=False
    ).stdout
    return [l for l in stats.splitlines() if l.strip().startswith("Z")]


def test_launcher_idle_execs_and_reaping(nm, tmp_path):
    """Idle with wifi changes: no execs. Launching an app: child is reaped."""
    assets = tmp_path / "assets"
    shutil.copytree(ROOT / "assets", assets)
    config = (assets / "config.json").read_text()
    (assets / "config.json").write_text(config.replace('"shell": "lxterminal"', '"shell": "true"'))
    cwd = tmp_path / "a" / "b"
    cwd.mkdir(parents=True)
    env = dict(os.environ, DISPLAY=":42")
    trace = tmp_path / "trace"
    xvfb = subprocess.Popen(["Xvfb", ":42", "-screen", "0", "480x272x24"])
    time.sleep(1)
    try:
        proc = subprocess.Popen(
            ["strace", "-f", "-qq", "-e", "trace=execve", "-o", str(trace)]
            + [str(BUILD / "pocket-home")],
            cwd=cwd,
            env=env,
        )
        time.sleep(5)
        startup = execs(trace)
        app = subprocess.run(
            ["pgrep", "-P", str(proc.pid), "pocket-home"],
            capture_output=True,
            text=True,
            check=True,
        ).stdout.split()[0]
        drive(
            [
                lambda: nm.ap1.set(mock_nm.ACCESS_POINT, Strength=mock_nm.dbus.Byte(40)),
                lambda: nm.set_connected(False),
                lambda: nm.root.set(mock_nm.NM, WirelessEnabled=False),
                lambda: nm.root.set(mock_nm.NM, WirelessEnabled=True),
                lambda: nm.set_connected(True),
            ],
            1000,
            duration_ms=int(os.environ.get("IDLE_SECONDS", "30")) * 1000,
        )
        assert execs(trace) == startup, execs(trace)[len(startup) :]
        subprocess.run(
            ["xdotool", "mousemove", "118", "85", "sleep", "0.2", "click", "1"], env=env, check=True
        )
        time.sleep(3)
        launched = execs(trace)[len(startup) :]
        assert '"true"' in launched, launched
        assert not zombies(app)
    finally:
        subprocess.run(["pkill", "-KILL", "-P", str(proc.pid)], check=False)
        proc.wait()
        xvfb.terminate()
        xvfb.wait()
