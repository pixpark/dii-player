package org.dii.core;

import android.content.Context;
public class DiiPlayer {
    // 播放器状态
    public final static int STATE_ERROR      = 0;
    public final static int STATE_PLAYING    = 1;          // 正在播放
    public final static int STATE_STOPPED    = 2;          // 停止
    public final static int STATE_PAUSED     = 3;          // 暂停
    public final static int STATE_SEEKING    = 4;          // SEEK
    public final static int STATE_BUFFERING  = 5;          // 缓冲
    public final static int STATE_STUCK      = 6;          // 卡顿（可在此状态切CND）
    public final static int STATE_FINISH     = 7;          // 播完

    private long native_player = 0;
    private long native_this = 0;
    private IDiiPlayerHelper.IDiiPlayerCallback callback;

    public final static int _UserRole_unknown = 0;
    public final static int _UserRole_Student = 1;
    public final static int _UserRole_Teachear = 2;
    public final static int _UserRole_Assistant = 3;

    public DiiPlayer(Context ctx, final long renderPointer) {
        DiiMediaCore.Inst().Init(ctx.getApplicationContext());
        native_player = nativeInit(renderPointer);
    }

    /**
     * 开始播放，网络流或本地视频文件
     * @param url 媒体链接或本地文件路径
     * @return 0 成功，非0 失败
     */
    public synchronized int start(final String url) {
        if (native_player == 0) {
            return 0;
        }
        return nativeStart(native_player, url, 0, false);
    }


    public synchronized int start(final String url, long pos) {
        if (native_player == 0) {
            return 0;
        }
        return nativeStart(native_player, url, pos, false);
    }

    public synchronized int start(final String url, long pos, boolean pause) {
        if (native_player == 0) {
            return 0;
        }
        return nativeStart(native_player, url, pos, pause);
    }

    public int start(final String url, int role, String userId) {
        return start(url, 0, role, userId);
    }

    public int start(final String url, long pos, int role, String userId) {
        return start(url, pos, false, role, userId);
    }

    public synchronized int start(final String url, long pos, boolean pause, int role, String userId) {
        int ret = 0;
        if(native_player != 0){
            int rolevalue = 0;
            if(role == _UserRole_unknown || role == _UserRole_Student || role == _UserRole_Teachear || role == _UserRole_Assistant){
                rolevalue = role;
            }
            nativeSetUserInfo(native_player, rolevalue, userId);
            ret = nativeStart(native_player, url, pos, pause);
        }

        return ret;
    }

    /**
     * 停止播放
     * @return 0 成功，非0 失败
     */
    public synchronized int stop() {
        if (native_player == 0) {
            return 0;
        }
        return nativeStop(native_player);
    }

    /**
     * 暂停
     * @return 0 成功，非0 失败
     */
    public synchronized int pause() {
        if (native_player == 0) {
            return 0;
        }
        return nativePause(native_player);
    }

    /**
     * 恢复
     * @return 0 成功，非0 失败
     */
    public synchronized int resume() {
        if (native_player == 0) {
            return 0;
        }
        return nativeResume(native_player);
    }

    /**
     * 进度跳转
     * @param pos 进度，单位毫秒
     * @return 0 成功，非0 失败
     */
    public synchronized int seek(long pos) {
        if (native_player == 0) {
            return 0;
        }
        return nativeSeek(native_player, pos);
    }

    public synchronized void setMute(boolean mute) {
        if (native_player == 0) {
            return;
        }
        nativeSetMute(native_player, mute);
    }

    /**
     * 当前播放进度
     * @return 播放进度，单位毫秒
     */
    public synchronized long position() {
        if(native_player == 0) {
            return 0;
        }
        return nativePosition(native_player);
    }

    /**
     * 音视频时长
     * @return 时长，毫秒
     */
    public synchronized long duration() {
        if (native_player == 0) {
            return 0;
        }
        return nativeDuration(native_player);
    }

    /**
     * 销毁播放器
     */
    public synchronized void destroy() {
        if (native_player == 0) {
            return;
        }
        new Thread() {
            @Override
            public void run() {
                nativeDestroy(native_player, native_this);
                native_player = 0;
                native_this = 0;
            }
        }.start();
    }

    /**
     * 设置播放器回调
     * @param callback 见 IDiiPlayerCallback
     */
    public synchronized void setPlayerCallbck(IDiiPlayerHelper.IDiiPlayerCallback callback) {
        if (native_player == 0) {
            return;
        }
        this.callback = callback;
        native_this = nativeSetPlayerCallback(native_player);
    }

    // clear display with black color.
    public int clearDisplay() {
        return nativeClearDisplayView(native_player,640, 480, 0, 0, 0);
    }

    public int clearDisplay(int width, int height, int r, int g, int b) {
        return nativeClearDisplayView(native_player, width, height, r, g, b);
    }

    // Callback
    private void OnPlayerState(int state, int code) {
        if(this.callback != null) {
            this.callback.OnPlayerState(state, code);
        }
    }

    private  void OnStreamSyncTs(long ts) {
        if(this.callback != null) {
            this.callback.OnStreamSyncTs(ts);
        }
    }

    private void OnResolutionChange(int width, int height) {
        if(this.callback != null) {
            this.callback.OnResolutionChange(width, height);
        }
    }

    private void OnStatistics(String json) {
        if(this.callback != null) {
            this.callback.OnStatistics(json);
        }
    }

    /**
     *  Native function
     */
    private native long nativeInit(final long jrender);
    private native long nativeDestroy(long jplayer, long jthis);
    private native long nativeSetPlayerCallback(long jplayer);
    private native boolean nativeSetUserInfo(long jplayer, int role, String userId);
    private native int nativeStart(long jplayer, String url, long pos, boolean pause);
    private native int nativePause(long jplayer);
    private native int nativeResume(long jplayer);
    private native int nativeStop(long jplayer);
    private native int nativeSeek(long jplayer, long pos);
    private native int nativeSetMute(long jplayer, boolean mute);
    private native long nativePosition(long jplayer);
    private native long nativeDuration(long jplayer);
    private native int nativeClearDisplayView(long jplayer, int width, int height, int r, int g, int b);
}
