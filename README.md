<div align = "center">
<img src=".github/obs-logo.svg" width="128" height="128" />
</div>


hyperstream-source
==============
Use your iPhone camera as a video source in OBS Studio and stream high quality video from your iPhone's camera over USB.

[![Build status](https://ci.appveyor.com/api/projects/status/ya6xt30mxfnvplna?svg=true)](https://ci.appveyor.com/project/wtsnz/hyperstream-source)
[![Build Status](https://travis-ci.org/wtsnz/hyperstream-source.svg?branch=master)](https://travis-ci.org/wtsnz/hyperstream-source)

To use this you use the [accompanying iOS app](https://will.townsend.io/products/obs-iphone/) to begin streaming in OBS.


## Downloads

Binaries for Windows and Mac are available in the [Releases](https://github.com/wtsnz/hyperstream-source/releases) section.

## Building

You can run the CI scripts to build it. They will clone and build OBS Studio prior to building this plugin.

    ./CI/install-dependencies-macos.sh
    ./CI/install-build-obs-macos.sh
    ./CI/build-macos.sh
    ./CI/package-macos.sh


## Special thanks
- The entire [obs-websockets](https://github.com/Palakis/obs-websocket) project for providing a stella example of an obs plugin build pipeline!