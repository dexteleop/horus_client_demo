package com.horus.sdkdemo;

import java.util.Arrays;

/**
 * talite_xrstream.h 的 Java 包装层。
 * <p>
 * 通过 JNI 桥接库（libhorus_jni.so）调用预编译的 libtalite_xrstream.so，
 * 每个 public 方法对应一个 C API，按飞书文档「4. 调用顺序」使用。
 */
public final class NativeStreamer implements AutoCloseable {
    private long handle;

    public NativeStreamer() {
        handle = nativeCreate();
    }

    public boolean isValid() {
        return handle != 0;
    }

    /** talite_create_context 在构造函数中调用；这里返回句柄用于诊断。 */
    public long handle() {
        return handle;
    }

    /** talite_discover_devices */
    public int discover() {
        return nativeDiscover(handle);
    }

    /** talite_connect */
    public int connect(String ip) {
        return nativeConnect(handle, ip);
    }

    /** talite_disconnect */
    public int disconnect() {
        return nativeDisconnect(handle);
    }

    /** talite_stop（兼容接口，行为等同于 disconnect） */
    public void stop() {
        if (handle != 0) nativeStop(handle);
    }

    /** talite_send_data，发送原始字节。 */
    public int sendData(byte[] data) {
        if (data == null) return 1;
        return nativeSendData(handle, data, data.length);
    }

    /** talite_get_last_error_string */
    public String getLastErrorString() {
        return nativeGetLastErrorString(handle);
    }

    /** talite_set_h265_stream_callback */
    public void setH265StreamListener(H265StreamListener listener) {
        nativeSetH265StreamListener(handle, listener);
    }

    /** talite_set_error_callback */
    public void setErrorListener(ErrorListener listener) {
        nativeSetErrorListener(handle, listener);
    }

    /** talite_set_data_callback */
    public void setDataListener(DataListener listener) {
        nativeSetDataListener(handle, listener);
    }

    /** talite_set_device_list_callback */
    public void setDiscoveryListener(DiscoveryListener listener) {
        nativeSetDiscoveryListener(handle, listener);
    }

    /** talite_destroy_context 在 close 时调用。 */
    @Override
    public void close() {
        long value = handle;
        handle = 0;
        if (value != 0) nativeDestroy(value);
    }

    /** 对应 talite_on_h265_stream，data 仅在回调期间有效（已由 JNI 拷贝）。 */
    public interface H265StreamListener {
        void onH265Stream(byte[] data, long timestampUs, boolean keyFrame);
    }

    /** 对应 talite_on_error。 */
    public interface ErrorListener {
        void onError(int result, String message);
    }

    /** 对应 talite_on_data，当前只回调 camera_calibration。 */
    public interface DataListener {
        void onCameraCalibration(CameraCalibration calibration);
    }

    /** talite_camera_calibration 的 Java 投影。 */
    public static final class CameraCalibration {
        public final float[] rRawLRow;
        public final float[] rRawRRow;
        public final float[] kL;
        public final float[] kR;
        public final float[] dL;
        public final float[] dR;
        public final int[] imageSize;
        public final float[] yawDeg;
        public final float[] pitchDeg;

        CameraCalibration(float[] rRawLRow, float[] rRawRRow, float[] kL, float[] kR,
                          float[] dL, float[] dR, int[] imageSize, float[] yawDeg, float[] pitchDeg) {
            this.rRawLRow = rRawLRow;
            this.rRawRRow = rRawRRow;
            this.kL = kL;
            this.kR = kR;
            this.dL = dL;
            this.dR = dR;
            this.imageSize = imageSize;
            this.yawDeg = yawDeg;
            this.pitchDeg = pitchDeg;
        }

        @Override public String toString() {
            return "CameraCalibration{imageSize=" + Arrays.toString(imageSize)
                    + ", kL=" + Arrays.toString(kL) + ", kR=" + Arrays.toString(kR)
                    + ", yaw=" + Arrays.toString(yawDeg) + ", pitch=" + Arrays.toString(pitchDeg) + '}';
        }
    }

    /** 对应 talite_on_device_list，逐设备回调。 */
    public interface DiscoveryListener {
        void onDevice(String id, String name, String ip);
    }

    private static native long nativeCreate();
    private static native void nativeDestroy(long handle);
    private static native int nativeDiscover(long handle);
    private static native int nativeConnect(long handle, String ip);
    private static native int nativeDisconnect(long handle);
    private static native void nativeStop(long handle);
    private static native int nativeSendData(long handle, byte[] data, int size);
    private static native String nativeGetLastErrorString(long handle);
    private static native void nativeSetH265StreamListener(long handle, H265StreamListener listener);
    private static native void nativeSetErrorListener(long handle, ErrorListener listener);
    private static native void nativeSetDataListener(long handle, DataListener listener);
    private static native void nativeSetDiscoveryListener(long handle, DiscoveryListener listener);

    static {
        System.loadLibrary("horus_jni");
    }
}
