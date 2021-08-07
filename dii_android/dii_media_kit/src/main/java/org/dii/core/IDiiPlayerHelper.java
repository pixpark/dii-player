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

/**
 * Created by devzhaoyou on 2016/7/28.
 */
public interface IDiiPlayerHelper {
    public interface IDiiPlayerCallback {
        public void OnPlayerState(int state, int code); // 播放器状态回调
        public void OnStreamSyncTs(long ts); // rtmp同步时间戳回调
        public void OnResolutionChange(int height, int width); // 视频分辨率变更回调
        public void OnStatistics(String json); // 播放器信息统计回调
    }
}
