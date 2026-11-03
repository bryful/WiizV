#pragma once
#include <SDL3/SDL.h>
#include <jni.h>
#include <filesystem>
#include <map>
#include <string>
#include <stdexcept>
#include <vector>

inline jstring androidToJava(JNIEnv* env, const std::string& value)
{
    jclass type=env->FindClass("java/lang/String");
    auto bytes=env->NewByteArray(static_cast<jsize>(value.size()));if(!bytes)return nullptr;
    env->SetByteArrayRegion(bytes,0,static_cast<jsize>(value.size()),reinterpret_cast<const jbyte*>(value.data()));
    auto encoding=env->NewStringUTF("UTF-8");
    auto result=static_cast<jstring>(env->NewObject(type,env->GetMethodID(type,"<init>","([BLjava/lang/String;)V"),bytes,encoding));
    env->DeleteLocalRef(encoding);env->DeleteLocalRef(bytes);env->DeleteLocalRef(type);return result;
}
inline std::string androidFromJava(JNIEnv* env, jstring value)
{
    auto encoding=env->NewStringUTF("UTF-8");jclass type=env->FindClass("java/lang/String");
    auto bytes=static_cast<jbyteArray>(env->CallObjectMethod(value,env->GetMethodID(type,"getBytes","(Ljava/lang/String;)[B"),encoding));
    env->DeleteLocalRef(type);env->DeleteLocalRef(encoding);
    if(!bytes)throw std::runtime_error("Androidの文字列を変換できません。");
    std::string result(static_cast<size_t>(env->GetArrayLength(bytes)),'\0');
    env->GetByteArrayRegion(bytes,0,static_cast<jsize>(result.size()),reinterpret_cast<jbyte*>(result.data()));env->DeleteLocalRef(bytes);return result;
}

struct AndroidCall
{
    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    jclass type = env->GetObjectClass(activity);
    ~AndroidCall() { env->DeleteLocalRef(type); env->DeleteLocalRef(activity); }
    void check()
    {
        if (!env->ExceptionCheck()) return;
        jthrowable exception = env->ExceptionOccurred(); env->ExceptionClear();
        jclass throwable = env->GetObjectClass(exception);
        auto text = static_cast<jstring>(env->CallObjectMethod(exception, env->GetMethodID(throwable, "toString", "()Ljava/lang/String;")));
        const std::string message=androidFromJava(env,text);
        env->DeleteLocalRef(text); env->DeleteLocalRef(throwable); env->DeleteLocalRef(exception);
        throw std::runtime_error(message);
    }
    jmethodID method(const char* name, const char* signature)
    {
        auto result = env->GetMethodID(type, name, signature); check(); return result;
    }
};

inline std::string androidString(const char* name)
{
    AndroidCall call;
    auto value = static_cast<jstring>(call.env->CallObjectMethod(call.activity, call.method(name, "()Ljava/lang/String;")));
    call.check(); if (!value) return {};
    auto result=androidFromJava(call.env,value);call.env->DeleteLocalRef(value);return result;
}

inline float androidDensity()
{
    AndroidCall call;
    const float result = call.env->CallFloatMethod(call.activity, call.method("displayDensity", "()F")); call.check();
    return result > 0 ? result : 1.0f;
}

inline void androidCommit(const std::filesystem::path& path, const void* data, size_t size)
{
    AndroidCall call;
    auto name = androidToJava(call.env,path.string());
    auto bytes = call.env->NewByteArray(static_cast<jsize>(size)); call.check();
    if (size) call.env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(size), static_cast<const jbyte*>(data));
    call.env->CallVoidMethod(call.activity, call.method("commitFile", "(Ljava/lang/String;[B)V"), name, bytes);
    call.env->DeleteLocalRef(bytes); call.env->DeleteLocalRef(name); call.check();
}

inline void androidShowMenu(const std::string& snapshot)
{
    AndroidCall call; auto text = androidToJava(call.env,snapshot);
    call.env->CallVoidMethod(call.activity, call.method("showFunctionMenu", "(Ljava/lang/String;)V"), text);
    call.env->DeleteLocalRef(text); call.check();
}

inline void androidChooseRom()
{
    AndroidCall call; call.env->CallVoidMethod(call.activity, call.method("chooseRom", "()V")); call.check();
}

inline void androidToast(const std::string& text)
{
    AndroidCall call; auto value = androidToJava(call.env,text);
    call.env->CallVoidMethod(call.activity, call.method("showNotice", "(Ljava/lang/String;)V"), value);
    call.env->DeleteLocalRef(value); call.check();
}

inline float androidUiDensity = 1;
inline bool renderOutputSize(SDL_Renderer* renderer, int* width, int* height)
{
    const bool result = SDL_GetRenderOutputSize(renderer, width, height);
    if (result) { *width = static_cast<int>(*width / androidUiDensity); *height = static_cast<int>(*height / androidUiDensity); }
    return result;
}

struct AndroidText { SDL_Texture* texture = nullptr; int width = 0, height = 0; };
inline std::map<std::string, AndroidText> androidTexts;
inline void androidClearText()
{
    for (auto& [key, item] : androidTexts) { (void)key; SDL_DestroyTexture(item.texture); }
    androidTexts.clear();
}
inline void androidText(SDL_Renderer* renderer, float x, float y, const std::string& text, bool centered = false, float maxWidth = 100000)
{
    auto found = androidTexts.find(text);
    if (found == androidTexts.end())
    {
        if(androidTexts.size()>=128)androidClearText();
        AndroidCall call; auto name = androidToJava(call.env,text);
        auto pixels = static_cast<jintArray>(call.env->CallObjectMethod(call.activity, call.method("renderUiText", "(Ljava/lang/String;)[I"), name));
        call.env->DeleteLocalRef(name); call.check();
        jint dimensions[2]{}; call.env->GetIntArrayRegion(pixels, 0, 2, dimensions);
        AndroidText item; item.width = dimensions[0]; item.height = dimensions[1];
        if (item.width < 1 || item.height < 1 || call.env->GetArrayLength(pixels) != 2 + item.width * item.height)
            throw std::runtime_error("Androidの文字画像が不正です。");
        auto data = call.env->GetIntArrayElements(pixels, nullptr);
        item.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, item.width, item.height);
        if (item.texture) { SDL_SetTextureBlendMode(item.texture, SDL_BLENDMODE_BLEND); SDL_UpdateTexture(item.texture, nullptr, data + 2, item.width * 4); }
        call.env->ReleaseIntArrayElements(pixels, data, JNI_ABORT); call.env->DeleteLocalRef(pixels);
        if (!item.texture) throw std::runtime_error(SDL_GetError());
        found = androidTexts.emplace(text, item).first;
    }
    const float scale=std::min(1.0f,std::max(1.0f,maxWidth)/found->second.width);
    const float width=found->second.width*scale,height=found->second.height*scale;
    SDL_FRect destination{x-(centered?width/2:0), y+(found->second.height-height)/2,width,height};
    SDL_RenderTexture(renderer, found->second.texture, nullptr, &destination);
}
