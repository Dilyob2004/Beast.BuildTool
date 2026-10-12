# RHI verification — 2026-09-09

- BuildTool .NET build: zero warnings/errors.
- Windows Debug: RHIDemo and RHIVulkan built through Beast; RHIVulkan.sln also
  built through Visual Studio MSBuild, including the BeastSDK dependency.
- RHITests: command-list validation, 40 update iterations on each backend,
  window resize, backend destruction/recreation on the same HWND. Passed.
- Windows OpenGL: 3.3 core, AMD driver. Vulkan: AMD Radeon(TM) Graphics,
  VK_LAYER_KHRONOS_validation enabled, no reported validation warnings/errors.
- Android ARM64 APKs: compiled with NDK 29.0.14206865 and installed on
  Medium_Phone_API_36.1. Runtime OpenGL ES 3.1 and Vulkan Goldfish GFXStream
  (NVIDIA GeForce RTX 5060 Laptop GPU). Both display the same background and
  blue/orange rectangles. Screenshots inspected under each test project's Intermediate.
- Android lifecycle: both apps lost their native surfaces in the background,
  recreated RHI after resume and kept the same process IDs. No app crash in logs.
- Compiler-failure regression: intentionally invalid test source returns nonzero,
  skips linking and produces no executable.

Android validation layers were not installed/enabled for this run. Physical
devices, rotation, forced GPU device loss, and long-duration stress are not covered.

## OpenGL function-table update

- User-supplied GL/WGL/EGL headers in ThirdParty/OpenGL are now on the SDK build
  and IntelliSense include paths. GLES3 types still come from Android NDK.
- OpenGLLoaderTests compiled with MSVC /W4 /WX and passed: BASE/CORE routing,
  missing required function names, failure/reset/reload and independent tables.
- RHITests re-linked against the updated library and passed on Windows,
  including repeated GL context creation and the Vulkan regression check.
- Android ARM64 library and RHIDemo APK rebuilt successfully with the new table
  and supplied EGL headers.
- Updated Android app started with OpenGL ES 3.1, released its RHI surface on
  backgrounding and recreated the context/function table on resume with the same PID.

## Windows OpenGL 4.6 requirement — 2026-09-10

- WGL now requests 4.6 core with no fallback to older desktop GL. RHI creation
  also checks GL_MAJOR_VERSION, GL_MINOR_VERSION and GL_CONTEXT_PROFILE_MASK.
- RHIDemo Windows Debug rebuilt through Beast successfully. Actual driver log:
  `4.6.0 Core Profile Context 25.10.36.08.260506`.
- RHITests compiled with MSVC /W4 /WX and passed: rendering, resize and context
  recreation, plus the Vulkan regression with validation enabled and no reported
  warnings/errors. OpenGLLoaderTests also passed.
- BuildTool .NET build: zero warnings/errors.
- Android's GLES baseline and implementation are unchanged; Android runtime was
  not re-tested in this change. Lower-version/unsupported hardware is not tested.
- This change does not add shader pipelines, custom OpenGL FBOs or GLsync/RHI fence
  resources. The existing Vulkan backend already uses framebuffers and a fence.

## OpenGL private framebuffer and GPU fence — 2026-09-10

- RHIDemo Windows Debug and Android ARM64 Debug rebuilt through Beast. Both now
  use the same RGBA8 renderbuffer/FBO + window blit + GLsync path.
- OpenGLLoaderTests: every required entry point tested for failure/reset/reload.
- OpenGLFrameTests: passed with MSVC /W4 /WX. Includes timeout without target
  reuse/resize, signaled/failed fences, allocation failure, incomplete replacement
  rollback, blit/fence/flush ordering, context generation changes, failed recovery
  and failed function-table reload. No deletion through stale context handles.
- RHITests: Windows GL 4.6 core and Vulkan validation regression passed (resize
  and renderer recreation included); no reported Vulkan validation errors.
- OpenGLFrameGpuTests: passed actual RGBA8/attachment dimensions, offscreen pixel
  colors/orientation, GL blit error checks, resize and recreation on AMD GL 4.6.
  Window-backbuffer pixel assertions explicitly SKIPPED on this host: even a direct
  green clear of framebuffer 0 reads back black with GL_NO_ERROR, independently
  of the new FBO/blit. This does not establish Windows displayed-pixel correctness.
- Android: Medium_Phone_API_36.1 cold boot, headless emulator, OpenGL ES 3.0
  SwiftShader 4.0.0.1. APK installed/launched, screenshot verified: correct dark
  background, cyan rectangle and orange upper inner rectangle. Home/resume
  destroyed/recreated the RHI surface, same app PID 1908, screenshot verified
  again, no app errors in Beast/AndroidRuntime logs.
- Screenshots: Test/RHIDemo/Intermediate/Tests/android-fbo.png and
  android-fbo-resumed.png. The Android run validates GLES compatibility and
  lifecycle, not physical GPU performance; no Flutter-level smoothness claim.
- No public PSO/texture/shader/fence API added. No forced physical context/device
  loss, long-duration resource stress or 60/120 Hz frame-pacing benchmark.
