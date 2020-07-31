Hyperstream Plugin
==================

This plugin allows OBS to connect to your iPhone and receive video
from the Hyperstream app (currently in beta).

## Building

You can run the CI scripts to build it. They will clone and build OBS Studio prior to building this plugin.

    ./CI/install-dependencies-macos.sh
    ./CI/install-build-obs-macos.sh
    ./CI/build-macos.sh
    ./CI/package-macos.sh


## Special thanks

- Will Townsend, who created the original version of this plugin, to work with
	[his OBS Camera app](https://will.townsend.io/products/obs-iphone/).
- The entire [obs-websockets](https://github.com/Palakis/obs-websocket) project for providing a stella example of an obs plugin build pipeline!
