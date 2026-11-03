# Android dependencies

- SDL 3.2.28 source: https://github.com/libsdl-org/SDL/releases/tag/release-3.2.28
- Archive: `third_party/SDL3/android-source.zip`, SHA256 `24a30069af514a6c6b773bdc8ccca8b321661b251381acc1daeebf8c8f4a109a`.
- `app/src/main/java/org/libsdl/app` is copied without modification from that archive's official Android project. SDL's zlib notice is included in the APK at `assets/licenses/SDL3.txt`.
- The Gradle wrapper is reused from the same SDL Android template via the existing local Wiiz project, with Gradle 8.12 selected. Gradle is distributed under Apache License 2.0: https://github.com/gradle/gradle/blob/v8.12.0/LICENSE .
- Snes9x sources, patches, and non-commercial license notices are described in `third_party/PROVENANCE.md`. The same source and hooks are compiled for Android; no ROM or extracted game assets are included.
- Android storage, native menu, and JNI adapters in `com/wiizv/app` are WiizV application code.
