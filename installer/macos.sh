#!/bin/sh

# https://stackoverflow.com/a/246128
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

VERSION=$(cat "${SCRIPT_DIR}/../CMakeLists.txt" | grep VERSION | sed -n 2p | sed -e 's/VERSION//' -e 's/ //g')

PKG_DIR="${SCRIPT_DIR}/../dist"

INSTALLER_PATH="${PKG_DIR}/Scream_v${VERSION}.pkg"

# if [ -f $INSTALLER_PATH ]; then
#     echo "Error: ${INSTALLER_PATH} already exists! Please bump the version number or delete it the old file."
#     exit 1
# fi

echo "Building ${INSTALLER_PATH}"

echo "Removing old build folder"
rm -rf "${SCRIPT_DIR}/../build"
echo "Creating new build folder"
mkdir -p "${SCRIPT_DIR}/../build"
mkdir -p $PKG_DIR

echo "Configuring CMake"
cmake --no-warn-unused-cli \
      -DCMAKE_BUILD_TYPE:STRING=Release \
      -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE \
      -S${SCRIPT_DIR}/../ \
      -B${SCRIPT_DIR}/../build \
      -G Ninja

if [ $? != 0 ]; then
    echo "Failed to configure CMake"
    exit 1
fi

cmake --build "${SCRIPT_DIR}/../build" --config Release --target all --
if [ $? != 0 ]; then
    echo "Failed to compile release build"
    exit 1
fi

# If/when tests exist, run them here
# echo "Running tests"
# ${SCRIPT_DIR}/../build/Release/tests
# if [ $? != 0 ]
# then
#     echo "Failed tests"
#     exit 1
# fi

echo "Backing up binaries with debug symbols"
xcrun dsymutil "${SCRIPT_DIR}/../build/Release/Scream.component/Contents/MacOS/Scream" -o "${PKG_DIR}/Scream_v${VERSION}_auv2.dSYM"
xcrun dsymutil "${SCRIPT_DIR}/../build/Release/Scream.clap/Contents/MacOS/Scream" -o "${PKG_DIR}/Scream_v${VERSION}_clap.dSYM"
xcrun dsymutil "${SCRIPT_DIR}/../build/Release/Scream.vst3/Contents/MacOS/Scream" -o "${PKG_DIR}/Scream_v${VERSION}_vst3.dSYM"

# If this wasn't open source, I would strip the symbols here
# echo "Stripping symbols"
# strip -x "${SCRIPT_DIR}/../build/Release/Scream.clap/Contents/MacOS/Scream"
# strip -x "${SCRIPT_DIR}/../build/Release/Scream.component/Contents/MacOS/Scream"
# strip -x "${SCRIPT_DIR}/../build/Release/Scream.vst3/Contents/MacOS/Scream"

#======================================

echo "Signing"

if [ -n "$DEVELOPER_ID_APPLICATION" ]; then
    codesign --force -s "${DEVELOPER_ID_APPLICATION}" -v "${SCRIPT_DIR}/../build/Release/Scream.clap"      --strict --options=runtime --timestamp
    codesign --force -s "${DEVELOPER_ID_APPLICATION}" -v "${SCRIPT_DIR}/../build/Release/Scream.component" --strict --options=runtime --timestamp
    codesign --force -s "${DEVELOPER_ID_APPLICATION}" -v "${SCRIPT_DIR}/../build/Release/Scream.vst3"      --strict --options=runtime --timestamp

    codesign -dv --verbose=4 "${SCRIPT_DIR}/../build/Release/Scream.clap"
    codesign -dv --verbose=4 "${SCRIPT_DIR}/../build/Release/Scream.component"
    codesign -dv --verbose=4 "${SCRIPT_DIR}/../build/Release/Scream.vst3"
else
    echo "WARNING: environment variable \"DEVELOPER_ID_APPLICATION\" not found. Skipping code signing packages..."
fi

#======================================

echo "Building pkgs"

# Note: when installing static files to user directories, you have to first install to /tmp/custom-path
# then using a postinstall script, copy the files from /tmp/custom-path to the user directory(s) and
# delete the /tmp/custom-path
# Sending paths like "~/Desktop" to pkgbuild will cause the installer to create user directories on the users computer
# with YOUR username. This is embarrasing!

# Your installer WILL fail if execution permissions arent set 
chmod +x "${SCRIPT_DIR}/scripts/postinstall"

mkdir -p "${SCRIPT_DIR}/../build/installer_assets/"
cp "${SCRIPT_DIR}/../assets/Tomorrow-SemiBold.ttf" "${SCRIPT_DIR}/../build/installer_assets/"
cp "${SCRIPT_DIR}/../assets/OFL.txt"               "${SCRIPT_DIR}/../build/installer_assets/"

pkgbuild --root "${SCRIPT_DIR}/../build/installer_assets/" \
         --identifier "com.CureAudio.Scream.pkg.assets" \
         --version $VERSION \
         --install-location "/tmp/Scream-installer/Scream" \
         --scripts ${SCRIPT_DIR}/scripts \
         ${PKG_DIR}/Scream_assets.pkg

pkgbuild --root "${SCRIPT_DIR}/../build/Release/Scream.component" \
         --identifier com.CureAudio.Scream.pkg.au \
         --version $VERSION \
         --install-location "/Library/Audio/Plug-Ins/Components/Scream.component" \
         ${PKG_DIR}/Scream_au.pkg

pkgbuild --root "${SCRIPT_DIR}/../build/Release/Scream.clap" \
         --identifier com.CureAudio.Scream.pkg.clap \
         --version $VERSION \
         --install-location "/Library/Audio/Plug-Ins/CLAP/Scream.clap" \
         ${PKG_DIR}/Scream_clap.pkg

pkgbuild --root "${SCRIPT_DIR}/../build/Release/Scream.vst3" \
         --identifier com.CureAudio.Scream.pkg.vst3 \
         --version $VERSION \
         --install-location "/Library/Audio/Plug-Ins/VST3/Scream.vst3" \
         ${PKG_DIR}/Scream_vst3.pkg

# https://developer.apple.com/library/archive/documentation/DeveloperTools/Reference/DistributionDefinitionRef/Chapters/Distribution_XML_Ref.html
# https://forum.juce.com/t/vst-installer/16654/15
# https://github.com/surge-synthesizer/surge/blob/main/scripts/installer_mac/make_installer.sh
# Background images appear to be around 1280x832 - 1280x840.
# Set scaling="proportional" to resize the image to always fit
# http://shanekirk.com/2013/10/creating-flat-packages-in-osx/>
# https://ofek.dev/words/guides/2025-05-13-distributing-command-line-tools-for-macos/

cp "${SCRIPT_DIR}/../LICENSE" "${SCRIPT_DIR}"

cat > ${PKG_DIR}/distribution.xml << XMLEND
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="1">
    <title>Scream ${VERSION}</title>
    <license file="LICENSE"/>
    <background file="_macOS_installer_background.png" mime-type="image/png" scaling="proportional" />
    <pkg-ref id="com.CureAudio.Scream.pkg.assets"/>
    <pkg-ref id="com.CureAudio.Scream.pkg.au"/>
    <pkg-ref id="com.CureAudio.Scream.pkg.clap"/>
    <pkg-ref id="com.CureAudio.Scream.pkg.vst3"/>
    <options require-scripts="false" customize="always" />
    <choices-outline>
        <line choice="com.CureAudio.Scream.pkg.assets"/>
        <line choice="com.CureAudio.Scream.pkg.au"/>
        <line choice="com.CureAudio.Scream.pkg.clap"/>
        <line choice="com.CureAudio.Scream.pkg.vst3"/>
    </choices-outline>
    <choice id="com.CureAudio.Scream.pkg.assets" visible="true" start_selected="true" title="Required assets" enabled="false">
        <pkg-ref id="com.CureAudio.Scream.pkg.assets"/>
    </choice>
    <pkg-ref id="com.CureAudio.Scream.pkg.assets" version="${VERSION}">Scream_assets.pkg</pkg-ref>
    <choice id="com.CureAudio.Scream.pkg.au" visible="true" start_selected="true" title="Audio Unit (v2)">
        <pkg-ref id="com.CureAudio.Scream.pkg.au"/>
    </choice>
    <pkg-ref id="com.CureAudio.Scream.pkg.au" version="${VERSION}">Scream_au.pkg</pkg-ref>
    <choice id="com.CureAudio.Scream.pkg.clap" visible="true" start_selected="true" title="CLAP">
        <pkg-ref id="com.CureAudio.Scream.pkg.clap"/>
    </choice>
    <pkg-ref id="com.CureAudio.Scream.pkg.clap" version="${VERSION}">Scream_clap.pkg</pkg-ref>
    <choice id="com.CureAudio.Scream.pkg.vst3" visible="true" start_selected="true" title="VST3">
        <pkg-ref id="com.CureAudio.Scream.pkg.vst3"/>
    </choice>
    <pkg-ref id="com.CureAudio.Scream.pkg.vst3" version="${VERSION}">Scream_vst3.pkg</pkg-ref>
</installer-gui-script>
XMLEND

echo "Running productbuild..."

if [ -n "$DEVELOPER_ID_INSTALLER" ]; then
    productbuild --distribution ${PKG_DIR}/distribution.xml \
                 --package-path ${PKG_DIR} \
                 --resources ${SCRIPT_DIR} \
                 --sign "${DEVELOPER_ID_INSTALLER}" \
                 "${INSTALLER_PATH}"
else
    echo "WARNING: environment variable \"DEVELOPER_ID_INSTALLER\" not found. Building installer without signature..."
    productbuild --distribution ${PKG_DIR}/distribution.xml \
                 --package-path ${PKG_DIR} \
                 --resources ${SCRIPT_DIR} \
                 "${INSTALLER_PATH}"
fi

if [ $? != 0 ]; then
    echo "Failed productbuild"
    exit 1
fi

if [ -n "$DEVELOPER_ID_INSTALLER" ]; then
    pkgutil --check-signature "${INSTALLER_PATH}"
    if [ $? != 0 ]; then
        echo "Failed pkgutil"
        exit 1
    fi
fi

#======================================

# Submit for notorisation
# NOTE if this fails, try debugging using this
# xcrun notarytool log {GUID} --apple-id ${APPLE_ID} --team-id ${TEAM_ID} log.json # --password {APP_PASSWORD}
# https://melatonin.dev/blog/how-to-code-sign-and-notarize-macos-audio-plugins-in-ci/
if [[ -n "$APPLE_ID" && -n "$TEAM_ID" ]]; then
    echo "Running notarytool..."
    xcrun notarytool submit "${INSTALLER_PATH}" --apple-id "${APPLE_ID}" --team-id "${TEAM_ID}" --wait

    echo "Running stapler..."
    xcrun stapler staple "${INSTALLER_PATH}"

    # verify notorisation
    echo "Running spctl..."
    spctl --verbose --assess --type install "${INSTALLER_PATH}"
else
    echo "WARNING: environment variable \"APPLE_ID\" and/or \"TEAM_ID\" not found. Skipping notorisation..."
fi

echo "Done!"