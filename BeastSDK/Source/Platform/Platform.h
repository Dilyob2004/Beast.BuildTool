#pragma once

namespace Beast
{
    enum class Platform { Windows, Android };
    constexpr Platform CurrentPlatform()
    {
#if defined(_WIN32)
        return Platform::Windows;
#elif defined(__ANDROID__)
        return Platform::Android;
#else
#error BeastSDK currently supports Windows and Android.
#endif
    }
}
