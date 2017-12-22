/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package android.perfetto.producer;

import android.app.Service;
import android.os.IBinder;
import android.content.Intent;

public class ProducerService extends Service {

   @Override
   public void onCreate() {
        System.loadLibrary("perfettoctsproducer_jni");
        new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    setupProducer();
                } catch (Exception ex) {
                    ex.printStackTrace();
                }
            }
        }).start();
   }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {

    }

    private static native void setupProducer();
}