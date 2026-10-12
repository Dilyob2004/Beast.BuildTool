#pragma once
#if defined(__ANDROID__)
#include "Core/TextInput.h"
#include "Core/Accessibility.h"
#include <jni.h>
#include <memory>

namespace Beast::Detail
{
    std::shared_ptr<TextInput> CreateAndroidTextInput(JNIEnv* env, jobject activity);
    int AndroidAvailableBottom(const std::shared_ptr<TextInput>& input);
    std::shared_ptr<Accessibility> AndroidAccessibility(const std::shared_ptr<TextInput>& input);
}
#endif
