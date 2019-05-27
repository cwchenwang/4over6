package com.example.my_4over6;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.VpnService;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.support.annotation.Nullable;
import android.support.v4.app.ActivityCompat;
import android.support.v4.content.ContextCompat;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;
import android.widget.Toast;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Timer;
import java.util.TimerTask;

public class MainActivity extends AppCompatActivity {

    private EditText address;
    private EditText port;
    private TextView flow_view;
    private Button start_link;
    private Button break_link;
    private File _ext_mem_dir;
    final String ip_pipe = "ip_pipe";
    final String ip_pipe_f2b = "ip_pipe_f2b";
    final String flow_pipe = "flow_pipe";
    private boolean backend_running;
    private boolean ip_flag;

    private Timer timer_ip;
    private Timer timer_flow;
    private TimerTask task_ip;
    private TimerTask task_flow;
    private static Handler handler = new Handler(Looper.getMainLooper());

    String[] permissions = new String[]{
            Manifest.permission.INTERNET,
            Manifest.permission.READ_EXTERNAL_STORAGE,
            Manifest.permission.WRITE_EXTERNAL_STORAGE,
            Manifest.permission.BIND_VPN_SERVICE
    };
    final String log_tag = "frontend";
    int sockfd;
    String ip_addr;
    String route;
    String[] dns;

    // Used to load the 'native-lib' library on application startup.
    static {
        System.loadLibrary("native-lib");
    }

    private boolean checkPermissions() {
        int result;
        List<String> listPermissionsNeeded = new ArrayList<>();

        for (String p : permissions) {
            result = ContextCompat.checkSelfPermission(this, p);
            if (result != PackageManager.PERMISSION_GRANTED) {
                listPermissionsNeeded.add(p);
            }
        }
        if (!listPermissionsNeeded.isEmpty()) {
            ActivityCompat.requestPermissions(this, listPermissionsNeeded.toArray(new String[listPermissionsNeeded.size()]), 100);
            return false;
        }
        return true;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        checkPermissions();

        initArgus();
        initTimerTask();
        initView();

    }

    protected void initArgus()
    {
        _ext_mem_dir = Environment.getExternalStorageDirectory();
        backend_running = false;
        ip_flag = false;
        dns = new String[3];
    }

    protected void initView()
    {
        address = findViewById(R.id.address);
        port = findViewById(R.id.port);
        start_link = findViewById(R.id.start_link);
        break_link = findViewById(R.id.break_link);
        flow_view = findViewById(R.id.flow_view);
        flow_view.setText("Test");

        start_link.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                // Toast.makeText(MainActivity.this, Environment.getExternalStorageDirectory().toString(), Toast.LENGTH_SHORT).show();
                if (!backend_running) {
                    new Thread(new Runnable() {
                        @Override
                        public void run() {
                            if (address.getText().toString().length() == 0) {
                                runOnUiThread(new Runnable() {
                                    @Override
                                    public void run() {
                                        Toast.makeText(MainActivity.this, "fill addr", Toast.LENGTH_SHORT).show();
                                    }
                                });
                                return;
                            }
                            if (port.getText().toString().length() == 0) {
                                runOnUiThread(new Runnable() {
                                    @Override
                                    public void run() {
                                        Toast.makeText(MainActivity.this, "fill port", Toast.LENGTH_SHORT).show();
                                    }
                                });
                                return;
                            }
                            backend_running = true;
                            clean_pipe();
                            putTrigger();
                            backend(address.getText().toString(),
                                    port.getText().toString(),
                                    _ext_mem_dir.toString() + "/" + ip_pipe,
                                    _ext_mem_dir.toString() + "/" + flow_pipe,
                                    _ext_mem_dir .toString()+ "/" + ip_pipe_f2b);
                        }
                    }).start();
                } else {
                    Toast.makeText(MainActivity.this, "后台运行中", Toast.LENGTH_SHORT).show();
                }
            }
        });
        break_link.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if(backend_running) {
                    backend_running = false;
                    unlink_backend();
                }
            }
        });
    }

    protected void initTimerTask()
    {
        timer_ip = new Timer();
        timer_flow = new Timer();
        task_ip = new TimerTask() {
            @Override
            public void run() {
                if (!ip_flag) {
                    String ip_info = readPipe(ip_pipe);
                    // Log.d(log_tag, ip_info);
                    if (ip_info.length() > 0) {
                        ip_flag = true;
                        String[] ip_infos = ip_info.split(" ");
                        // Log.d(log_tag, ip_info + "");
                        int cursor = 0;
                        sockfd = Integer.parseInt(ip_infos[cursor++]);
                        ip_addr = ip_infos[cursor++];
                        route = ip_infos[cursor++];
                        dns[0] = ip_infos[cursor++];
                        dns[1] = ip_infos[cursor++];
                        dns[2] = ip_infos[cursor];
                        Log.d(log_tag, "ip_addr: " + ip_addr);
                        Log.d(log_tag, "route: " + route);
                        Log.d(log_tag, "dns[0]: " + dns[0]);
                        Log.d(log_tag, "dns[1]: " + dns[1]);
                        Log.d(log_tag, "dns[2]" + dns[2]);

                        // open VPNService
                        startVpnService();
                    }
                } else {
                    // start read flow pipe
                    //String ip_info = readPipe(ip_pipe);
                    //Log.d(log_tag, "ip_flag set. " + ip_info);
                }
            }
        };
        task_flow = new TimerTask() {
            @Override
            public void run() {
            if (!backend_running) cancel();
            final String ans = readPipe(flow_pipe);
            if (ans != null) {
                processMsgFromFlow(ans);
            }
            }
        };
    }

    protected void processMsgFromFlow(final String msg) {
        Runnable run = new Runnable() {
            @Override
            public void run() {
                String[] infos = msg.split(" ");
                if (infos.length < 5) return;
                String newMsg = "Recv speed: " + getFlow(infos[0]) + "/s\nTot Recv: " +
                        getFlow(infos[1]) + "\nSend speed: " + getFlow(infos[2]) + "/s\nTot Sent: " +
                        getFlow(infos[3]) + "\nlink time: " +  getTime(infos[4]);
                Log.d("Flow", newMsg);
                flow_view.setText(newMsg);
            }
        };
        handler.post(run);
    }

    protected String getFlow(String s) {
        if (s.isEmpty()) return "0B";
        Double d = Double.parseDouble(s);
        int cnt = 0;
        while (d > 1024) {
            d = d / 1024;
            cnt++;
        }
        String format = String.format("%.2f", d);
        if (cnt == 0) return format + "B";
        else if (cnt == 1) return format + "KB";
        else if (cnt == 2) return format + "MB";
        else return format + "GB";
    }

    protected String getTime(String s) {
        try {
            Integer wholeTime = Integer.parseInt(s);
            Integer hour, min, sec;
            sec = wholeTime % 60;
            hour = wholeTime / 3600;
            min = (wholeTime / 60) % 60;
            return hour.toString() + ":" + min.toString() + ":" + sec.toString();
        } catch (Exception e) {
            Log.d("frontend", e.toString());
            return "0:0:0";
        }
    }

    protected void putTrigger()
    {
        timer_ip.schedule(task_ip, 0, 1000);
        timer_flow.schedule(task_flow, 0, 1000);
    }

    protected String readPipe(String pipe_name)
    {
        int readLen = 0;
        byte[] res = new byte[4096];
        try {
            File file = new File(_ext_mem_dir, pipe_name);
            FileInputStream fileInputStream = new FileInputStream(file);
            BufferedInputStream in = new BufferedInputStream(fileInputStream);
            readLen = in.read(res);
        } catch (Exception e) {
            //Log.e(log_tag, "readPipe() failed." + e.getMessage());
        }
        if (readLen > 0) {
            return new String(res);
        } else {
            return "";
        }
    }

    protected void clean_pipe()
    {
        boolean ret;
        File f = new File(_ext_mem_dir + "/" + ip_pipe);
        if (f.exists()) {
            ret = f.delete();
            Log.d(log_tag, "delete " + ip_pipe + " " + ret);
        }
        f = new File(_ext_mem_dir + "/" + flow_pipe);
        if (f.exists()) {
            ret = f.delete();
            Log.d(log_tag, "delete " + flow_pipe + " " + ret);
        }
        f = new File(_ext_mem_dir + "/" + ip_pipe_f2b);
        if (f.exists()) {
            ret = f.delete();
            Log.d(log_tag, "delete " + ip_pipe_f2b + " " + ret);
        }
    }

    protected void startVpnService()
    {
        Intent intent = VpnService.prepare(MainActivity.this);
        if (intent != null) {
            startActivityForResult(intent, 0);
        } else {
            onActivityResult(0, RESULT_OK, null);
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        //super.onActivityResult(requestCode, resultCode, data);
        if (resultCode == RESULT_OK) {
            Intent intent = new Intent(this, MyVPNService.class);
            intent.putExtra("socket", sockfd);

            intent.putExtra("ip_addr", ip_addr);
            intent.putExtra("route", route);
            intent.putExtra("dns0", dns[0]);
            intent.putExtra("dns1", dns[1]);
            intent.putExtra("dns2", dns[2]);

            intent.putExtra("ip_pipe", _ext_mem_dir + "/" + ip_pipe_f2b);

            Log.d(log_tag, "start VpnService");
            startService(intent);
        }

    }

    /**
     * A native method that is implemented by the 'native-lib' native library,
     * which is packaged with this application.
     */
    public native String stringFromJNI();
    public native void backend(String addr, String port, String ip_pipe, String flow_pipe, String ip_f2b);
    public native void unlink_backend();
}
