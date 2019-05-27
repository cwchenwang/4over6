package com.example.my_4over6;

import android.content.Intent;
import android.net.VpnService;
import android.os.ParcelFileDescriptor;
import android.util.Log;

import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;

public class MyVPNService extends VpnService {

    private ParcelFileDescriptor mInterface;
    Builder builder = new Builder();
    final String log_tag = "VpnService";

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Log.d(log_tag, "onstartCommand() start.\n");

        int sockfd = intent.getIntExtra("socket", -1);
        protect(sockfd);

        mInterface = builder.setSession("MyVpnService")
                .addAddress(intent.getStringExtra("ip_addr"), 24)
                .addRoute(intent.getStringExtra("route"), 0)
                .addDnsServer(intent.getStringExtra("dns0"))
                .addDnsServer(intent.getStringExtra("dns1"))
                .addDnsServer(intent.getStringExtra("dns2"))
                .setMtu(1500)
                .establish();

        String ip_pipe = intent.getStringExtra("ip_pipe");
        int tun0_fd = mInterface.getFd();
        Log.d(log_tag, "tun0_fd; " + tun0_fd);
        String tun0_fd_string = tun0_fd + "";
        // write it to ip_pipe
        try {
            File file = new File(ip_pipe);
            FileOutputStream fileOutputStream = new FileOutputStream(file);
            BufferedOutputStream out = new BufferedOutputStream(fileOutputStream);
            out.write(tun0_fd_string.getBytes(), 0, tun0_fd_string.length());
            out.flush();
            out.close();
        } catch (Exception e) {
            Log.e(log_tag, e.getMessage());
        }
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        try {
            mInterface.close();
        } catch (Exception e) {
            Log.e(log_tag, e.getMessage());
        }
        super.onDestroy();
    }
}
