/**
 *  Copyright (c) 2016 The rtmp_live_kit project authors. All Rights Reserved.
 *
 *  Please visit https://https://github.com/PixPark/DiiPlayer for detail.
 *
 * The GNU General Public License is a free, copyleft license for
 * software and other kinds of works.
 *
 * The licenses for most software and other practical works are designed
 * to take away your freedom to share and change the works.  By contrast,
 * the GNU General Public License is intended to guarantee your freedom to
 * share and change all versions of a program--to make sure it remains free
 * software for all its users.  We, the Free Software Foundation, use the
 * GNU General Public License for most of our software; it applies also to
 * any other work released this way by its authors.  You can apply it to
 * your programs, too.
 * See the GNU LICENSE file for more info.
 */
package org.dii.core;

import android.content.Context;

import org.dii.webrtc.EglBase;

/**
 * Created by Eric on 2016/9/15.
 */
public class DiiMediaCore {
    //log  level
    public static final int LOG_NONE        = 0;
    public static final int LOG_ERROR       = 1;
    public static final int LOG_WARNNING    = 2;
    public static final int LOG_INFO        = 3;
    public static final int LOG_VERBOSE     = 4;

    private final EglBase eglBase;
    private boolean has_init = false;

    /**
     * 加载api所需要的动态库
     */
    static {
        System.loadLibrary("dii_media_kit");
    }

    private static class SingletonHolder {
        private static final DiiMediaCore INSTANCE = new DiiMediaCore();
    }

    public static final DiiMediaCore Inst() {
        return SingletonHolder.INSTANCE;
    }

    private DiiMediaCore() {
        eglBase = EglBase.create();
    }


    public void Init(final Context ctx) {
        if(has_init) {
            return;
        }
        has_init = true;
        nativeInitCtx(ctx, eglBase.getEglBaseContext());
    }

    public String Version() {
        return nativeVersion();
    }

    public EglBase Egl() {
        return eglBase;
    }

    public int SetTraceLog(String path, int level) {
        return nativeSetTraceLog(path, level);
    }
    public void SetLogToDebug(int level) {
        nativeSetDebugLog(level);
    }

    /**
     * Jni interface
     */
    private static native void nativeInitCtx(Context ctx, EglBase.Context context);
    private static native void nativeSetDebugLog(int level);
    private static native int nativeSetTraceLog(String path, int level);
    private static native String nativeVersion();
}
