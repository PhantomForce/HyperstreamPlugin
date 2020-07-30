#!/bin/sh

set -e

OSTYPE=$(uname)

if [ "${OSTYPE}" != "Darwin" ]; then
    echo "[hyperstream-plugin - Error] macOS package script can be run on Darwin-type OS only."
    exit 1
fi

echo "[hyperstream-plugin] Preparing package build"

GIT_HASH=$(git rev-parse --short HEAD)
GIT_BRANCH_OR_TAG=$(git name-rev --name-only HEAD | awk -F/ '{print $NF}')

VERSION="$GIT_HASH-$GIT_BRANCH_OR_TAG"
LATEST_VERSION="$GIT_BRANCH_OR_TAG"

FILENAME_UNSIGNED="hyperstream-source-$VERSION-Unsigned.pkg"
FILENAME="hyperstream-source-$VERSION.pkg"

echo "-- Modifying hyperstream-source.so"
install_name_tool \
	-change /usr/local/opt/ffmpeg/lib/libavcodec.58.dylib @rpath/libavcodec.58.dylib \
	-change /usr/local/opt/ffmpeg/lib/libavutil.56.dylib @rpath/libavutil.56.dylib \
	-change /tmp/obsdeps/bin/libavcodec.58.dylib @rpath/libavcodec.58.dylib \
	-change /tmp/obsdeps/bin/libavutil.56.dylib @rpath/libavutil.56.dylib \
	./build/hyperstream-source.so

echo "-- Dependencies for hyperstream-source"
otool -L ./build/hyperstream-source.so

if [[ "$RELEASE_MODE" == "True" ]]; then
	echo "-- Signing plugin binary: hyperstream-source.so"
	codesign --sign "$CODE_SIGNING_IDENTITY" ./build/hyperstream-source.so
else
	echo "-- Skipped plugin codesigning"
fi

echo "-- Actual package build"
packagesbuild ./CI/macos/hyperstream-source.pkgproj

echo "-- Renaming hyperstream-source.pkg to $FILENAME_UNSIGNED"
# mkdir release
mv ./release/hyperstream-source.pkg ./release/$FILENAME_UNSIGNED

if [[ "$RELEASE_MODE" == "True" ]]; then
	echo "[hyperstream-source] Signing installer: $FILENAME"
	productsign \
		--sign "$INSTALLER_SIGNING_IDENTITY" \
		./release/$FILENAME_UNSIGNED \
		./release/$FILENAME

	echo "[hyperstream-source] Submitting installer $FILENAME for notarization"
	zip -r ./release/$FILENAME.zip ./release/$FILENAME
	UPLOAD_RESULT=$(xcrun altool \
		--notarize-app \
		--primary-bundle-id "io.loftlabs.hyperstream-source.pkg" \
		--username "$AC_USERNAME" \
		--password "$AC_PASSWORD" \
		--asc-provider "$AC_PROVIDER_SHORTNAME" \
		--file "./release/$FILENAME.zip")
	rm ./release/$FILENAME.zip

	REQUEST_UUID=$(echo $UPLOAD_RESULT | awk -F ' = ' '/RequestUUID/ {print $2}')
	echo "Request UUID: $REQUEST_UUID"

	echo "[hyperstream-source] Wait for notarization result"
	# Pieces of code borrowed from rednoah/notarized-app
	while sleep 30 && date; do
		CHECK_RESULT=$(xcrun altool \
			--notarization-info "$REQUEST_UUID" \
			--username "$AC_USERNAME" \
			--password "$AC_PASSWORD" \
			--asc-provider "$AC_PROVIDER_SHORTNAME")
		echo $CHECK_RESULT

		if ! grep -q "Status: in progress" <<< "$CHECK_RESULT"; then
			echo "[hyperstream-source] Staple ticket to installer: $FILENAME"
			xcrun stapler staple ./release/$FILENAME
			break
		fi
	done
else
	echo "[hyperstream-source] Skipped installer codesigning and notarization"
fi