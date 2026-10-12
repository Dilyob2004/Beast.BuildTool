# Android Release symbols

Beast compiles Android code with `-g` in both configurations, preserving `-O2`
for Release and `-O0` for Debug. Release packaging runs the selected NDK's
`llvm-strip --strip-unneeded` on the **deployment copies** of the application
library and `libc++_shared.so`. The NDK installation and linker outputs are not
modified. The same packaging path works with existing generated Gradle projects.

`keepDebugSymbols` in Gradle is intentional: Beast owns stripping for these
native libraries, and Gradle must not independently alter Debug libraries.
Additional user-supplied JNI libraries are not automatically stripped.

Outputs:

- `Binaries/Android/Release/<Project>.apk`: signed, verified deployment APK.
- `Binaries/Android/Release/Symbols/arm64-v8a/*.so`: original unstripped ELF files,
  including source/line debug info where provided by the compiler/NDK.
- `Symbols/arm64-v8a/manifest.json`: SHA-256 of the exact APK, original and stripped
  libraries, ABI and NDK version. ELF build IDs are preserved by stripping.

Archive the APK and the **entire matching Symbols directory together** for every
distributed version; subsequent builds overwrite these current-build outputs.
Symbols are outside APK/assets. Do not distribute them to end users: they can
contain source paths and implementation details. Use these exact originals with
NDK symbolization tools; optimized code can still have inlined/optimized-out values.

From the repository root, after building Release:

```powershell
.\Tests\Test-AndroidReleaseSymbols.ps1 -Project Test/EcoSbor -Ndk 'C:/path/to/ndk/version'
```

The test checks APK/library hashes, original DWARF presence, removal of deployment
debug/static symbols, unchanged dynamic symbols and matching build IDs. This
does not substitute for installing and exercising the application on a device.

Reference: [LLVM strip options](https://www.llvm.org/docs/CommandGuide/llvm-strip.html).

Verified on EcoSbor arm64 with NDK 29.0.14206865: Release APK decreased from
11,718,235 to 3,573,011 bytes (~69.5%). Packaged application ELF is 2,113,040
bytes, C++ runtime is 1,374,336 bytes. Original ELFs remain 12,370,560 and
9,290,184 bytes respectively. Release → Debug → Release packaging and the
manifest/ELF test passed; Debug APK retains original DWARF. Device execution
was not tested in that historical check; Release was still unsigned at that time.

## Automatic APK signing

Every Beast Android build verifies the final APK signature before publishing it
to `Binaries/Android/<Configuration>/<Project>.apk`. Existing projects work without
regenerating Gradle files. Intermediate Gradle outputs may still be unsigned;
install the APK from `Binaries`, not from `Platforms/Android/app/build/outputs`.

By default, a valid signature supplied by Gradle is preserved byte-for-byte.
Otherwise Beast signs using the local Android debug key: `ANDROID_USER_HOME/debug.keystore`
when that environment variable is set, or the user's `.android/debug.keystore`.
If missing, the key is created once with Java `keytool`; an existing key is never
overwritten. This also allows Release to update a Debug installation signed with
the same key, without losing app data. **A debug-signed Release is optimized, but
its signing identity is only for local testing, not store publication.**

For a publishing key, set this block under `android` in `Config/BuildConfig.json`:

```json
"releaseSigning": {
  "keyStore": "Config/Signing/my-release.jks",
  "keyAlias": "my-release",
  "storePasswordEnv": "BEAST_RELEASE_STORE_PASSWORD",
  "keyPasswordEnv": "BEAST_RELEASE_KEY_PASSWORD"
}
```

The path is absolute or relative to the project root. Set the named password
environment variables in your shell/CI secret store; do not commit passwords or
private keys. Empty `keyPasswordEnv` means use the store password. Explicit
`releaseSigning` overrides Gradle signing for Release only; Debug keeps Gradle's
signature. Empty signing fields retain the default behavior. New project configs
include these fields automatically; old configs need no migration.

Signing uses `apksigner.jar` from the latest installed stable SDK build-tools and
Java/keytool from `android.javaHome`. Password values are passed only through the
child process environment, not arguments or build logs. Invalid config, missing
keys/passwords, signing failure or verification failure stops the build; there is
no silent fallback from an explicitly configured publishing key to a debug key.
The final output is replaced only after verification, and the symbol manifest's
APK hash refers to the signed artifact. `beast run` can install this standard APK
without an extra manual signing step.

Changing keys cannot update an already installed package signed with a different
certificate. Beast does not uninstall it or clear its data automatically.

Integration checks (requires installed Android SDK/JBR and an EcoSbor Release build):

```powershell
dotnet run --project Tests/AndroidSigningTests/AndroidSigningTests.csproj -c Release -- D:/Github/Beast.BuildTool
.\Tests\Test-AndroidReleaseSymbols.ps1
```

The signing test creates isolated disposable keys under EcoSbor's Intermediate
verification directory, not in the real Android user home, and leaves artifacts
there for inspection. It covers first-use key creation/reuse, preserving an existing
signature, custom relative keystores, environment passwords, failed signing without
replacing a good APK, generated defaults and malformed-config rejection.
