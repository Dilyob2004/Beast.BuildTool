#if defined(__ANDROID__)
#include "Platform/Android/AndroidTextInput.h"
#include <charconv>
#include <array>
#include <cmath>

namespace Beast::Detail
{
    namespace
    {
        struct AndroidTextInput final : TextInput, Accessibility
        {
            JavaVM* vm{}; jobject object{};
            jmethodID update{}, close{}, poll{}, bottom{}, readClipboard{}, writeClipboard{};
            jmethodID semanticUpdate{}, semanticPoll{}, semanticEnabled{};
            struct Env
            {
                JavaVM* vm; JNIEnv* env{}; bool attached{};
                explicit Env(JavaVM* value) : vm(value) {
                    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED)
                        attached = vm->AttachCurrentThread(&env, nullptr) == JNI_OK;
                }
                ~Env() { if (attached) vm->DetachCurrentThread(); }
                bool Failed() { if (!env) return true; if (!env->ExceptionCheck()) return false; env->ExceptionClear(); return true; }
            };
            ~AndroidTextInput() override { Env e(vm); if (e.env) e.env->DeleteGlobalRef(object); }
            bool Enabled() const override { Env e(vm); if (!e.env) return false; const bool enabled = e.env->CallBooleanMethod(object, semanticEnabled); return !e.Failed() && enabled; }
            void Changed(const std::vector<AccessibleNode>& nodes) override {
                const auto quote = [](const std::u16string& value) {
                    static constexpr char digits[] = "0123456789abcdef";
                    std::string result = "\""; for (auto c : value) { result += "\\u"; for (int shift = 12; shift >= 0; shift -= 4) result += digits[(c >> shift) & 15]; } return result + "\"";
                };
                std::string packet = "[";
                for (const auto& n : nodes) {
                    if (packet.size() > 1) packet += ',';
                    packet += "{\"id\":" + std::to_string(n.id) + ",\"role\":" + std::to_string(static_cast<int>(n.role)) + ",\"label\":" + quote(n.label) + ",\"value\":" + quote(n.value) + ",\"error\":" + quote(n.error);
                    packet += ",\"x\":" + std::to_string(static_cast<int>(std::floor(n.x))) + ",\"y\":" + std::to_string(static_cast<int>(std::floor(n.y))) + ",\"width\":" + std::to_string(static_cast<int>(std::ceil(n.width))) + ",\"height\":" + std::to_string(static_cast<int>(std::ceil(n.height)));
                    packet += ",\"anchor\":" + std::to_string(n.anchor) + ",\"caret\":" + std::to_string(n.caret);
                    const auto flag = [&](const char* name, bool value) { packet += std::string(",\"") + name + "\":" + (value ? "true" : "false"); };
                    flag("enabled", n.enabled); flag("focused", n.focused); flag("password", n.password); flag("readonly", n.readOnly); flag("multiline", n.multiline); packet += '}';
                }
                packet += ']'; Env e(vm); if (!e.env) return;
                auto text = e.env->NewStringUTF(packet.c_str()); if (e.Failed()) return;
                e.env->CallVoidMethod(object, semanticUpdate, text); e.Failed(); e.env->DeleteLocalRef(text);
            }
            std::optional<AccessibilityAction> PollAction() override {
                Env e(vm); if (!e.env || !Attached()) return {};
                auto packet = static_cast<jstring>(e.env->CallObjectMethod(object, semanticPoll)); if (e.Failed() || !packet) return {};
                const auto length = e.env->GetStringLength(packet);
                if (length > 4200) { e.env->DeleteLocalRef(packet); return {}; }
                const auto* chars = e.env->GetStringChars(packet, nullptr);
                if (!chars) { e.Failed(); e.env->DeleteLocalRef(packet); return {}; }
                std::u16string data(reinterpret_cast<const char16_t*>(chars), static_cast<size_t>(length));
                e.env->ReleaseStringChars(packet, chars); e.env->DeleteLocalRef(packet);
                std::array<int, 4> fields{}; size_t offset = 0;
                for (auto& field : fields) {
                    const auto end = data.find(u'\n', offset); if (end == data.npos || end - offset > 12) return {};
                    std::string number; for (auto i = offset; i < end; ++i) { if (data[i] > 127) return {}; number += static_cast<char>(data[i]); }
                    auto parsed = std::from_chars(number.data(), number.data() + number.size(), field);
                    if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size()) return {}; offset = end + 1;
                }
                if (fields[0] <= 0 || fields[1] < 0 || fields[1] > 5 || fields[2] < 0 || fields[3] < 0 || fields[2] > 4096 || fields[3] > 4096) return {};
                return AccessibilityAction{fields[0], static_cast<AccessibleAction>(fields[1]), data.substr(offset), fields[2], fields[3]};
            }
            std::optional<std::u16string> ReadClipboard() override {
                Env e(vm); if (!e.env) return {};
                auto text = static_cast<jstring>(e.env->CallObjectMethod(object, readClipboard));
                if (e.Failed() || !text) return {};
                const auto length = e.env->GetStringLength(text);
                if (length > 4096) { e.env->DeleteLocalRef(text); return {}; }
                const auto* data = e.env->GetStringChars(text, nullptr);
                std::optional<std::u16string> result;
                if (data) { result = std::u16string(reinterpret_cast<const char16_t*>(data), length); e.env->ReleaseStringChars(text, data); }
                e.Failed(); e.env->DeleteLocalRef(text); return result;
            }
            bool WriteClipboard(const std::u16string& value) override {
                if (value.size() > 4096) return false;
                Env e(vm); if (!e.env) return false;
                auto text = e.env->NewString(reinterpret_cast<const jchar*>(value.data()), static_cast<jsize>(value.size()));
                if (e.Failed()) return false;
                const bool result = e.env->CallBooleanMethod(object, writeClipboard, text);
                const bool failed = e.Failed(); e.env->DeleteLocalRef(text); return result && !failed;
            }
            void Update(const NativeTextState& state, int limit) override {
                Env e(vm); if (!e.env) return;
                auto text = e.env->NewString(reinterpret_cast<const jchar*>(state.text.data()), static_cast<jsize>(state.text.size()));
                if (e.Failed()) return;
                e.env->CallVoidMethod(object, update, static_cast<jlong>(state.session), static_cast<jlong>(state.revision), text,
                    state.anchor, state.caret, state.composingStart, state.composingEnd, limit, static_cast<jint>((state.multiline ? 1 : 0) | (state.password ? 2 : 0)));
                e.Failed(); e.env->DeleteLocalRef(text);
            }
            void Close(std::uint64_t session) override {
                Env e(vm); if (!e.env) return; e.env->CallVoidMethod(object, close, static_cast<jlong>(session)); e.Failed();
            }
            std::optional<NativeTextState> Poll() override {
                Env e(vm); if (!e.env) return {};
                auto packet = static_cast<jstring>(e.env->CallObjectMethod(object, poll));
                if (e.Failed() || !packet) return {};
                const auto length = e.env->GetStringLength(packet);
                if (length > 4608) { e.env->DeleteLocalRef(packet); return {}; }
                const auto* chars = e.env->GetStringChars(packet, nullptr);
                if (!chars) { e.Failed(); e.env->DeleteLocalRef(packet); return {}; }
                std::u16string data(reinterpret_cast<const char16_t*>(chars), static_cast<size_t>(length));
                e.env->ReleaseStringChars(packet, chars); e.env->DeleteLocalRef(packet);
                std::array<long long, 7> numbers{}; size_t offset = 0;
                for (auto& number : numbers) {
                    auto end = data.find(u'\n', offset); if (end == data.npos || end - offset > 20) return {};
                    std::string ascii; for (auto i = offset; i < end; ++i) { if (data[i] > 127) return {}; ascii += static_cast<char>(data[i]); }
                    auto parsed = std::from_chars(ascii.data(), ascii.data() + ascii.size(), number);
                    if (parsed.ec != std::errc{} || parsed.ptr != ascii.data() + ascii.size()) return {};
                    offset = end + 1;
                }
                if (numbers[0] <= 0 || numbers[1] <= 0 || numbers[6] < 0 || numbers[6] > 2) return {};
                for (int i = 2; i < 6; ++i) if (numbers[i] < -1 || numbers[i] > 4096) return {};
                NativeTextState result;
                result.session = numbers[0]; result.revision = numbers[1]; result.text = data.substr(offset);
                result.anchor = static_cast<int>(numbers[2]); result.caret = static_cast<int>(numbers[3]);
                result.composingStart = static_cast<int>(numbers[4]); result.composingEnd = static_cast<int>(numbers[5]);
                result.action = static_cast<int>(numbers[6]); return result;
            }
            int Bottom() { Env e(vm); if (!e.env) return -1; int value = e.env->CallIntMethod(object, bottom); return e.Failed() ? -1 : value; }
        };
    }
    std::shared_ptr<TextInput> CreateAndroidTextInput(JNIEnv* env, jobject activity)
    {
        if (!env || env->PushLocalFrame(16) != JNI_OK) return {};
        auto activityClass = env->GetObjectClass(activity);
        auto loaderMethod = env->GetMethodID(activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
        auto loader = loaderMethod ? env->CallObjectMethod(activity, loaderMethod) : nullptr;
        jclass type = nullptr;
        if (loader && !env->ExceptionCheck()) {
            auto loaderClass = env->GetObjectClass(loader);
            auto load = env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
            auto name = env->NewStringUTF("com.beast.sdk.BeastTextInput");
            if (load) type = static_cast<jclass>(env->CallObjectMethod(loader, load, name));
        }
        std::shared_ptr<AndroidTextInput> result;
        if (type && !env->ExceptionCheck()) {
            auto create = env->GetStaticMethodID(type, "create", "(Landroid/app/Activity;)Lcom/beast/sdk/BeastTextInput;");
            auto object = create ? env->CallStaticObjectMethod(type, create, activity) : nullptr;
            if (object && !env->ExceptionCheck()) {
                result = std::make_shared<AndroidTextInput>(); env->GetJavaVM(&result->vm);
                result->object = env->NewGlobalRef(object);
                result->update = env->GetMethodID(type, "update", "(JJLjava/lang/String;IIIIII)V");
                result->close = env->GetMethodID(type, "close", "(J)V");
                result->poll = env->GetMethodID(type, "poll", "()Ljava/lang/String;");
                result->bottom = env->GetMethodID(type, "getAvailableBottom", "()I");
                result->readClipboard = env->GetMethodID(type, "readClipboard", "()Ljava/lang/String;");
                result->writeClipboard = env->GetMethodID(type, "writeClipboard", "(Ljava/lang/String;)Z");
                result->semanticUpdate = env->GetMethodID(type, "accessibilityUpdate", "(Ljava/lang/String;)V");
                result->semanticPoll = env->GetMethodID(type, "accessibilityPoll", "()Ljava/lang/String;");
                result->semanticEnabled = env->GetMethodID(type, "accessibilityEnabled", "()Z");
            }
        }
        if (env->ExceptionCheck()) { env->ExceptionClear(); result.reset(); }
        env->PopLocalFrame(nullptr); return result;
    }
    int AndroidAvailableBottom(const std::shared_ptr<TextInput>& input) {
        return input ? static_cast<AndroidTextInput&>(*input).Bottom() : -1;
    }
    std::shared_ptr<Accessibility> AndroidAccessibility(const std::shared_ptr<TextInput>& input) {
        return std::dynamic_pointer_cast<Accessibility>(input);
    }
}
#endif
