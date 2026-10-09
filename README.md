# PocketCHIP Launcher

### Required Packages

```sh
sudo apt-get install \
    git \
    build-essential \
    libx11-dev \
    libxrandr-dev \
    libxcursor-dev \
    libxft-dev \
    libxinerama-dev \
    libglib2.0-dev \
    libi2c-dev \
    libasound2-dev 
```

### Tests

`tests/run.sh` builds natively in Docker and runs the integration tests against a mock
NetworkManager D-Bus service.
