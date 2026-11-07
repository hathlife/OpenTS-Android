# The Android shell

This directory packages the engine as an APK. It does not build the engine: CMake with the
NDK toolchain produces `libOpenTS.so` and `libSDL3.so` under `build/android/`, and Gradle
copies them from there.

## Languages

| Part | Language | Owner |
| --- | --- | --- |
| `app/src/main/java/net/opents/android/` | Kotlin | OpenTS |
| `app/src/main/java/org/libsdl/app/` | Java | SDL, vendored |

`org.libsdl.app` is copied from `thirdparty/SDL/android-project`. SDL's native code binds to
those classes by name, down to individual fields and method signatures, so **do not translate
them or rename their members**. When SDL is upgraded, replace that directory from the
submodule rather than editing it.

## Building

Run these from the repository root:

```powershell
cmake -S . -B build/android -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=<ndk>/build/cmake/android.toolchain.cmake `
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DCMAKE_BUILD_TYPE=Release
cmake --build build/android
gradle -p android assembleDebug
```

The APK is written to `android/app/build/outputs/apk/debug/app-debug.apk`.

## Running

The engine needs Tiberian Sun's game data, which the APK does not ship. It looks for it in
`/data/data/net.opents.android/files/data` and unpacks its own `ui/` documents from the APK
assets into `files/ui` on first launch.

`saves/` beside it holds `SUN.INI`, saved games, and screenshots. The engine writes nothing
outside that directory.

## What the shell does

`OpenTSActivity` extends SDL's `SDLActivity` and does three things:

1. names the libraries to load, in order, with the engine last so SDL finds `SDL_main` in it;
2. unpacks `assets/ui` into the files directory before SDL starts;
3. nothing else — SDL owns the window, input, and the game's thread.
