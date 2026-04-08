# smol-engine

### Android cheat sheet for Arch Linux

install tooling and SDK
```bash
sudo pacman -S android-tools
yay -S android-ndk android-sdk-cmdline-tools-latest

sudo chown -R $USER:$USER /opt/android-sdk

yes | /opt/android-sdk/cmdline-tools/latest/bin/sdkmanager "build-tools;34.0.0" "platforms;android-34"
```

Generate debug keystore:
```bash
keytool -genkeypair -v -keystore debug.keystore -storepass android -alias androiddebugkey -keypass android -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=EU"
```

xmake configure command:
```bash
xmake f -p android -a arm64-v8a --ndk=/opt/android-ndk -m debug --standalone=y --runtimes=c++_shared
```

Android build only works on Arch Linux, currently using absolute path to JDK 17.