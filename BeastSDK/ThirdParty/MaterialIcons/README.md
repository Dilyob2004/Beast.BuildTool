# Material Icons — EcoSbor subset

Google Material Design Icons, Apache License 2.0 (see LICENSE).
Font copied from the user's Flutter 3.41.7 release build:
`Prototypes/flutter_application_1/build/app/intermediates/assets/release/mergeReleaseAssets/flutter_assets/fonts/MaterialIcons-Regular.otf`.
Flutter's release tree-shaking has subsetted the upstream font; Beast copied
that 6520-byte subset unchanged. It contains the prototype's icons, not the full catalog.

SHA-256: `849B3EBD99D7F7ACEEC962CFD26A39A32AFC806B73407EF810CC2896F3FFE9DB`.
Codepoints were checked against Flutter 3.41.7 `packages/flutter/lib/src/material/icons.dart`.
Upstream: https://github.com/google/material-design-icons
Generator: `BeastSDK/Tools/GenerateUIAssets`; it verifies this hash before embedding.
Only trusted embedded fonts reach stb; Icon rejects absent codepoints.
