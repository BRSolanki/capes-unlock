#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

#include "mc_interceptor_java_dex.hpp"
#include "mc_interceptor_native_dex.hpp"
#include "capes_v1.hpp"
#include "capes_v2.hpp"
#include "creator_pages.hpp"
#include "profile_persona_append.hpp"
#include "profile_skins_append.hpp"
#include "skinpack_viewer_60a2047b_fe99_4a1d_9ccf_79d6a9b63adb.hpp"
#include "skinpack_viewer_c65b1919_df90_4c9b_b3d5_a59c7d7a3979.hpp"
#include "skinpack_viewer_62c68fd4_3075_40ed_8b7b_15dcc7b667ff.hpp"
#include "skinpack_viewer_2f60a7a0_8010_49b1_8b57_851ac41f34cf.hpp"
#include "skinpack_viewer_80c85890_dad7_4d3a_b2ef_60071327b7cf.hpp"
#include "skinpack_viewer_4c61b1f5_2bbc_492f_8a2a_1f3b813c4113.hpp"
#include "skinpack_viewer_1fc32dd0_b399_4255_a4c7_e6e262a40ab6.hpp"
#include "skinpack_viewer_358a8710_bcf4_4cd7_ae86_a63ffec500c8.hpp"
#include "skinpack_viewer_5b13cb52_e2bf_438c_97f2_1105c54929d9.hpp"
#include "all_main_pages.hpp"
#include "skinpack_pages.hpp"
#include "playfab_items.hpp"
#include "playfab_search.hpp"

#define LOG_TAG "mchook"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {
std::atomic<bool> g_install_complete{false};
std::atomic<bool> g_install_mutated{false};

JavaVM* g_vm = nullptr;

constexpr const char* kDexClass = "com.mchook.McInterceptor";
constexpr const char* kHttpClientClass = "com/xbox/httpclient/HttpClientRequest";
constexpr const char* kHttpClientDotClass = "com.xbox.httpclient.HttpClientRequest";
constexpr const char* kMinecraftActivityJniClass = "org/levimc/launcher/core/minecraft/MinecraftActivity";
constexpr const char* kMinecraftActivityDotClass = "org.levimc.launcher.core.minecraft.MinecraftActivity";
constexpr const char* kInstanceField = "INSTANCE";
constexpr const char* kOkClientField = "OK_CLIENT";
constexpr const char* kOkHttpClientDesc = "Lokhttp3/OkHttpClient;";
constexpr const char* kInterceptorClass = "okhttp3.Interceptor";
constexpr const char* kOkHttpClientClass = "okhttp3.OkHttpClient";

// Forward declarations: entitlement forwarding is defined before these helpers.
std::string jstring_value(JNIEnv* env, jobject obj);
jobject build_json_response(JNIEnv* env, jobject request, const std::string& json);

struct LocalRef {
    JNIEnv* env{};
    jobject obj{};
    ~LocalRef() { if (env && obj) env->DeleteLocalRef(obj); }
    LocalRef(const LocalRef&) = delete;
    LocalRef& operator=(const LocalRef&) = delete;
};

bool clear_exception(JNIEnv* env, const char* where, bool describe = true) {
    if (!env || !env->ExceptionCheck()) return false;
    if (describe) {
        LOGE("JNI exception at %s", where);
        env->ExceptionDescribe();
    }
    env->ExceptionClear();
    return true;
}

jclass load_class(JNIEnv* env, jobject loader, const char* name) {
    if (!env || !loader) return nullptr;
    jclass loaderCls = env->FindClass("java/lang/ClassLoader");
    if (clear_exception(env, "FindClass(ClassLoader)")) return nullptr;
    jmethodID loadClass = env->GetMethodID(loaderCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (clear_exception(env, "GetMethodID(ClassLoader.loadClass)")) return nullptr;
    jstring nameStr = env->NewStringUTF(name);
    jobject result = env->CallObjectMethod(loader, loadClass, nameStr);
    env->DeleteLocalRef(nameStr);
    if (clear_exception(env, "ClassLoader.loadClass")) return nullptr;
    return static_cast<jclass>(result);
}

bool loader_can_load(JNIEnv* env, jobject loader, const char* name) {
    if (!loader) return false;
    jobject cls = load_class(env, loader, name);
    if (!cls) return false;
    env->DeleteLocalRef(cls);
    return true;
}

jobject class_loader_of(JNIEnv* env, jclass clazz) {
    if (!clazz) return nullptr;
    jmethodID getClassLoader = env->GetMethodID(env->FindClass("java/lang/Class"), "getClassLoader", "()Ljava/lang/ClassLoader;");
    if (clear_exception(env, "Class.getClassLoader method")) return nullptr;
    jobject loader = env->CallObjectMethod(clazz, getClassLoader);
    if (clear_exception(env, "Class.getClassLoader")) return nullptr;
    return loader;
}

jobject application_class_loader(JNIEnv* env, jobject app) {
    if (!app) return nullptr;
    jclass appCls = env->GetObjectClass(app);
    jmethodID mid = env->GetMethodID(appCls, "getClassLoader", "()Ljava/lang/ClassLoader;");
    if (clear_exception(env, "Application.getClassLoader method")) return nullptr;
    jobject loader = env->CallObjectMethod(app, mid);
    if (clear_exception(env, "Application.getClassLoader")) return nullptr;
    env->DeleteLocalRef(appCls);
    return loader;
}

jobject context_class_loader(JNIEnv* env) {
    jclass threadCls = env->FindClass("java/lang/Thread");
    if (clear_exception(env, "FindClass(Thread)")) return nullptr;
    jmethodID currentThread = env->GetStaticMethodID(threadCls, "currentThread", "()Ljava/lang/Thread;");
    jmethodID getContext = env->GetMethodID(threadCls, "getContextClassLoader", "()Ljava/lang/ClassLoader;");
    if (clear_exception(env, "Thread methods")) return nullptr;
    jobject thread = env->CallStaticObjectMethod(threadCls, currentThread);
    if (clear_exception(env, "Thread.currentThread")) return nullptr;
    jobject loader = env->CallObjectMethod(thread, getContext);
    env->DeleteLocalRef(thread);
    if (clear_exception(env, "Thread.getContextClassLoader")) return nullptr;
    return loader;
}

jobject system_class_loader(JNIEnv* env) {
    jclass cls = env->FindClass("java/lang/ClassLoader");
    if (clear_exception(env, "FindClass(ClassLoader)")) return nullptr;
    jmethodID mid = env->GetStaticMethodID(cls, "getSystemClassLoader", "()Ljava/lang/ClassLoader;");
    if (clear_exception(env, "getSystemClassLoader method")) return nullptr;
    jobject loader = env->CallStaticObjectMethod(cls, mid);
    if (clear_exception(env, "getSystemClassLoader")) return nullptr;
    return loader;
}

jobject current_application(JNIEnv* env) {
    jclass at = env->FindClass("android/app/ActivityThread");
    if (clear_exception(env, "FindClass(ActivityThread)")) return nullptr;
    jmethodID currentApplication = env->GetStaticMethodID(at, "currentApplication", "()Landroid/app/Application;");
    if (clear_exception(env, "ActivityThread.currentApplication method")) return nullptr;
    jobject app = env->CallStaticObjectMethod(at, currentApplication);
    if (clear_exception(env, "ActivityThread.currentApplication")) return nullptr;
    if (!app) {
        LOGW("currentApplication returned null");
        return nullptr;
    }
    return app;
}

void add_candidate(JNIEnv* env, std::vector<jobject>& out, jobject loader) {
    if (!loader) return;
    for (jobject existing : out) {
        if (env->IsSameObject(existing, loader)) {
            env->DeleteLocalRef(loader);
            return;
        }
    }
    out.push_back(loader);
}

void add_loader_for_named_class(JNIEnv* env, std::vector<jobject>& out, const char* className) {
    std::vector<jobject> bases;
    jobject context = context_class_loader(env);
    if (context) bases.push_back(context);
    jobject system = system_class_loader(env);
    if (system) bases.push_back(system);

    for (jobject base : bases) {
        jclass clazz = load_class(env, base, className);
        if (clazz) {
            jobject loader = class_loader_of(env, clazz);
            if (loader) add_candidate(env, out, loader);
            env->DeleteLocalRef(clazz);
        }
        env->DeleteLocalRef(base);
    }
}

jobject find_okhttp_loader(JNIEnv* env, jobject app) {
    std::vector<jobject> candidates;

    // IMPORTANT on Android: during JNI_OnLoad, FindClass() can use the
    // class-loader associated with the native library load context. The game
    // may keep Minecraft/OkHttp classes outside the Application loader, so
    // this is a stronger source than guessing from Application/Context.
    const char* bootstrapClasses[] = {
        kHttpClientClass,
        "okhttp3/Interceptor",
        "okhttp3/OkHttpClient",
        kMinecraftActivityJniClass,
    };

    for (const char* name : bootstrapClasses) {
        jclass clazz = env->FindClass(name);
        if (clear_exception(env, "JNI_OnLoad FindClass bootstrap", false)) continue;
        if (!clazz) continue;

        jobject loader = class_loader_of(env, clazz);
        if (loader) add_candidate(env, candidates, loader);
        env->DeleteLocalRef(clazz);
    }

    if (jobject loader = application_class_loader(env, app)) add_candidate(env, candidates, loader);
    if (jobject loader = context_class_loader(env)) add_candidate(env, candidates, loader);
    if (jobject loader = system_class_loader(env)) add_candidate(env, candidates, loader);

    // Resolve loaders from the actual classes known to the game/mod runtime.
    add_loader_for_named_class(env, candidates, kHttpClientDotClass);
    add_loader_for_named_class(env, candidates, kMinecraftActivityDotClass);

    // Walk parent chains breadth-first and retain every distinct candidate.
    for (size_t i = 0; i < candidates.size(); ++i) {
        jobject loader = candidates[i];
        jclass loaderCls = env->FindClass("java/lang/ClassLoader");
        if (clear_exception(env, "FindClass(ClassLoader)")) break;
        jmethodID getParent = env->GetMethodID(loaderCls, "getParent", "()Ljava/lang/ClassLoader;");
        if (clear_exception(env, "ClassLoader.getParent method")) break;
        jobject parent = env->CallObjectMethod(loader, getParent);
        if (clear_exception(env, "ClassLoader.getParent")) continue;
        if (parent) add_candidate(env, candidates, parent);
    }

    // Strong preference: a loader that can see all classes needed by the DEX
    // and the HttpClientRequest field we are going to replace.
    for (jobject loader : candidates) {
        const bool http = loader_can_load(env, loader, kHttpClientDotClass);
        const bool interceptor = loader_can_load(env, loader, kInterceptorClass);
        const bool client = loader_can_load(env, loader, kOkHttpClientClass);
        LOGI("[dex] loader candidate http=%d interceptor=%d client=%d", http, interceptor, client);
        if (http && interceptor && client) {
            LOGI("[dex] selected loader resolves HttpClientRequest + okhttp3 classes");
            jobject global = env->NewGlobalRef(loader);
            for (jobject c : candidates) env->DeleteLocalRef(c);
            return global;
        }
    }

    LOGW("[dex] no candidate loader resolves all required classes");
    for (jobject c : candidates) env->DeleteLocalRef(c);
    return nullptr;
}

jclass load_embedded_dex(JNIEnv* env, jobject app, const std::uint8_t* dexBytes, size_t dexSize, const char* label) {
    jobject parentLoader = find_okhttp_loader(env, app);
    if (!parentLoader) return nullptr;

    jclass byteBufferCls = env->FindClass("java/nio/ByteBuffer");
    if (clear_exception(env, "FindClass(ByteBuffer)")) { env->DeleteGlobalRef(parentLoader); return nullptr; }
    jmethodID allocateDirect = env->GetStaticMethodID(byteBufferCls, "allocateDirect", "(I)Ljava/nio/ByteBuffer;");
    if (clear_exception(env, "ByteBuffer.allocateDirect")) { env->DeleteGlobalRef(parentLoader); return nullptr; }
    jobject buffer = env->CallStaticObjectMethod(byteBufferCls, allocateDirect, static_cast<jint>(dexSize));
    if (clear_exception(env, "ByteBuffer.allocateDirect call")) { env->DeleteGlobalRef(parentLoader); return nullptr; }

    jbyteArray bytes = env->NewByteArray(static_cast<jsize>(dexSize));
    if (clear_exception(env, "NewByteArray(Dex)")) { env->DeleteLocalRef(buffer); env->DeleteGlobalRef(parentLoader); return nullptr; }
    env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(dexSize), reinterpret_cast<const jbyte*>(dexBytes));
    if (clear_exception(env, "SetByteArrayRegion(Dex)")) { env->DeleteLocalRef(bytes); env->DeleteLocalRef(buffer); env->DeleteGlobalRef(parentLoader); return nullptr; }

    jmethodID put = env->GetMethodID(byteBufferCls, "put", "([B)Ljava/nio/ByteBuffer;");
    jmethodID rewind = env->GetMethodID(byteBufferCls, "rewind", "()Ljava/nio/Buffer;");
    if (clear_exception(env, "ByteBuffer methods")) { env->DeleteLocalRef(bytes); env->DeleteLocalRef(buffer); env->DeleteGlobalRef(parentLoader); return nullptr; }
    env->CallObjectMethod(buffer, put, bytes);
    if (clear_exception(env, "ByteBuffer.put")) { env->DeleteLocalRef(bytes); env->DeleteLocalRef(buffer); env->DeleteGlobalRef(parentLoader); return nullptr; }
    env->CallObjectMethod(buffer, rewind);
    if (clear_exception(env, "ByteBuffer.rewind")) { env->DeleteLocalRef(bytes); env->DeleteLocalRef(buffer); env->DeleteGlobalRef(parentLoader); return nullptr; }

    jclass memLoaderCls = env->FindClass("dalvik/system/InMemoryDexClassLoader");
    if (clear_exception(env, "FindClass(InMemoryDexClassLoader)")) { env->DeleteLocalRef(bytes); env->DeleteLocalRef(buffer); env->DeleteGlobalRef(parentLoader); return nullptr; }
    jmethodID ctor = env->GetMethodID(memLoaderCls, "<init>", "(Ljava/nio/ByteBuffer;Ljava/lang/ClassLoader;)V");
    if (clear_exception(env, "InMemoryDexClassLoader ctor")) { env->DeleteLocalRef(bytes); env->DeleteLocalRef(buffer); env->DeleteGlobalRef(parentLoader); return nullptr; }
    jobject loader = env->NewObject(memLoaderCls, ctor, buffer, parentLoader);
    if (clear_exception(env, "InMemoryDexClassLoader construction")) { env->DeleteLocalRef(bytes); env->DeleteLocalRef(buffer); env->DeleteGlobalRef(parentLoader); return nullptr; }

    jclass clazz = load_class(env, loader, kDexClass);
    if (!clazz) {
        LOGE("[dex] loadClass(%s) failed", kDexClass);
    } else {
        LOGI("[dex] %s McInterceptor loaded from in-memory DEX", label);
    }

    env->DeleteLocalRef(loader);
    env->DeleteLocalRef(bytes);
    env->DeleteLocalRef(buffer);
    env->DeleteGlobalRef(parentLoader);
    return clazz;
}

jobject new_interceptor(JNIEnv* env, jclass mcClass) {
    jmethodID ctor = env->GetMethodID(mcClass, "<init>", "()V");
    if (clear_exception(env, "McInterceptor ctor lookup")) return nullptr;
    jobject interceptor = env->NewObject(mcClass, ctor);
    if (clear_exception(env, "McInterceptor constructor")) return nullptr;
    return interceptor;
}

bool set_instance(JNIEnv* env, jclass mcClass, jobject interceptor) {
    jfieldID fid = env->GetStaticFieldID(mcClass, kInstanceField, "Lcom/mchook/McInterceptor;");
    if (clear_exception(env, "McInterceptor.INSTANCE field")) return false;
    env->SetStaticObjectField(mcClass, fid, interceptor);
    if (clear_exception(env, "McInterceptor.INSTANCE set")) return false;
    return true;
}

bool add_spoof(JNIEnv* env, jclass mcClass, const char* key, const uint8_t* data, size_t size) {
    jmethodID mid = env->GetStaticMethodID(mcClass, "addSpoof", "(Ljava/lang/String;Ljava/lang/String;)V");
    if (clear_exception(env, "McInterceptor.addSpoof method")) return false;

    std::string value(reinterpret_cast<const char*>(data), size);
    jstring jkey = env->NewStringUTF(key);
    jstring jvalue = env->NewStringUTF(value.c_str());
    if (clear_exception(env, "NewStringUTF(addSpoof)")) {
        if (jkey) env->DeleteLocalRef(jkey);
        if (jvalue) env->DeleteLocalRef(jvalue);
        return false;
    }

    env->CallStaticVoidMethod(mcClass, mid, jkey, jvalue);
    env->DeleteLocalRef(jkey);
    env->DeleteLocalRef(jvalue);
    if (clear_exception(env, "McInterceptor.addSpoof")) return false;
    return true;
}




bool peek_request_body(JNIEnv* env, jobject request, std::string& outBody, jobject* outRequest);

// PocketCosmos' actual entitlement path: forward Minecraft's inventory
// request body to Bedrock Cosmos and return its inventory response. This is
// what makes restored/custom skin packs actually count as owned to Bedrock;
// changing price/ownership fields on ItemDetail pages alone is not enough.
jobject fetch_cosmos_inventory(JNIEnv* env, jobject request, jobject chain) {
    (void)request;
    (void)chain;
    jclass clientCls = env->FindClass("okhttp3/OkHttpClient");
    if (clear_exception(env, "FindClass(OkHttpClient)")) return nullptr;
    jmethodID clientCtor = env->GetMethodID(clientCls, "<init>", "()V");
    jmethodID newCall = env->GetMethodID(clientCls, "newCall", "(Lokhttp3/Request;)Lokhttp3/Call;");
    if (clear_exception(env, "OkHttpClient methods")) return nullptr;
    jobject client = env->NewObject(clientCls, clientCtor);
    if (clear_exception(env, "OkHttpClient ctor") || !client) return nullptr;

    // Reuse the exact original request body.
    jclass reqCls = env->GetObjectClass(request);
    jmethodID bodyMid = env->GetMethodID(reqCls, "body", "()Lokhttp3/RequestBody;");
    jobject originalBody = bodyMid ? env->CallObjectMethod(request, bodyMid) : nullptr;
    if (clear_exception(env, "inventory Request.body") || !originalBody) {
        env->DeleteLocalRef(client);
        return nullptr;
    }

    jclass bodyCls = env->GetObjectClass(originalBody);
    jmethodID bodyStringMid = env->GetMethodID(bodyCls, "toString", "()Ljava/lang/String;");
    (void)bodyStringMid;

    // RequestBody is one-shot in some OkHttp versions, so use the same
    // safe peek/rebuild helper used by PlayFab handling.
    std::string inventoryBody;
    jobject rebuiltRequest = nullptr;
    if (!peek_request_body(env, request, inventoryBody, &rebuiltRequest) || inventoryBody.empty()) {
        if (rebuiltRequest) env->DeleteLocalRef(rebuiltRequest);
        env->DeleteLocalRef(originalBody);
        env->DeleteLocalRef(client);
        return nullptr;
    }

    jclass mediaTypeCls = env->FindClass("okhttp3/MediaType");
    jmethodID mediaGet = env->GetStaticMethodID(mediaTypeCls, "get", "(Ljava/lang/String;)Lokhttp3/MediaType;");
    jstring mediaName = env->NewStringUTF("application/json; charset=utf-8");
    jobject media = env->CallStaticObjectMethod(mediaTypeCls, mediaGet, mediaName);
    jclass requestBodyCls = env->FindClass("okhttp3/RequestBody");
    jmethodID rbCreate = env->GetStaticMethodID(requestBodyCls, "create", "([BLokhttp3/MediaType;)Lokhttp3/RequestBody;");
    jbyteArray bytes = env->NewByteArray(static_cast<jsize>(inventoryBody.size()));
    env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(inventoryBody.size()),
                            reinterpret_cast<const jbyte*>(inventoryBody.data()));
    jobject cosmosBody = env->CallStaticObjectMethod(requestBodyCls, rbCreate, bytes, media);
    if (clear_exception(env, "RequestBody.create(inventory)")) {
        if (rebuiltRequest) env->DeleteLocalRef(rebuiltRequest);
        env->DeleteLocalRef(originalBody);
        env->DeleteLocalRef(client);
        return nullptr;
    }

    jclass builderCls = env->FindClass("okhttp3/Request$Builder");
    jmethodID builderCtor = env->GetMethodID(builderCls, "<init>", "()V");
    jmethodID urlMid = env->GetMethodID(builderCls, "url", "(Ljava/lang/String;)Lokhttp3/Request$Builder;");
    jmethodID postMid = env->GetMethodID(builderCls, "post", "(Lokhttp3/RequestBody;)Lokhttp3/Request$Builder;");
    jmethodID buildMid = env->GetMethodID(builderCls, "build", "()Lokhttp3/Request;");
    jobject builder = env->NewObject(builderCls, builderCtor);
    jstring cosmosUrl = env->NewStringUTF("https://bedrock-cosmos.app/api/v1.0/player/inventory?includeReceipt=true");
    env->CallObjectMethod(builder, urlMid, cosmosUrl);
    env->CallObjectMethod(builder, postMid, cosmosBody);
    jobject cosmosRequest = env->CallObjectMethod(builder, buildMid);
    if (clear_exception(env, "build Cosmos inventory request") || !cosmosRequest) {
        if (rebuiltRequest) env->DeleteLocalRef(rebuiltRequest);
        env->DeleteLocalRef(originalBody); env->DeleteLocalRef(client);
        return nullptr;
    }

    jobject call = env->CallObjectMethod(client, newCall, cosmosRequest);
    jclass callCls = env->GetObjectClass(call);
    jmethodID executeMid = env->GetMethodID(callCls, "execute", "()Lokhttp3/Response;");
    jobject cosmosResponse = env->CallObjectMethod(call, executeMid);
    if (clear_exception(env, "Cosmos inventory execute") || !cosmosResponse) {
        if (rebuiltRequest) env->DeleteLocalRef(rebuiltRequest);
        env->DeleteLocalRef(originalBody); env->DeleteLocalRef(client);
        return nullptr;
    }

    jclass responseCls = env->GetObjectClass(cosmosResponse);
    jmethodID bodyOutMid = env->GetMethodID(responseCls, "body", "()Lokhttp3/ResponseBody;");
    jobject responseBody = env->CallObjectMethod(cosmosResponse, bodyOutMid);
    std::string result;
    if (!clear_exception(env, "Cosmos inventory response.body") && responseBody) {
        jclass responseBodyCls = env->GetObjectClass(responseBody);
        jmethodID stringMid = env->GetMethodID(responseBodyCls, "string", "()Ljava/lang/String;");
        jstring text = static_cast<jstring>(env->CallObjectMethod(responseBody, stringMid));
        if (!clear_exception(env, "Cosmos inventory body.string") && text) {
            result = jstring_value(env, text);
            env->DeleteLocalRef(text);
        }
        env->DeleteLocalRef(responseBodyCls);
        env->DeleteLocalRef(responseBody);
    }
    LOGI("[native] Cosmos entitlement inventory returned %zu bytes", result.size());
    env->DeleteLocalRef(responseCls);
    env->DeleteLocalRef(cosmosResponse);
    env->DeleteLocalRef(callCls);
    env->DeleteLocalRef(call);
    if (rebuiltRequest) env->DeleteLocalRef(rebuiltRequest);
    env->DeleteLocalRef(originalBody);
    env->DeleteLocalRef(client);
    return result.empty() ? nullptr : build_json_response(env, request, result);
}

jobject build_json_response(JNIEnv* env, jobject request, const std::string& json) {
    jclass rb = env->FindClass("okhttp3/Response$Builder");
    if (clear_exception(env, "FindClass(Response.Builder)")) return nullptr;
    jmethodID ctor = env->GetMethodID(rb, "<init>", "()V");
    jmethodID req = env->GetMethodID(rb, "request", "(Lokhttp3/Request;)Lokhttp3/Response$Builder;");
    jmethodID protocol = env->GetMethodID(rb, "protocol", "(Lokhttp3/Protocol;)Lokhttp3/Response$Builder;");
    jmethodID code = env->GetMethodID(rb, "code", "(I)Lokhttp3/Response$Builder;");
    jmethodID message = env->GetMethodID(rb, "message", "(Ljava/lang/String;)Lokhttp3/Response$Builder;");
    jmethodID header = env->GetMethodID(rb, "header", "(Ljava/lang/String;Ljava/lang/String;)Lokhttp3/Response$Builder;");
    jmethodID body = env->GetMethodID(rb, "body", "(Lokhttp3/ResponseBody;)Lokhttp3/Response$Builder;");
    jmethodID build = env->GetMethodID(rb, "build", "()Lokhttp3/Response;");
    if (clear_exception(env, "Response.Builder methods")) return nullptr;
    jobject b = env->NewObject(rb, ctor);
    if (clear_exception(env, "Response.Builder ctor")) return nullptr;
    env->CallObjectMethod(b, req, request);
    if (clear_exception(env, "Response.Builder.request")) return nullptr;
    jclass proto = env->FindClass("okhttp3/Protocol");
    jfieldID http11 = env->GetStaticFieldID(proto, "HTTP_1_1", "Lokhttp3/Protocol;");
    jobject p = env->GetStaticObjectField(proto, http11);
    env->CallObjectMethod(b, protocol, p);
    jstring ok = env->NewStringUTF("OK");
    env->CallObjectMethod(b, code, 200);
    env->CallObjectMethod(b, message, ok);
    jstring ctName = env->NewStringUTF("Content-Type");
    jstring ctVal = env->NewStringUTF("application/json; charset=utf-8");
    env->CallObjectMethod(b, header, ctName, ctVal);
    jclass mt = env->FindClass("okhttp3/MediaType");
    jmethodID parse = env->GetStaticMethodID(mt, "parse", "(Ljava/lang/String;)Lokhttp3/MediaType;");
    jstring mts = env->NewStringUTF("application/json; charset=utf-8");
    jobject media = env->CallStaticObjectMethod(mt, parse, mts);
    jclass rbody = env->FindClass("okhttp3/ResponseBody");
    jmethodID create = env->GetStaticMethodID(rbody, "create", "(Lokhttp3/MediaType;Ljava/lang/String;)Lokhttp3/ResponseBody;");
    jstring js = env->NewStringUTF(json.c_str());
    jobject rbod = env->CallStaticObjectMethod(rbody, create, media, js);
    env->CallObjectMethod(b, body, rbod);
    jobject out = env->CallObjectMethod(b, build);
    clear_exception(env, "build synthetic response");
    return out;
}

jclass load_java_interceptor_dex(JNIEnv* env, jobject app) {
    return load_embedded_dex(env, app, kMcInterceptorDexJava, kMcInterceptorDexJava_size, "Java");
}

jclass load_native_interceptor_dex(JNIEnv* env, jobject app) {
    return load_embedded_dex(env, app, kMcInterceptorDexNative, kMcInterceptorDexNative_size, "native");
}


bool peek_request_body(JNIEnv* env, jobject request, std::string& outBody, jobject* outRequest) {
    outBody.clear();
    if (outRequest) *outRequest = nullptr;

    jclass rc = env->GetObjectClass(request);
    jmethodID bodyMid = env->GetMethodID(rc, "body", "()Lokhttp3/RequestBody;");
    if (clear_exception(env, "Request.body method")) return false;
    jobject body = env->CallObjectMethod(request, bodyMid);
    if (clear_exception(env, "Request.body") || !body) return false;

    jclass buf = env->FindClass("okio/Buffer");
    jmethodID bufCtor = env->GetMethodID(buf, "<init>", "()V");
    jobject buffer = env->NewObject(buf, bufCtor);
    if (clear_exception(env, "Buffer ctor")) return false;

    jclass rb = env->GetObjectClass(body);
    jmethodID writeTo = env->GetMethodID(rb, "writeTo", "(Lokio/BufferedSink;)V");
    env->CallVoidMethod(body, writeTo, buffer);
    if (clear_exception(env, "RequestBody.writeTo")) return false;

    jmethodID cloneMid = env->GetMethodID(buf, "clone", "()Lokio/Buffer;");
    jobject clone = env->CallObjectMethod(buffer, cloneMid);
    jmethodID readUtf8 = env->GetMethodID(buf, "readUtf8", "()Ljava/lang/String;");
    jstring text = static_cast<jstring>(env->CallObjectMethod(clone, readUtf8));
    if (clear_exception(env, "Buffer.readUtf8") || !text) return false;
    const char* chars = env->GetStringUTFChars(text, nullptr);
    outBody = chars ? chars : "";
    if (chars) env->ReleaseStringUTFChars(text, chars);

    // Rebuild the RequestBody so a body-aware interceptor can safely call
    // chain.proceed() afterward. This mirrors PocketCosmos's peekRequestBody().
    jmethodID readBytes = env->GetMethodID(buf, "readByteArray", "()[B");
    jbyteArray bytes = static_cast<jbyteArray>(env->CallObjectMethod(buffer, readBytes));
    if (clear_exception(env, "Buffer.readByteArray")) return false;

    jclass rbc = env->GetObjectClass(body);
    jmethodID contentType = env->GetMethodID(rbc, "contentType", "()Lokhttp3/MediaType;");
    jobject media = env->CallObjectMethod(body, contentType);
    if (clear_exception(env, "RequestBody.contentType")) return false;

    jclass requestBodyCls = env->FindClass("okhttp3/RequestBody");
    jmethodID create = env->GetStaticMethodID(requestBodyCls, "create", "([BLokhttp3/MediaType;)Lokhttp3/RequestBody;");
    jobject rebuiltBody = env->CallStaticObjectMethod(requestBodyCls, create, bytes, media);
    if (clear_exception(env, "RequestBody.create")) return false;

    jmethodID methodMid = env->GetMethodID(rc, "method", "()Ljava/lang/String;");
    jstring method = static_cast<jstring>(env->CallObjectMethod(request, methodMid));
    jclass reqBuilderCls = env->FindClass("okhttp3/Request$Builder");
    jmethodID reqBuilderCtor = env->GetMethodID(reqBuilderCls, "<init>", "(Lokhttp3/Request;)V");
    jmethodID reqMethod = env->GetMethodID(reqBuilderCls, "method", "(Ljava/lang/String;Lokhttp3/RequestBody;)Lokhttp3/Request$Builder;");
    jmethodID reqBuild = env->GetMethodID(reqBuilderCls, "build", "()Lokhttp3/Request;");
    jobject builder = env->NewObject(reqBuilderCls, reqBuilderCtor, request);
    env->CallObjectMethod(builder, reqMethod, method, rebuiltBody);
    jobject rebuiltRequest = env->CallObjectMethod(builder, reqBuild);
    if (clear_exception(env, "rebuild Request")) return false;
    if (outRequest) *outRequest = env->NewGlobalRef(rebuiltRequest);
    return true;
}



std::string jstring_value(JNIEnv* env, jobject obj) {
    if (!obj) return {};
    const char* c = env->GetStringUTFChars(static_cast<jstring>(obj), nullptr);
    std::string out = c ? c : "";
    if (c) env->ReleaseStringUTFChars(static_cast<jstring>(obj), c);
    return out;
}

jobject json_obj(JNIEnv* env, const char* raw) {
    jclass cls = env->FindClass("org/json/JSONObject");
    if (clear_exception(env, "FindClass(JSONObject)")) return nullptr;
    jmethodID ctor = env->GetMethodID(cls, "<init>", "(Ljava/lang/String;)V");
    if (clear_exception(env, "JSONObject ctor")) return nullptr;
    jstring s = env->NewStringUTF(raw ? raw : "{}");
    jobject o = env->NewObject(cls, ctor, s);
    env->DeleteLocalRef(s);
    if (clear_exception(env, "JSONObject parse")) return nullptr;
    return o;
}

jobject json_array_get(JNIEnv* env, jobject arr, int index) {
    if (!arr) return nullptr;
    jclass cls = env->GetObjectClass(arr);
    jmethodID get = env->GetMethodID(cls, "get", "(I)Ljava/lang/Object;");
    if (clear_exception(env, "JSONArray.get")) return nullptr;
    jobject o = env->CallObjectMethod(arr, get, index);
    if (clear_exception(env, "JSONArray.get call")) return nullptr;
    return o;
}

int json_array_len(JNIEnv* env, jobject arr) {
    if (!arr) return 0;
    jclass cls = env->GetObjectClass(arr);
    jmethodID len = env->GetMethodID(cls, "length", "()I");
    if (clear_exception(env, "JSONArray.length")) return 0;
    return env->CallIntMethod(arr, len);
}

jobject json_opt_obj(JNIEnv* env, jobject obj, const char* key) {
    if (!obj) return nullptr;
    jclass cls = env->GetObjectClass(obj);
    jmethodID mid = env->GetMethodID(cls, "optJSONObject", "(Ljava/lang/String;)Lorg/json/JSONObject;");
    if (clear_exception(env, "JSONObject.optJSONObject")) return nullptr;
    jstring k = env->NewStringUTF(key);
    jobject out = env->CallObjectMethod(obj, mid, k);
    env->DeleteLocalRef(k);
    if (clear_exception(env, "JSONObject.optJSONObject call")) return nullptr;
    return out;
}

jobject json_opt_array(JNIEnv* env, jobject obj, const char* key) {
    if (!obj) return nullptr;
    jclass cls = env->GetObjectClass(obj);
    jmethodID mid = env->GetMethodID(cls, "optJSONArray", "(Ljava/lang/String;)Lorg/json/JSONArray;");
    if (clear_exception(env, "JSONObject.optJSONArray")) return nullptr;
    jstring k = env->NewStringUTF(key);
    jobject out = env->CallObjectMethod(obj, mid, k);
    env->DeleteLocalRef(k);
    if (clear_exception(env, "JSONObject.optJSONArray call")) return nullptr;
    return out;
}

std::string json_opt_string(JNIEnv* env, jobject obj, const char* key) {
    if (!obj) return {};
    jclass cls = env->GetObjectClass(obj);
    jmethodID mid = env->GetMethodID(cls, "optString", "(Ljava/lang/String;)Ljava/lang/String;");
    if (clear_exception(env, "JSONObject.optString")) return {};
    jstring k = env->NewStringUTF(key);
    jobject out = env->CallObjectMethod(obj, mid, k);
    env->DeleteLocalRef(k);
    if (clear_exception(env, "JSONObject.optString call") || !out) return {};
    std::string value = jstring_value(env, out);
    env->DeleteLocalRef(out);
    return value;
}

int json_opt_int(JNIEnv* env, jobject obj, const char* key, int fallback) {
    if (!obj) return fallback;
    jclass cls = env->GetObjectClass(obj);
    jmethodID mid = env->GetMethodID(cls, "optInt", "(Ljava/lang/String;I)I");
    if (clear_exception(env, "JSONObject.optInt")) return fallback;
    jstring k = env->NewStringUTF(key);
    jint out = env->CallIntMethod(obj, mid, k, fallback);
    env->DeleteLocalRef(k);
    if (clear_exception(env, "JSONObject.optInt call")) return fallback;
    return out;
}

bool json_put(JNIEnv* env, jobject obj, const char* key, jobject value) {
    if (!obj) return false;
    jclass cls = env->GetObjectClass(obj);
    jmethodID mid = env->GetMethodID(cls, "put", "(Ljava/lang/String;Ljava/lang/Object;)Lorg/json/JSONObject;");
    if (clear_exception(env, "JSONObject.put")) return false;
    jstring k = env->NewStringUTF(key);
    env->CallObjectMethod(obj, mid, k, value);
    env->DeleteLocalRef(k);
    return !clear_exception(env, "JSONObject.put call");
}

bool json_put_int(JNIEnv* env, jobject obj, const char* key, int value) {
    if (!obj) return false;
    jclass cls = env->GetObjectClass(obj);
    jmethodID mid = env->GetMethodID(cls, "put", "(Ljava/lang/String;I)Lorg/json/JSONObject;");
    if (clear_exception(env, "JSONObject.put int")) return false;
    jstring k = env->NewStringUTF(key);
    env->CallObjectMethod(obj, mid, k, value);
    env->DeleteLocalRef(k);
    return !clear_exception(env, "JSONObject.put int call");
}

jobject json_new_array(JNIEnv* env) {
    jclass cls = env->FindClass("org/json/JSONArray");
    if (clear_exception(env, "FindClass(JSONArray)")) return nullptr;
    jmethodID ctor = env->GetMethodID(cls, "<init>", "()V");
    if (clear_exception(env, "JSONArray ctor")) return nullptr;
    return env->NewObject(cls, ctor);
}

bool json_array_put(JNIEnv* env, jobject arr, jobject value) {
    if (!arr) return false;
    jclass cls = env->GetObjectClass(arr);
    jmethodID mid = env->GetMethodID(cls, "put", "(Ljava/lang/Object;)Lorg/json/JSONArray;");
    if (clear_exception(env, "JSONArray.put")) return false;
    env->CallObjectMethod(arr, mid, value);
    return !clear_exception(env, "JSONArray.put call");
}

jobject json_to_string(JNIEnv* env, jobject obj) {
    if (!obj) return nullptr;
    jclass cls = env->GetObjectClass(obj);
    jmethodID mid = env->GetMethodID(cls, "toString", "()Ljava/lang/String;");
    if (clear_exception(env, "JSON.toString")) return nullptr;
    jobject s = env->CallObjectMethod(obj, mid);
    if (clear_exception(env, "JSON.toString call")) return nullptr;
    return s;
}

std::string transform_persona_profile(JNIEnv* env, const std::string& original) {
    jobject root = json_obj(env, original.c_str());
    if (!root) return original;
    std::string personaRaw(reinterpret_cast<const char*>(kProfilePersonaAppend), kProfilePersonaAppend_size);
    std::string skinsRaw(reinterpret_cast<const char*>(kProfileSkinsAppend), kProfileSkinsAppend_size);
    jobject persona = json_obj(env, personaRaw.c_str());
    jobject skins = json_obj(env, skinsRaw.c_str());
    if (!persona || !skins) return original;
    jobject personaItems = json_opt_array(env, persona, "items");
    jobject skinsItems = json_opt_array(env, skins, "items");
    jobject result = json_opt_obj(env, root, "result");
    jobject layout = json_opt_array(env, result, "layout");
    if (!personaItems || !skinsItems || !layout || json_array_len(env, layout) == 0) return original;
    jobject firstLayout = json_array_get(env, layout, 0);
    jobject rows = json_opt_array(env, firstLayout, "rows");
    if (!rows) return original;

    const int rowCount = json_array_len(env, rows);
    for (int i = 0; i < rowCount; ++i) {
        jobject row = json_array_get(env, rows, i);
        if (!row) continue;
        if (json_opt_string(env, row, "controlId") != "StoreRow") { env->DeleteLocalRef(row); continue; }
        jobject components = json_opt_array(env, row, "components");
        if (!components) { env->DeleteLocalRef(row); continue; }
        int dropdownId = -1;
        jobject itemList = nullptr;
        for (int j = 0; j < json_array_len(env, components); ++j) {
            jobject comp = json_array_get(env, components, j);
            if (!comp) continue;
            std::string type = json_opt_string(env, comp, "type");
            if (type == "dropdownOptionComp") dropdownId = json_opt_int(env, comp, "dropdownId", -1);
            else if (type == "itemListComp") itemList = comp;
            else env->DeleteLocalRef(comp);
        }
        if (!itemList) { env->DeleteLocalRef(components); env->DeleteLocalRef(row); continue; }
        jobject existing = json_opt_array(env, itemList, "items");
        jobject add = dropdownId == 0 ? personaItems : (dropdownId == 1 ? skinsItems : nullptr);
        if (!existing || !add) {
            env->DeleteLocalRef(itemList); env->DeleteLocalRef(components); env->DeleteLocalRef(row); continue;
        }
        jobject updated = json_new_array(env);
        for (int k = 0; k < json_array_len(env, add); ++k) { jobject v=json_array_get(env,add,k); if(v){json_array_put(env,updated,v);env->DeleteLocalRef(v);} }
        for (int k = 0; k < json_array_len(env, existing); ++k) { jobject v=json_array_get(env,existing,k); if(v){json_array_put(env,updated,v);env->DeleteLocalRef(v);} }
        int total = json_array_len(env, updated);
        json_put(env, itemList, "items", updated);
        json_put_int(env, itemList, "totalItems", total);
        jobject config = json_opt_obj(env, itemList, "customStoreRowConfiguration");
        if (config) { json_put_int(env, config, "maxOffers", total); env->DeleteLocalRef(config); }
        env->DeleteLocalRef(updated); env->DeleteLocalRef(existing); env->DeleteLocalRef(itemList); env->DeleteLocalRef(components); env->DeleteLocalRef(row);
    }
    jobject out = json_to_string(env, root);
    std::string resultStr = out ? jstring_value(env, out) : original;
    if (out) env->DeleteLocalRef(out);
    return resultStr.empty() ? original : resultStr;
}

std::string json_string_field(const std::string& body, const char* field) {
    std::string needle = "\"" + std::string(field) + "\"";
    size_t p = body.find(needle);
    if (p == std::string::npos) return {};
    p = body.find(':', p + needle.size());
    if (p == std::string::npos) return {};
    p = body.find('"', p + 1);
    if (p == std::string::npos) return {};
    size_t e = p + 1;
    while (e < body.size()) {
        if (body[e] == '"' && body[e - 1] != '\\') break;
        ++e;
    }
    if (e >= body.size()) return {};
    return body.substr(p + 1, e - p - 1);
}


jobject native_intercept(JNIEnv* env, jobject self, jobject chain) {
    jclass cc = env->GetObjectClass(chain);
    jmethodID requestMid = env->GetMethodID(cc, "request", "()Lokhttp3/Request;");
    jobject request = env->CallObjectMethod(chain, requestMid);
    if (clear_exception(env, "Chain.request") || !request) return nullptr;

    jclass rc = env->GetObjectClass(request);
    jmethodID urlMid = env->GetMethodID(rc, "url", "()Lokhttp3/HttpUrl;");
    jobject httpUrl = env->CallObjectMethod(request, urlMid);
    jclass uc = env->GetObjectClass(httpUrl);
    jmethodID toString = env->GetMethodID(uc, "toString", "()Ljava/lang/String;");
    jstring ju = static_cast<jstring>(env->CallObjectMethod(httpUrl, toString));
    const char* cu = ju ? env->GetStringUTFChars(ju, nullptr) : nullptr;
    std::string url = cu ? cu : "";
    if (cu) env->ReleaseStringUTFChars(ju, cu);

    const bool isPlayfabItem =
        url.find("20ca2.playfabapi.com/Catalog/GetPublishedItem") != std::string::npos;
    const bool isPlayfabSearch =
        url.find("20ca2.playfabapi.com/Catalog/Search") != std::string::npos;
    const bool isPersonaProfile =
        url.find("/api/v2.0/layout/pages/DressingRoom_PersonaProfile") != std::string::npos;
    const bool isEntitlementInventory =
        url.find("entitlements.mktpl.minecraft-services.net/api/v1.0/player/inventory") != std::string::npos;

    if (isEntitlementInventory) {
        jobject entitlement = fetch_cosmos_inventory(env, request, chain);
        if (entitlement) return entitlement;
        LOGI("[native] Cosmos entitlement forwarding failed; falling through to real inventory");
    }

    jobject safeRequest = request;
    std::string body;
    if (isPlayfabItem || isPlayfabSearch) {
        jobject rebuilt = nullptr;
        if (peek_request_body(env, request, body, &rebuilt) && rebuilt) {
            safeRequest = rebuilt;
        } else {
            // If the body cannot be safely peeked, do not interfere with the
            // request at all. Let the original chain handle it.
            jmethodID proceed = env->GetMethodID(cc, "proceed", "(Lokhttp3/Request;)Lokhttp3/Response;");
            return env->CallObjectMethod(chain, proceed, request);
        }
    }

    if (isPlayfabItem) {
        std::string id = json_string_field(body, "ItemId");
        auto it = cosmos::kPlayfabItems.find(id);
        if (it != cosmos::kPlayfabItems.end()) {
            LOGI("[native] GetPublishedItem spoof %s", id.c_str());
            if (safeRequest != request && safeRequest) env->DeleteGlobalRef(safeRequest);
            return build_json_response(env, request, std::string(it->second));
        }
    }
    if (isPlayfabSearch) {
        size_t p = body.find("t eq '");
        if (p != std::string::npos) {
            p += 6; size_t e = body.find('\'', p);
            if (e != std::string::npos) {
                std::string id = body.substr(p, e-p);
                auto it = cosmos::kPlayfabSearch.find(id);
                if (it != cosmos::kPlayfabSearch.end()) {
                    LOGI("[native] PlayFab Search spoof %s", id.c_str());
                    if (safeRequest != request && safeRequest) env->DeleteGlobalRef(safeRequest);
                    return build_json_response(env, request, std::string(it->second));
                }
            }
        }
    }

    jmethodID proceed = env->GetMethodID(cc, "proceed", "(Lokhttp3/Request;)Lokhttp3/Response;");
    jobject response = env->CallObjectMethod(chain, proceed, safeRequest);
    if (safeRequest != request && safeRequest) env->DeleteGlobalRef(safeRequest);

    // IMPORTANT: Do not clear an exception thrown by chain.proceed().
    // OkHttp expects the interceptor to either return a Response or propagate
    // the exception. Clearing the network exception and returning nullptr makes
    // RealInterceptorChain.proceed() throw:
    // "interceptor com.mchook.McInterceptor returned null".
    //
    // The tombstone shows exactly this path: chain.proceed() threw a
    // java.net.ConnectException while joining geo.hivebedrock.cloud, then the
    // old code cleared it and returned the null Response.
    if (env->ExceptionCheck()) {
        LOGE("[native] Chain.proceed threw; propagating original exception");
        env->ExceptionDescribe();
        return nullptr; // pending JNI exception is intentionally preserved
    }

    if (isPersonaProfile && response) {
        jclass responseCls = env->GetObjectClass(response);
        jmethodID bodyMid = env->GetMethodID(responseCls, "body", "()Lokhttp3/ResponseBody;");
        jobject responseBody = bodyMid ? env->CallObjectMethod(response, bodyMid) : nullptr;
        if (!clear_exception(env, "Response.body") && responseBody) {
            jclass bodyCls = env->GetObjectClass(responseBody);
            jmethodID stringMid = env->GetMethodID(bodyCls, "string", "()Ljava/lang/String;");
            jstring raw = stringMid ? static_cast<jstring>(env->CallObjectMethod(responseBody, stringMid)) : nullptr;
            if (!clear_exception(env, "ResponseBody.string") && raw) {
                std::string original = jstring_value(env, raw);
                std::string updated = transform_persona_profile(env, original);
                env->DeleteLocalRef(raw);
                env->DeleteLocalRef(responseBody);
                env->DeleteLocalRef(responseCls);
                if (updated != original) {
                    LOGI("[native] DressingRoom_PersonaProfile preserved mode + appended Cosmos persona items");
                    return build_json_response(env, request, updated);
                }
                return build_json_response(env, request, original);
            }
            env->DeleteLocalRef(responseBody);
        }
        env->DeleteLocalRef(responseCls);
    }
    return response;
}

bool unsafe_set_static(JNIEnv* env, jclass clazz, jfieldID field, jobject value) {
    jclass unsafeCls = env->FindClass("sun/misc/Unsafe");
    if (clear_exception(env, "FindClass(Unsafe)")) return false;
    jfieldID theUnsafeField = env->GetStaticFieldID(unsafeCls, "theUnsafe", "Lsun/misc/Unsafe;");
    if (clear_exception(env, "Unsafe.theUnsafe field")) return false;
    jobject unsafe = env->GetStaticObjectField(unsafeCls, theUnsafeField);
    if (clear_exception(env, "Unsafe.theUnsafe get") || !unsafe) return false;

    jmethodID staticFieldBase = env->GetMethodID(unsafeCls, "staticFieldBase", "(Ljava/lang/reflect/Field;)Ljava/lang/Object;");
    jmethodID staticFieldOffset = env->GetMethodID(unsafeCls, "staticFieldOffset", "(Ljava/lang/reflect/Field;)J");
    jmethodID putObjectVolatile = env->GetMethodID(unsafeCls, "putObjectVolatile", "(Ljava/lang/Object;JLjava/lang/Object;)V");
    if (clear_exception(env, "Unsafe methods")) return false;

    jclass classCls = env->FindClass("java/lang/Class");
    jclass fieldCls = env->FindClass("java/lang/reflect/Field");
    if (clear_exception(env, "reflection classes")) return false;
    jmethodID getDeclaredField = env->GetMethodID(classCls, "getDeclaredField", "(Ljava/lang/String;)Ljava/lang/reflect/Field;");
    if (clear_exception(env, "Class.getDeclaredField")) return false;
    jstring name = env->NewStringUTF(kOkClientField);
    jobject reflectField = env->CallObjectMethod(clazz, getDeclaredField, name);
    env->DeleteLocalRef(name);
    if (clear_exception(env, "get OK_CLIENT Field") || !reflectField) return false;

    jobject base = env->CallObjectMethod(unsafe, staticFieldBase, reflectField);
    jlong offset = env->CallLongMethod(unsafe, staticFieldOffset, reflectField);
    if (clear_exception(env, "Unsafe static field metadata") || !base) return false;
    env->CallVoidMethod(unsafe, putObjectVolatile, base, offset, value);
    if (clear_exception(env, "Unsafe.putObjectVolatile")) return false;

    jobject check = env->GetStaticObjectField(clazz, field);
    bool ok = !clear_exception(env, "OK_CLIENT verification") && env->IsSameObject(check, value);
    if (check) env->DeleteLocalRef(check);
    env->DeleteLocalRef(base);
    env->DeleteLocalRef(reflectField);
    env->DeleteLocalRef(unsafe);
    env->DeleteLocalRef(fieldCls);
    env->DeleteLocalRef(classCls);
    env->DeleteLocalRef(unsafeCls);
    return ok;
}

bool inject_ok_client_pair(JNIEnv* env, jclass httpClass, jobject first, jobject second) {
    jfieldID fid = env->GetStaticFieldID(httpClass, kOkClientField, kOkHttpClientDesc);
    if (clear_exception(env, "HttpClientRequest.OK_CLIENT field")) return false;
    jobject client = env->GetStaticObjectField(httpClass, fid);
    if (clear_exception(env, "HttpClientRequest.OK_CLIENT get")) return false;
    if (!client) {
        LOGE("[dex] OK_CLIENT is null");
        return false;
    }

    jclass clientCls = env->GetObjectClass(client);
    jmethodID newBuilder = env->GetMethodID(clientCls, "newBuilder", "()Lokhttp3/OkHttpClient$Builder;");
    if (clear_exception(env, "OkHttpClient.newBuilder")) return false;
    jobject builder = env->CallObjectMethod(client, newBuilder);
    if (clear_exception(env, "OkHttpClient.newBuilder call")) return false;

    jclass builderCls = env->GetObjectClass(builder);
    jmethodID addInterceptor = env->GetMethodID(builderCls, "addInterceptor", "(Lokhttp3/Interceptor;)Lokhttp3/OkHttpClient$Builder;");
    if (clear_exception(env, "OkHttpClient.Builder.addInterceptor")) return false;

    env->CallObjectMethod(builder, addInterceptor, first);
    if (clear_exception(env, "OkHttpClient.Builder.addInterceptor first")) return false;
    env->CallObjectMethod(builder, addInterceptor, second);
    if (clear_exception(env, "OkHttpClient.Builder.addInterceptor second")) return false;

    jmethodID build = env->GetMethodID(builderCls, "build", "()Lokhttp3/OkHttpClient;");
    if (clear_exception(env, "OkHttpClient.Builder.build")) return false;
    jobject rebuilt = env->CallObjectMethod(builder, build);
    if (clear_exception(env, "OkHttpClient.Builder.build call")) return false;

    // From this point onward the live OK_CLIENT is about to change. Never retry
    // installation after this point: a second replacement would create a second
    // interceptor chain while Minecraft may already be using the first client.
    g_install_mutated.store(true, std::memory_order_release);

    env->SetStaticObjectField(httpClass, fid, rebuilt);
    if (!clear_exception(env, "HttpClientRequest.OK_CLIENT set")) {
        LOGI("[dex] rebuilt OkHttpClient once with native + Java interceptors");
        return true;
    }

    LOGW("[dex] direct OK_CLIENT assignment failed; trying Unsafe static-final write");
    if (unsafe_set_static(env, httpClass, fid, rebuilt)) {
        LOGI("[dex] OK_CLIENT field set with Unsafe and verified");
        return true;
    }
    return false;
}

bool install(JNIEnv* env) {
    jobject app = current_application(env);
    if (!app) return false;

    // Keep the known-good Java interceptor intact. This is the exact DEX from the
    // 19-cape build that is already proven to populate/equip without hanging.
    jclass javaClass = load_java_interceptor_dex(env, app);
    if (!javaClass) {
        LOGE("[dex] Java interceptor DEX installation failed");
        env->DeleteLocalRef(app);
        return false;
    }

    jobject javaInterceptor = new_interceptor(env, javaClass);
    if (!javaInterceptor) {
        LOGE("[dex] Java McInterceptor() failed");
        env->DeleteLocalRef(javaClass);
        env->DeleteLocalRef(app);
        return false;
    }
    if (!set_instance(env, javaClass, javaInterceptor)) {
        LOGE("[dex] failed to set Java McInterceptor.INSTANCE");
        env->DeleteLocalRef(javaInterceptor);
        env->DeleteLocalRef(javaClass);
        env->DeleteLocalRef(app);
        return false;
    }

    // Register every static MainPages mapping present in PocketCosmos MainResponses.json.
    // This is intentionally broader than the cape/creator pages: it covers the
    // complete local catalog/navigation set, including Minecraft Dungeons,
    // Minecraft Earth, Redeemable Items, Tiny Takeover, Creator Hub, every
    // creator persona/marketplace page, and the cape catalog itself.
    struct MainPage {
        const char* suffix;
        const std::uint8_t* bytes;
        std::size_t size;
    };
    const MainPage mainPages[] = {
        {"/api/v2.0/layout/pages/DressingRoom_Capes", kPage_Capes, kPage_Capes_size},
        {"/api/v2.0/layout/pages/MultiItemPage_BedrockCosmosMarketplaceCategoryPage", kPage_MainMarketplacePage, kPage_MainMarketplacePage_size},
        {"/api/v2.0/layout/pages/MultiItemPage_LegacyVault", kPage_LegacyVault, kPage_LegacyVault_size},
        {"/api/v2.0/layout/pages/MultiItemPage_MinecraftRestored", kPage_MinecraftRestored, kPage_MinecraftRestored_size},
        {"/api/v2.0/layout/pages/MultiItemPage_CreatorHub", kPage_Cosmos_Creator_Hub, kPage_Cosmos_Creator_Hub_size},
        {"/api/v2.0/layout/pages/PersonaCreatorPage_master_player_account!MinecraftCosmos", kPage_MinecraftCosmos_PersonaCreatorPage, kPage_MinecraftCosmos_PersonaCreatorPage_size},
        {"/api/v2.0/layout/pages/PersonaCreatorPage_master_player_account!SpinOffCapes", kPage_SpinOffCapes_PersonaCreatorPage, kPage_SpinOffCapes_PersonaCreatorPage_size},
        {"/api/v2.0/layout/pages/PersonaCreatorPage_master_player_account!BedrockCosmos", kPage_BedrockCosmos_PersonaCreatorPage, kPage_BedrockCosmos_PersonaCreatorPage_size},
        {"/api/v2.0/layout/pages/PersonaCreatorPage_master_player_account!BionicBen", kPage_BionicBen_Persona, kPage_BionicBen_Persona_size},
        {"/api/v2.0/layout/pages/PersonaCreatorPage_master_player_account!DL0ZE", kPage_DL0ZE_Persona, kPage_DL0ZE_Persona_size},
        {"/api/v2.0/layout/pages/PersonaCreatorPage_master_player_account!NAC", kPage_NAC_Persona, kPage_NAC_Persona_size},
        {"/api/v2.0/layout/pages/PersonaCreatorPage_master_player_account!Sability", kPage_Sability_Persona, kPage_Sability_Persona_size},
        {"/api/v2.0/layout/pages/PersonaCreatorPage_master_player_account!Stars", kPage_Stars_Persona, kPage_Stars_Persona_size},
        {"/api/v2.0/layout/pages/PersonaCreatorPage_master_player_account!Wizardry", kPage_Wizardry_Persona, kPage_Wizardry_Persona_size},
        {"/api/v2.0/layout/pages/MultiItemPage_Alleis_Elis_Marketplace", kPage_Alleis_Elis_Marketplace, kPage_Alleis_Elis_Marketplace_size},
        {"/api/v2.0/layout/pages/MultiItemPage_BionicBen_Marketplace", kPage_BionicBen_Marketplace, kPage_BionicBen_Marketplace_size},
        {"/api/v2.0/layout/pages/MultiItemPage_Exports_Marketplace", kPage_Exports_Marketplace, kPage_Exports_Marketplace_size},
        {"/api/v2.0/layout/pages/MultiItemPage_Mono_Marketplace", kPage_Mono_Marketplace, kPage_Mono_Marketplace_size},
        {"/api/v2.0/layout/pages/MultiItemPage_SRG64_Marketplace", kPage_SRG64_Marketplace, kPage_SRG64_Marketplace_size},
        {"/api/v2.0/layout/pages/MultiItemPage_YEH_Marketplace", kPage_YEH_Marketplace, kPage_YEH_Marketplace_size},
        {"/api/v2.0/layout/pages/MultiItemPage_ZeroMISTER_JA_Marketplace", kPage_ZeroMISTER_JA_Marketplace, kPage_ZeroMISTER_JA_Marketplace_size},
        {"/api/v2.0/layout/pages/PersonaCreatorPage_master_player_account!Essential", kPage_Essential_Persona, kPage_Essential_Persona_size},
        {"/api/v2.0/layout/pages/DressingRoom_CreeperCrunchCereal", kPage_CreeperCrunchCereal, kPage_CreeperCrunchCereal_size},
        {"/api/v2.0/layout/pages/DressingRoom_MinecraftDungeons", kPage_MinecraftDungeons, kPage_MinecraftDungeons_size},
        {"/api/v2.0/layout/pages/DressingRoom_MinecraftEarth", kPage_MinecraftEarth, kPage_MinecraftEarth_size},
        {"/api/v2.0/layout/pages/DressingRoom_RedeemableItems", kPage_RedeemableItems, kPage_RedeemableItems_size},
        {"/api/v2.0/layout/pages/DressingRoom_TinyTakeover", kPage_TinyTakeover, kPage_TinyTakeover_size},
        {"/api/v2.0/layout/pages/MultiItemPage_HiddenDLC", kPage_HiddenDLC, kPage_HiddenDLC_size},
        {"/api/v2.0/layout/pages/MultiItemPage_PersonaSkinSelector%7cSkinPack_60a2047b-fe99-4a1d-9ccf-79d6a9b63adb", kSkinPackViewer_60a2047b_fe99_4a1d_9ccf_79d6a9b63adb, kSkinPackViewer_60a2047b_fe99_4a1d_9ccf_79d6a9b63adb_size},
        {"/api/v2.0/layout/pages/MultiItemPage_PersonaSkinSelector%7cSkinPack_c65b1919-df90-4c9b-b3d5-a59c7d7a3979", kSkinPackViewer_c65b1919_df90_4c9b_b3d5_a59c7d7a3979, kSkinPackViewer_c65b1919_df90_4c9b_b3d5_a59c7d7a3979_size},
        {"/api/v2.0/layout/pages/MultiItemPage_PersonaSkinSelector%7cSkinPack_62c68fd4-3075-40ed-8b7b-15dcc7b667ff", kSkinPackViewer_62c68fd4_3075_40ed_8b7b_15dcc7b667ff, kSkinPackViewer_62c68fd4_3075_40ed_8b7b_15dcc7b667ff_size},
        {"/api/v2.0/layout/pages/MultiItemPage_PersonaSkinSelector%7cSkinPack_2f60a7a0-8010-49b1-8b57-851ac41f34cf", kSkinPackViewer_2f60a7a0_8010_49b1_8b57_851ac41f34cf, kSkinPackViewer_2f60a7a0_8010_49b1_8b57_851ac41f34cf_size},
        {"/api/v2.0/layout/pages/MultiItemPage_PersonaSkinSelector%7cSkinPack_80c85890-dad7-4d3a-b2ef-60071327b7cf", kSkinPackViewer_80c85890_dad7_4d3a_b2ef_60071327b7cf, kSkinPackViewer_80c85890_dad7_4d3a_b2ef_60071327b7cf_size},
        {"/api/v2.0/layout/pages/MultiItemPage_PersonaSkinSelector%7cSkinPack_4c61b1f5-2bbc-492f-8a2a-1f3b813c4113", kSkinPackViewer_4c61b1f5_2bbc_492f_8a2a_1f3b813c4113, kSkinPackViewer_4c61b1f5_2bbc_492f_8a2a_1f3b813c4113_size},
        {"/api/v2.0/layout/pages/MultiItemPage_PersonaSkinSelector%7cSkinPack_1fc32dd0-b399-4255-a4c7-e6e262a40ab6", kSkinPackViewer_1fc32dd0_b399_4255_a4c7_e6e262a40ab6, kSkinPackViewer_1fc32dd0_b399_4255_a4c7_e6e262a40ab6_size},
        {"/api/v2.0/layout/pages/MultiItemPage_PersonaSkinSelector%7cSkinPack_358a8710-bcf4-4cd7-ae86-a63ffec500c8", kSkinPackViewer_358a8710_bcf4_4cd7_ae86_a63ffec500c8, kSkinPackViewer_358a8710_bcf4_4cd7_ae86_a63ffec500c8_size},
        {"/api/v2.0/layout/pages/MultiItemPage_PersonaSkinSelector%7cSkinPack_5b13cb52-e2bf-438c-97f2-1105c54929d9", kSkinPackViewer_5b13cb52_e2bf_438c_97f2_1105c54929d9, kSkinPackViewer_5b13cb52_e2bf_438c_97f2_1105c54929d9_size},
    };
    for (const auto& page : mainPages) {
        const bool ok = add_spoof(env, javaClass, page.suffix, page.bytes, page.size);
        LOGI("[java] MainPages spoof %s: %d", page.suffix, ok ? 1 : 0);
    }

    // PocketCosmos also statically serves the complete SkinPacks catalog pages.
    // These include productId/..., ItemDetail_..., and packId/... routes for
    // creator packs such as BionicBen, Alleis, SRG64, Exports, YEH, etc.
    // Without these routes Minecraft falls through to the real marketplace and
    // shows "Cannot access the marketplace" when a catalog item is opened.
    for (std::size_t i = 0; i < cosmos::kSkinPackPagesCount; ++i) {
        const auto& page = cosmos::kSkinPackPages[i];
        const bool ok = add_spoof(env, javaClass, page.suffix, page.data, page.size);
        LOGI("[java] SkinPacks spoof %s: %d", page.suffix, ok ? 1 : 0);
    }

    // Separate class-loader definition of the same class name is intentional:
    // this second McInterceptor contains only the native body-aware PlayFab /
    // entitlement handling. It is chained AFTER the Java interceptor so normal
    // requests retain the exact known-good Java behavior.
    jclass nativeClass = load_native_interceptor_dex(env, app);
    if (!nativeClass) {
        LOGE("[dex] native interceptor DEX installation failed");
        env->DeleteLocalRef(javaInterceptor);
        env->DeleteLocalRef(javaClass);
        env->DeleteLocalRef(app);
        return false;
    }

    JNINativeMethod nativeMethods[] = {
        {"intercept", "(Lokhttp3/Interceptor$Chain;)Lokhttp3/Response;",
         reinterpret_cast<void*>(native_intercept)}
    };
    if (env->RegisterNatives(nativeClass, nativeMethods, 1) != JNI_OK ||
        clear_exception(env, "RegisterNatives(native McInterceptor)")) {
        LOGE("[dex] failed to bind native intercept");
        env->DeleteLocalRef(nativeClass);
        env->DeleteLocalRef(javaInterceptor);
        env->DeleteLocalRef(javaClass);
        env->DeleteLocalRef(app);
        return false;
    }

    jobject nativeInterceptor = new_interceptor(env, nativeClass);
    if (!nativeInterceptor) {
        LOGE("[dex] native McInterceptor() failed");
        env->DeleteLocalRef(nativeClass);
        env->DeleteLocalRef(javaInterceptor);
        env->DeleteLocalRef(javaClass);
        env->DeleteLocalRef(app);
        return false;
    }

    jobject parent = find_okhttp_loader(env, app);
    jclass httpClass = parent ? load_class(env, parent, kHttpClientDotClass) : nullptr;
    if (parent) env->DeleteGlobalRef(parent);
    if (!httpClass) {
        jobject contextLoader = context_class_loader(env);
        httpClass = contextLoader ? load_class(env, contextLoader, kHttpClientDotClass) : nullptr;
        if (contextLoader) env->DeleteLocalRef(contextLoader);
    }
    if (!httpClass) {
        LOGE("[dex] cannot resolve %s", kHttpClientDotClass);
        env->DeleteLocalRef(nativeInterceptor);
        env->DeleteLocalRef(nativeClass);
        env->DeleteLocalRef(javaInterceptor);
        env->DeleteLocalRef(javaClass);
        env->DeleteLocalRef(app);
        return false;
    }

    // Build the client ONCE. Native is first so it can answer PlayFab body-aware
    // requests; Java is second and retains the exact known-good cape catalogue
    // interceptor. Never replace the live OK_CLIENT twice.
    if (!inject_ok_client_pair(env, httpClass, nativeInterceptor, javaInterceptor)) {
        LOGE("[dex] atomic dual-interceptor OK_CLIENT injection failed");
        env->DeleteLocalRef(httpClass);
        env->DeleteLocalRef(nativeInterceptor);
        env->DeleteLocalRef(nativeClass);
        env->DeleteLocalRef(javaInterceptor);
        env->DeleteLocalRef(javaClass);
        env->DeleteLocalRef(app);
        return false;
    }

    LOGI("[dex] atomic dual-interceptor install complete");

    env->DeleteLocalRef(httpClass);
    env->DeleteLocalRef(nativeInterceptor);
    env->DeleteLocalRef(nativeClass);
    env->DeleteLocalRef(javaInterceptor);
    env->DeleteLocalRef(javaClass);
    env->DeleteLocalRef(app);
    return true;
}

} // namespace

__attribute__((constructor))
static void native_constructor() {
    LOGI("[mchook] ELF constructor");
}

extern "C" jint JNI_OnLoad(JavaVM* vm, void*) {
    g_vm = vm;
    LOGI("[mchook] JNI_OnLoad START");

    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK || !env) {
        LOGE("[mchook] GetEnv failed");
        return JNI_VERSION_1_6;
    }

    (void)env;
    std::thread([] {
        for (int attempt = 0; attempt < 120; ++attempt) {
            if (g_install_complete.load(std::memory_order_acquire) ||
                g_install_mutated.load(std::memory_order_acquire)) {
                return;
            }
            JNIEnv* e = nullptr;
            bool attached = false;
            if (g_vm->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6) != JNI_OK) {
                if (g_vm->AttachCurrentThread(&e, nullptr) != JNI_OK) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    continue;
                }
                attached = true;
            }
            if (e && install(e)) {
                g_install_complete.store(true, std::memory_order_release);
                LOGI("[mchook] delayed install succeeded on attempt %d", attempt + 1);
                if (attached) g_vm->DetachCurrentThread();
                return;
            }
            if (attached) g_vm->DetachCurrentThread();
            // If installation got as far as replacing OK_CLIENT, never run it again.
            if (g_install_mutated.load(std::memory_order_acquire)) {
                LOGE("[mchook] install mutated OK_CLIENT but did not complete; refusing retry");
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        LOGE("[mchook] delayed install exhausted without success");
    }).detach();
    LOGI("[mchook] JNI_OnLoad scheduled delayed installation");
    return JNI_VERSION_1_6;
}
