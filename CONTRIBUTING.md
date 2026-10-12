# Contributing to Beast

Thanks for helping build a native C++ UI stack. Beast is experimental, and small,
reproducible improvements are especially useful. English and Russian are welcome.

## Before changing code

1. Follow the [quick start](README.md#quick-start) and run a sample.
2. For a large API or architecture change, open an issue first to discuss scope.
3. Keep pull requests focused. Explain the problem, the solution, and what you tested.
4. Preserve third-party license notices. Do not commit SDK installations, build
   outputs, signing keys, credentials, or local machine paths.

Anyone can fork the public repository and propose a pull request. Direct write
access is limited to maintainers; submitting a PR does not grant that access.

## Validation on Windows

From the repository root, with MSVC and .NET installed:

```powershell
dotnet build BuildTool/Beast.BuildTool.csproj -c Release
.\BeastSDK\Tests\Run-UnicodeTests.ps1
```

For UI changes, build the EcoSbor Debug SDK/application first, then run the
relevant suites (some need a working OpenGL/Vulkan driver and desktop session):

```powershell
.\BuildTool\bin\Beast.BuildTool.exe build Test/EcoSbor --platform Windows --config Debug
.\BeastSDK\Tests\Run-TextFieldTests.ps1
.\Test\EcoSbor\Tests\Run-Tests.ps1
```

Use the sample config setup described in the README before building. Report
which tests ran and which platforms you could not test. A successful Windows
test does not establish Android correctness or performance.

## Useful bug reports

- Commit/revision, build configuration, OS version, GPU and RHI backend.
- Small reproduction and expected versus actual behavior.
- Redacted logs, screenshots, or frame-time traces when relevant.
- For Android: API level, ABI, emulator/device model; omit serial numbers and identifiers.

Never put passwords, tokens, private keystores, or personal data in public issues.
Please do not publish exploit details for a security-sensitive issue; contact the
maintainer privately before disclosing those details.

## Licensing

By contributing original code, you agree to provide it under the project's MIT
license. Include attribution and the original license for any third-party code,
and discuss new dependencies before vendoring them.
