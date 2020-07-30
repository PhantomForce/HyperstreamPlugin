rm -rf build
./CI/build-macos.sh
sudo cp build/hyperstream-source.so /Library/Application\ Support/obs-studio/plugins/hyperstream-source/bin
