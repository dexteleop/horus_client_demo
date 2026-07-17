package com.horus.sdkdemo;

import android.app.Activity;
import android.content.Context;
import android.net.wifi.WifiManager;
import android.os.Bundle;
import android.text.method.ScrollingMovementMethod;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.Spinner;
import android.widget.TextView;

import com.horus.sdkdemo.NativeStreamer;

import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Horus 客户端侧 SDK Demo。
 * <p>
 * 通过 aar 提供的 {@link NativeStreamer} 调用 talite_xrstream.h 全部方法，
 * 调用顺序遵循飞书文档「4. 调用顺序」：
 * create_context -> set_*_callback -> discover_devices -> connect -> send_data -> disconnect -> destroy_context
 */
public final class MainActivity extends Activity {
    private final Map<String, Device> devices = new LinkedHashMap<>();
    private final List<Device> deviceItems = new ArrayList<>();
    private final SimpleDateFormat clock = new SimpleDateFormat("HH:mm:ss.SSS", Locale.US);

    private NativeStreamer streamer;
    private EditText ipInput;
    private TextView logView;
    private Button discover;
    private Button connect;
    private Button disconnect;
    private Button send;
    private Spinner deviceSpinner;
    private ArrayAdapter<Device> deviceAdapter;
    private WifiManager.MulticastLock multicastLock;
    private boolean running;

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN, WindowManager.LayoutParams.FLAG_FULLSCREEN);
        getWindow().setNavigationBarColor(0xFF000000);
        setContentView(R.layout.activity_main);
        bindViews();
        acquireMulticastLock();

        // 1. talite_create_context（在 NativeStreamer 构造里调用）
        streamer = new NativeStreamer();
        log("create_context -> valid=" + streamer.isValid());

        // 2. talite_set_h265_stream_callback
        streamer.setH265StreamListener((data, ts, key) ->
                runOnUiThread(() -> log("h265: size=" + data.length + " ts=" + ts + " key=" + key)));
        // 3. talite_set_error_callback
        streamer.setErrorListener((result, message) ->
                runOnUiThread(() -> log("error: code=" + result + " msg=" + message)));
        // 4. talite_set_device_list_callback
        streamer.setDiscoveryListener((id, name, ip) -> runOnUiThread(() -> addDevice(id, name, ip)));
        // 5. talite_set_data_callback（相机内外参）
        streamer.setDataListener(calibration -> runOnUiThread(() -> log("calibration: " + calibration)));

        // 6. talite_discover_devices
        discoverDevices();
    }

    private void bindViews() {
        ipInput = findViewById(R.id.ip_input);
        logView = findViewById(R.id.log_view);
        connect = findViewById(R.id.connect_button);
        disconnect = findViewById(R.id.disconnect_button);
        send = findViewById(R.id.send_button);
        deviceSpinner = findViewById(R.id.device_spinner);
        discover = findViewById(R.id.discover_button);
        logView.setMovementMethod(new ScrollingMovementMethod());
        deviceAdapter = new ArrayAdapter<Device>(this, android.R.layout.simple_spinner_item, deviceItems) {
            @Override
            public View getView(int position, View convertView, android.view.ViewGroup parent) {
                TextView view = (TextView) super.getView(position, convertView, parent);
                view.setTextColor(0xFFFFFFFF);
                return view;
            }
        };
        deviceAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        deviceSpinner.setAdapter(deviceAdapter);
        deviceSpinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                if (position >= 0 && position < deviceItems.size()) {
                    ipInput.setText(deviceItems.get(position).ip);
                }
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
            }
        });
        discover.setOnClickListener(v -> discoverDevices());
        connect.setOnClickListener(v -> toggleStream());
        disconnect.setOnClickListener(v -> disconnect());
        send.setOnClickListener(v -> sendDemoData());
    }

    private void acquireMulticastLock() {
        WifiManager wifiManager = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        if (wifiManager == null) return;
        multicastLock = wifiManager.createMulticastLock("horus-discovery");
        multicastLock.setReferenceCounted(false);
        multicastLock.acquire();
    }

    private void discoverDevices() {
        if (streamer == null || !streamer.isValid()) {
            log(R.string.discovery_unavailable);
            return;
        }
        devices.clear();
        deviceItems.clear();
        deviceAdapter.notifyDataSetChanged();
        log(R.string.discovering);
        // 6. talite_discover_devices
        int result = streamer.discover();
        if (result != 0) log(getString(R.string.discovery_failed, result));
    }

    private void addDevice(String id, String name, String ip) {
        if (ip == null || ip.isEmpty()) return;
        Device device = new Device(id, name, ip);
        devices.put(ip, device);
        deviceItems.clear();
        deviceItems.addAll(devices.values());
        deviceAdapter.notifyDataSetChanged();
        int position = deviceItems.indexOf(device);
        if (position >= 0) deviceSpinner.setSelection(position);
        ipInput.setText(ip);
        log(getString(R.string.device_found, device.displayName(), ip));
    }

    private void toggleStream() {
        if (running) {
            running = false;
            // talite_stop（兼容接口，行为等同于 disconnect）
            streamer.stop();
            connect.setText(R.string.connect);
            log(R.string.stopped);
            return;
        }
        String ip = ipInput.getText().toString().trim();
        if (ip.isEmpty()) {
            log(R.string.select_device);
            return;
        }
        // 7. talite_connect
        int result = streamer.connect(ip);
        if (result == 0) {
            running = true;
            connect.setText(R.string.stop);
            log(getString(R.string.streaming, ip));
        } else {
            log(getString(R.string.start_failed, result));
            // talite_get_last_error_string
            String last = streamer.getLastErrorString();
            if (last != null && !last.isEmpty()) log("last_error: " + last);
        }
    }

    private void disconnect() {
        // talite_disconnect：主动断开但保留 context 与回调
        int result = streamer.disconnect();
        running = false;
        connect.setText(R.string.connect);
        log("disconnect -> " + result + " (" + getString(R.string.disconnected) + ")");
    }

    private void sendDemoData() {
        // 8. talite_send_data：发送一段示例原始字节，演示透传通道
        byte[] payload = "ping".getBytes(java.nio.charset.StandardCharsets.UTF_8);
        int result = streamer.sendData(payload);
        if (result == 0) {
            log(getString(R.string.data_sent, payload.length));
        } else {
            log(getString(R.string.send_failed, result));
            String last = streamer.getLastErrorString();
            if (last != null && !last.isEmpty()) log("last_error: " + last);
        }
    }

    private void log(int resId) {
        log(getString(resId));
    }

    private void log(String message) {
        String line = clock.format(new Date()) + "  " + message + "\n";
        logView.append(line);
    }

    @Override
    protected void onDestroy() {
        running = false;
        if (streamer != null) {
            // 9. talite_destroy_context（在 close 里调用）
            streamer.setDiscoveryListener(null);
            streamer.setH265StreamListener(null);
            streamer.setErrorListener(null);
            streamer.setDataListener(null);
            streamer.close();
        }
        if (multicastLock != null && multicastLock.isHeld()) multicastLock.release();
        super.onDestroy();
    }

    private static final class Device {
        private final String id;
        private final String name;
        private final String ip;

        Device(String id, String name, String ip) {
            this.id = id == null ? "" : id;
            this.name = name == null ? "" : name;
            this.ip = ip;
        }

        String displayName() {
            if (!name.isEmpty()) return name;
            if (!id.isEmpty()) return id;
            return ip;
        }

        @Override
        public String toString() {
            return displayName() + " (" + ip + ")";
        }

        @Override
        public boolean equals(Object other) {
            return other instanceof Device && ip.equals(((Device) other).ip);
        }

        @Override
        public int hashCode() {
            return ip.hashCode();
        }
    }
}
