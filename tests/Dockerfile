FROM debian:trixie
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential pkg-config \
        libx11-dev libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev \
        libfreetype6-dev libasound2-dev libi2c-dev libglib2.0-dev \
        xvfb xdotool strace procps dbus-daemon python3-dbus python3-gi python3-pytest \
    && rm -rf /var/lib/apt/lists/*
