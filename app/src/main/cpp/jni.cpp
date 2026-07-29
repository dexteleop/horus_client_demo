#include <jni.h>
#include <android/log.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "talite_xrstream.h"

#define LOG_TAG "HorusJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

JavaVM* g_vm = nullptr;

// Cache CameraCalibration class global ref so native callback threads can use it
// (FindClass on native threads uses system ClassLoader which can't find app classes).
jclass g_calib_class = nullptr;
jmethodID g_calib_ctor = nullptr;

// 获取当前线程的 JNIEnv，必要时 attach 当前线程并在析构时 detach。
class ScopedEnv {
public:
    explicit ScopedEnv(JavaVM* vm) : vm_(vm), env_(nullptr), need_detach_(false) {
        if (!vm_) return;
        void* raw = nullptr;
        int stat = vm_->GetEnv(&raw, JNI_VERSION_1_6);
        env_ = static_cast<JNIEnv*>(raw);
        if (stat == JNI_EDETACHED) {
            JavaVMAttachArgs args = {};
            args.version = JNI_VERSION_1_6;
            args.name = const_cast<char*>("HorusNativeCB");
            if (vm_->AttachCurrentThread(&env_, &args) == 0) {
                need_detach_ = true;
            }
        }
    }
    ~ScopedEnv() {
        if (need_detach_ && env_ != nullptr) {
            vm_->DetachCurrentThread();
        }
    }
    JNIEnv* env() const { return env_; }

private:
    JavaVM* vm_;
    JNIEnv* env_;
    bool need_detach_;
};

// 每个 NativeStreamer 实例对应一个 State，保存 context 和各类 Java 监听器。
struct State {
    TALITE_CONTEXT context = nullptr;
    jobject discovery_listener = nullptr;
    jobject h265_listener = nullptr;
    jobject error_listener = nullptr;
    jobject data_listener = nullptr;
    std::mutex listener_mutex;
};

std::mutex states_mutex;
std::vector<State*> states;

State* state(jlong handle) {
    return reinterpret_cast<State*>(handle);
}

// 把 talite_float_array / talite_int32_array 拷贝成 Java 数组。
jfloatArray to_jfloat_array(JNIEnv* env, const talite_float_array& arr) {
    if (!arr.data || arr.size == 0) return env->NewFloatArray(0);
    jfloatArray out = env->NewFloatArray(static_cast<jsize>(arr.size));
    if (out) env->SetFloatArrayRegion(out, 0, static_cast<jsize>(arr.size), arr.data);
    return out;
}

jintArray to_jint_array(JNIEnv* env, const talite_int32_array& arr) {
    if (!arr.data || arr.size == 0) return env->NewIntArray(0);
    jintArray out = env->NewIntArray(static_cast<jsize>(arr.size));
    if (out) env->SetIntArrayRegion(out, 0, static_cast<jsize>(arr.size), arr.data);
    return out;
}

// 收集所有实例上某种 listener 的 local ref，供回调线程统一转发。
template <typename Picker>
std::vector<jobject> collect_listeners(JNIEnv* env, Picker picker) {
    std::vector<jobject> listeners;
    for (State* value : states) {
        std::scoped_lock<std::mutex> lock(value->listener_mutex);
        jobject slot = picker(value);
        if (slot) listeners.push_back(env->NewLocalRef(slot));
    }
    return listeners;
}

// ---- 回调：talite_on_device_list ----
void on_device_list(const talite_device_list* list) {
    if (!list) return;
    ScopedEnv sc(g_vm);
    JNIEnv* env = sc.env();
    if (!env) return;

    std::vector<jobject> listeners;
    {
        std::scoped_lock<std::mutex> lock(states_mutex);
        listeners = collect_listeners(env, [](State* s) { return s->discovery_listener; });
    }

    for (jobject listener : listeners) {
        if (!listener) continue;
        jclass listener_class = env->GetObjectClass(listener);
        jmethodID on_device = env->GetMethodID(
                listener_class, "onDevice",
                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
        if (on_device) {
            for (size_t i = 0; i < list->device_count; ++i) {
                const talite_device_info& device = list->devices[i];
                jstring id = env->NewStringUTF(device.device_id ? device.device_id : "");
                jstring name = env->NewStringUTF(device.device_name ? device.device_name : "");
                jstring ip = env->NewStringUTF(device.device_ip ? device.device_ip : "");
                env->CallVoidMethod(listener, on_device, id, name, ip);
                env->DeleteLocalRef(id);
                env->DeleteLocalRef(name);
                env->DeleteLocalRef(ip);
                if (env->ExceptionCheck()) break;
            }
        }
        env->DeleteLocalRef(listener_class);
        env->DeleteLocalRef(listener);
    }
}

// ---- 回调：talite_on_h265_stream ----
void on_h265_stream(const talite_h265_stream_params* params) {
    if (!params || params->size == 0) return;
    ScopedEnv sc(g_vm);
    JNIEnv* env = sc.env();
    if (!env) return;

    std::vector<jobject> listeners;
    {
        std::scoped_lock<std::mutex> lock(states_mutex);
        listeners = collect_listeners(env, [](State* s) { return s->h265_listener; });
    }

    // H.265 数据仅在回调期间有效，这里拷贝成 Java byte[] 后立刻交给上层。
    jbyteArray bytes = env->NewByteArray(static_cast<jsize>(params->size));
    if (bytes) {
        env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(params->size),
                                reinterpret_cast<const jbyte*>(params->data));
    }
    for (jobject listener : listeners) {
        if (!listener) continue;
        jclass listener_class = env->GetObjectClass(listener);
        jmethodID on_h265 = env->GetMethodID(
                listener_class, "onH265Stream", "([BJZ)V");
        if (on_h265) {
            env->CallVoidMethod(listener, on_h265, bytes,
                                static_cast<jlong>(params->timestamp_us),
                                static_cast<jboolean>(params->key_frame));
        }
        env->DeleteLocalRef(listener_class);
        env->DeleteLocalRef(listener);
        if (env->ExceptionCheck()) break;
    }
    env->DeleteLocalRef(bytes);
}

// ---- 回调：talite_on_error ----
void on_error(TALITE_CONTEXT /*context*/, talite_result result, const char* message) {
    ScopedEnv sc(g_vm);
    JNIEnv* env = sc.env();
    if (!env) return;

    std::vector<jobject> listeners;
    {
        std::scoped_lock<std::mutex> lock(states_mutex);
        listeners = collect_listeners(env, [](State* s) { return s->error_listener; });
    }

    jstring msg = env->NewStringUTF(message ? message : "");
    for (jobject listener : listeners) {
        if (!listener) continue;
        jclass listener_class = env->GetObjectClass(listener);
        jmethodID on_error_method = env->GetMethodID(
                listener_class, "onError", "(ILjava/lang/String;)V");
        if (on_error_method) {
            env->CallVoidMethod(listener, on_error_method,
                                static_cast<jint>(result), msg);
        }
        env->DeleteLocalRef(listener_class);
        env->DeleteLocalRef(listener);
        if (env->ExceptionCheck()) break;
    }
    env->DeleteLocalRef(msg);
}

// ---- 回调：talite_on_data ----
void on_data(const talite_data* data) {
    if (!data || data->type != talite_data_type_camera_calibration) return;
    const talite_camera_calibration* c = &data->value.camera_calibration;

    ScopedEnv sc(g_vm);
    JNIEnv* env = sc.env();
    if (!env) return;

    std::vector<jobject> listeners;
    {
        std::scoped_lock<std::mutex> lock(states_mutex);
        listeners = collect_listeners(env, [](State* s) { return s->data_listener; });
    }

    // Use globally cached class ref (native threads can't FindClass app classes).
    jclass calib_class = g_calib_class;
    jmethodID ctor = g_calib_ctor;
    if (!calib_class || !ctor) {
        LOGE("on_data: cached CameraCalibration class not initialized");
        return;
    }
    jfloatArray r_l = to_jfloat_array(env, c->r_raw_l_row);
    jfloatArray r_r = to_jfloat_array(env, c->r_raw_r_row);
    jfloatArray k_l = to_jfloat_array(env, c->k_l);
    jfloatArray k_r = to_jfloat_array(env, c->k_r);
    jfloatArray d_l = to_jfloat_array(env, c->d_l);
    jfloatArray d_r = to_jfloat_array(env, c->d_r);
    jintArray img = to_jint_array(env, c->image_size);
    jfloatArray yaw = to_jfloat_array(env, c->yaw_deg);
    jfloatArray pitch = to_jfloat_array(env, c->pitch_deg);

    jobject calib_obj = env->NewObject(calib_class, ctor,
            r_l, r_r, k_l, k_r, d_l, d_r, img, yaw, pitch);

    for (jobject listener : listeners) {
        if (!listener) continue;
        jclass listener_class = env->GetObjectClass(listener);
        jmethodID on_calib = env->GetMethodID(
                listener_class, "onCameraCalibration",
                "(Lcom/horus/sdkdemo/NativeStreamer$CameraCalibration;)V");
        if (on_calib) {
            env->CallVoidMethod(listener, on_calib, calib_obj);
        }
        env->DeleteLocalRef(listener_class);
        env->DeleteLocalRef(listener);
        if (env->ExceptionCheck()) break;
    }

    env->DeleteLocalRef(calib_obj);
    env->DeleteLocalRef(r_l);
    env->DeleteLocalRef(r_r);
    env->DeleteLocalRef(k_l);
    env->DeleteLocalRef(k_r);
    env->DeleteLocalRef(d_l);
    env->DeleteLocalRef(d_r);
    env->DeleteLocalRef(img);
    env->DeleteLocalRef(yaw);
    env->DeleteLocalRef(pitch);
}

// 替换某个 listener 字段，旧引用释放、新引用置入（调用方持 listener_mutex）。
void replace_listener(JNIEnv* env, jobject& slot, jobject replacement) {
    jobject next = replacement ? env->NewGlobalRef(replacement) : nullptr;
    if (slot) env->DeleteGlobalRef(slot);
    slot = next;
}
} // namespace

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("JNI_OnLoad reserved=%llu", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(reserved)));
    g_vm = vm;
    return JNI_VERSION_1_4;
}

JNIEXPORT void JNI_OnUnload(JavaVM* vm, void* reserved) {
    LOGI("JNI_OnUnload reserved=%llu", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(reserved)));
}

// ============================================================================
// talite_create_context
// ============================================================================
extern "C" JNIEXPORT jlong JNICALL
Java_com_horus_sdkdemo_NativeStreamer_nativeCreate(JNIEnv* env, jclass) {
    auto* value = new State();
    value->context = talite_create_context();
    if (!value->context) {
        delete value;
        return 0;
    }
    {
        std::scoped_lock<std::mutex> lock(states_mutex);
        // 第一个 context 负责注册全局回调（SDK 当前为进程级发现/分发）。
        if (states.empty()) {
            talite_set_device_list_callback(value->context, on_device_list);
            talite_set_h265_stream_callback(value->context, on_h265_stream);
            talite_set_error_callback(value->context, on_error);
            talite_set_data_callback(value->context, on_data);
        }
        // Cache CameraCalibration class ref on app thread for native callback use.
        if (!g_calib_class) {
            jclass local = env->FindClass("com/horus/sdkdemo/NativeStreamer$CameraCalibration");
            if (local) {
                g_calib_class = reinterpret_cast<jclass>(env->NewGlobalRef(local));
                g_calib_ctor = env->GetMethodID(g_calib_class, "<init>",
                        "([F[F[F[F[F[F[I[F[F)V");
                env->DeleteLocalRef(local);
            }
        }
        states.push_back(value);
    }
    return reinterpret_cast<jlong>(value);
}

// ============================================================================
// talite_destroy_context
// ============================================================================
extern "C" JNIEXPORT void JNICALL
Java_com_horus_sdkdemo_NativeStreamer_nativeDestroy(JNIEnv* env, jclass, jlong handle) {
    auto* value = state(handle);
    if (!value) return;
    {
        std::scoped_lock<std::mutex> lock(states_mutex);
        const bool was_dispatcher = !states.empty() && states.front() == value;
        states.erase(std::remove(states.begin(), states.end(), value), states.end());
        if (was_dispatcher && !states.empty()) {
            talite_set_device_list_callback(states.front()->context, on_device_list);
            talite_set_h265_stream_callback(states.front()->context, on_h265_stream);
            talite_set_error_callback(states.front()->context, on_error);
            talite_set_data_callback(states.front()->context, on_data);
        }
    }
    {
        std::scoped_lock<std::mutex> lock(value->listener_mutex);
        if (value->discovery_listener) { env->DeleteGlobalRef(value->discovery_listener); value->discovery_listener = nullptr; }
        if (value->h265_listener) { env->DeleteGlobalRef(value->h265_listener); value->h265_listener = nullptr; }
        if (value->error_listener) { env->DeleteGlobalRef(value->error_listener); value->error_listener = nullptr; }
        if (value->data_listener) { env->DeleteGlobalRef(value->data_listener); value->data_listener = nullptr; }
    }
    if (value->context) {
        talite_set_device_list_callback(value->context, nullptr);
        talite_set_h265_stream_callback(value->context, nullptr);
        talite_set_error_callback(value->context, nullptr);
        talite_set_data_callback(value->context, nullptr);
        talite_stop(value->context);
        talite_destroy_context(value->context);
    }
    delete value;
}

// ============================================================================
// talite_discover_devices
// ============================================================================
extern "C" JNIEXPORT jint JNICALL
Java_com_horus_sdkdemo_NativeStreamer_nativeDiscover(JNIEnv*, jclass, jlong handle) {
    auto* value = state(handle);
    return value && value->context ? static_cast<jint>(talite_discover_devices()) : 1;
}

// ============================================================================
// talite_connect
// ============================================================================
extern "C" JNIEXPORT jint JNICALL
Java_com_horus_sdkdemo_NativeStreamer_nativeConnect(JNIEnv* env, jclass, jlong handle, jstring ip) {
    auto* value = state(handle);
    if (!value || !value->context || !ip) return 1;
    const char* chars = env->GetStringUTFChars(ip, nullptr);
    const jint result = static_cast<jint>(talite_connect(value->context, chars));
    env->ReleaseStringUTFChars(ip, chars);
    return result;
}

// ============================================================================
// talite_disconnect
// ============================================================================
extern "C" JNIEXPORT jint JNICALL
Java_com_horus_sdkdemo_NativeStreamer_nativeDisconnect(JNIEnv*, jclass, jlong handle) {
    auto* value = state(handle);
    return value && value->context ? static_cast<jint>(talite_disconnect(value->context)) : 1;
}

// ============================================================================
// talite_stop
// ============================================================================
extern "C" JNIEXPORT void JNICALL
Java_com_horus_sdkdemo_NativeStreamer_nativeStop(JNIEnv*, jclass, jlong handle) {
    auto* value = state(handle);
    if (value && value->context) talite_stop(value->context);
}

// ============================================================================
// talite_send_data
// ============================================================================
extern "C" JNIEXPORT jint JNICALL
Java_com_horus_sdkdemo_NativeStreamer_nativeSendData(JNIEnv* env, jclass, jlong handle, jbyteArray data, jint size) {
    auto* value = state(handle);
    if (!value || !value->context || !data || size <= 0) return 1;
    jbyte* bytes = env->GetByteArrayElements(data, nullptr);
    if (!bytes) return 1;
    const jint result = static_cast<jint>(talite_send_data(
            value->context, reinterpret_cast<const uint8_t*>(bytes), static_cast<size_t>(size)));
    env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
    return result;
}

// ============================================================================
// talite_get_last_error_string
// ============================================================================
extern "C" JNIEXPORT jstring JNICALL
Java_com_horus_sdkdemo_NativeStreamer_nativeGetLastErrorString(JNIEnv* env, jclass, jlong handle) {
    auto* value = state(handle);
    if (!value || !value->context) return env->NewStringUTF("");
    const char* msg = talite_get_last_error_string(value->context);
    return env->NewStringUTF(msg ? msg : "");
}

// ============================================================================
// talite_set_h265_stream_callback
// ============================================================================
extern "C" JNIEXPORT void JNICALL
Java_com_horus_sdkdemo_NativeStreamer_nativeSetH265StreamListener(
        JNIEnv* env, jclass, jlong handle, jobject listener) {
    auto* value = state(handle);
    if (!value) return;
    std::scoped_lock<std::mutex> lock(value->listener_mutex);
    replace_listener(env, value->h265_listener, listener);
}

// ============================================================================
// talite_set_error_callback
// ============================================================================
extern "C" JNIEXPORT void JNICALL
Java_com_horus_sdkdemo_NativeStreamer_nativeSetErrorListener(
        JNIEnv* env, jclass, jlong handle, jobject listener) {
    auto* value = state(handle);
    if (!value) return;
    std::scoped_lock<std::mutex> lock(value->listener_mutex);
    replace_listener(env, value->error_listener, listener);
}

// ============================================================================
// talite_set_data_callback
// ============================================================================
extern "C" JNIEXPORT void JNICALL
Java_com_horus_sdkdemo_NativeStreamer_nativeSetDataListener(
        JNIEnv* env, jclass, jlong handle, jobject listener) {
    auto* value = state(handle);
    if (!value) return;
    std::scoped_lock<std::mutex> lock(value->listener_mutex);
    replace_listener(env, value->data_listener, listener);
}

// ============================================================================
// talite_set_device_list_callback
// ============================================================================
extern "C" JNIEXPORT void JNICALL
Java_com_horus_sdkdemo_NativeStreamer_nativeSetDiscoveryListener(
        JNIEnv* env, jclass, jlong handle, jobject listener) {
    auto* value = state(handle);
    if (!value) return;
    std::scoped_lock<std::mutex> lock(value->listener_mutex);
    replace_listener(env, value->discovery_listener, listener);
}
