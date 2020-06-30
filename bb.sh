rm -rf build
./CI/build-macos.sh
sudo cp build/obs-ios-camera-source.so /Library/Application\ Support/obs-studio/plugins/obs-ios-camera-source/bin
