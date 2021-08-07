/*
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
#include <stdio.h>
#include <stdlib.h>
#include <android/log.h>
#include "dii_player.h"
#include <jni.h>

#include "webrtc/api/java/jni/androidvideocapturer_jni.h"
#include "webrtc/api/java/jni/androidmediaencoder_jni.h"
#include "webrtc/api/java/jni/classreferenceholder.h"
#include "webrtc/api/java/jni/jni_helpers.h"
#include "webrtc/api/java/jni/native_handle_impl.h"
#include "webrtc/base/scoped_ptr.h"
#include "webrtc/media/base/videoframe.h"
#include "webrtc/modules/utility/include/helpers_android.h"
#include "webrtc/modules/utility/include/jvm_android.h"

static bool av_static_initialized = false;
#define TAG		"Dii-Media-JNI"
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace dii_media_jni {
// Wrapper dispatching dii_rtc::VideoSinkInterface to a Java VideoRenderer
// instance.
class JavaVideoRendererWrapper
    : public dii_rtc::VideoSinkInterface<cricket::VideoFrame> {
 public:
  JavaVideoRendererWrapper(JNIEnv* jni, jobject j_callbacks)
      : j_callbacks_(jni, j_callbacks),
        j_render_frame_id_(GetMethodID(
            jni, GetObjectClass(jni, j_callbacks), "renderFrame",
            "(Lorg/dii/webrtc/VideoRenderer$I420Frame;)V")),
        j_frame_class_(jni,
                       FindClass(jni, "org/dii/webrtc/VideoRenderer$I420Frame")),
        j_i420_frame_ctor_id_(GetMethodID(
            jni, *j_frame_class_, "<init>", "(III[I[Ljava/nio/ByteBuffer;J)V")),
        j_texture_frame_ctor_id_(GetMethodID(
            jni, *j_frame_class_, "<init>",
            "(IIII[FJ)V")),
        j_byte_buffer_class_(jni, FindClass(jni, "java/nio/ByteBuffer")) {
    CHECK_EXCEPTION(jni);
  }

  virtual ~JavaVideoRendererWrapper() {}

  void OnFrame(const cricket::VideoFrame& video_frame) override {
    ScopedLocalRefFrame local_ref_frame(jni());
    jobject j_frame =
        (video_frame.video_frame_buffer()->native_handle() != nullptr)
            ? CricketToJavaTextureFrame(&video_frame)
            : CricketToJavaI420Frame(&video_frame);
    // |j_callbacks_| is responsible for releasing |j_frame| with
    // VideoRenderer.renderFrameDone().
    jni()->CallVoidMethod(*j_callbacks_, j_render_frame_id_, j_frame);
    CHECK_EXCEPTION(jni());
  }

 private:
  // Make a shallow copy of |frame| to be used with Java. The callee has
  // ownership of the frame, and the frame should be released with
  // VideoRenderer.releaseNativeFrame().
  static jlong javaShallowCopy(const cricket::VideoFrame* frame) {
    return jlongFromPointer(frame->Copy());
  }

  // Return a VideoRenderer.I420Frame referring to the data in |frame|.
  jobject CricketToJavaI420Frame(const cricket::VideoFrame* frame) {
    jintArray strides = jni()->NewIntArray(3);
    jint* strides_array = jni()->GetIntArrayElements(strides, NULL);
    strides_array[0] = frame->video_frame_buffer()->StrideY();
    strides_array[1] = frame->video_frame_buffer()->StrideU();
    strides_array[2] = frame->video_frame_buffer()->StrideV();
    jni()->ReleaseIntArrayElements(strides, strides_array, 0);
    jobjectArray planes = jni()->NewObjectArray(3, *j_byte_buffer_class_, NULL);
    jobject y_buffer = jni()->NewDirectByteBuffer(
        const_cast<uint8_t*>(frame->video_frame_buffer()->DataY()),
        frame->video_frame_buffer()->StrideY() *
            frame->video_frame_buffer()->height());
    size_t chroma_height = (frame->height() + 1) / 2;
    jobject u_buffer = jni()->NewDirectByteBuffer(
        const_cast<uint8_t*>(frame->video_frame_buffer()->DataU()),
        frame->video_frame_buffer()->StrideU() * chroma_height);
    jobject v_buffer = jni()->NewDirectByteBuffer(
        const_cast<uint8_t*>(frame->video_frame_buffer()->DataV()),
        frame->video_frame_buffer()->StrideV() * chroma_height);

    jni()->SetObjectArrayElement(planes, 0, y_buffer);
    jni()->SetObjectArrayElement(planes, 1, u_buffer);
    jni()->SetObjectArrayElement(planes, 2, v_buffer);
    return jni()->NewObject(
        *j_frame_class_, j_i420_frame_ctor_id_,
        frame->width(), frame->height(),
        static_cast<int>(frame->rotation()),
        strides, planes, javaShallowCopy(frame));
  }

  // Return a VideoRenderer.I420Frame referring texture object in |frame|.
  jobject CricketToJavaTextureFrame(const cricket::VideoFrame* frame) {
    NativeHandleImpl* handle = reinterpret_cast<NativeHandleImpl*>(
        frame->video_frame_buffer()->native_handle());
    jfloatArray sampling_matrix = handle->sampling_matrix.ToJava(jni());

    return jni()->NewObject(
        *j_frame_class_, j_texture_frame_ctor_id_,
        frame->width(), frame->height(),
        static_cast<int>(frame->rotation()),
        handle->oes_texture_id, sampling_matrix, javaShallowCopy(frame));
  }

  JNIEnv* jni() {
    return AttachCurrentThreadIfNeeded();
  }

  ScopedGlobalRef<jobject> j_callbacks_;
  jmethodID j_render_frame_id_;
  ScopedGlobalRef<jclass> j_frame_class_;
  jmethodID j_i420_frame_ctor_id_;
  jmethodID j_texture_frame_ctor_id_;
  ScopedGlobalRef<jclass> j_byte_buffer_class_;
};

// Macro for native functions that can be found by way of jni-auto discovery.
// Note extern "C" is needed for "discovery" of native methods to work.
#define REG_JNI_FUNC(rettype, name)                                             \
  extern "C" rettype JNIEXPORT JNICALL Java_org_dii_core_##name


//=================================================================
namespace {
jlong GetJApp(JNIEnv* jni, jobject j_app)
{
  jclass j_app_class = jni->GetObjectClass(j_app);
  jfieldID native_id =
      jni->GetFieldID(j_app_class, "fNativeAppId", "J");
  return jni->GetLongField(j_app, native_id);
}

}

//=================================================================
//* DiiMediaCore.class
//=================================================================
REG_JNI_FUNC(void, DiiMediaCore_nativeInitCtx)(JNIEnv* jni, jclass, jobject context, jobject egl_context)
{
	if(!av_static_initialized)
	{
        // talk/ assumes pretty widely that the current Thread is ThreadManager'd, but
        // ThreadManager only WrapCurrentThread()s the thread where it is first
        // created.  Since the semantics around when auto-wrapping happens in
        // webrtc/base/ are convoluted, we simply wrap here to avoid having to think
        // about ramifications of auto-wrapping there.
        dii_rtc::ThreadManager::Instance()->WrapCurrentThread();

		 ALOGD("JVM::Initialize nativeInitCtx");
		//* Set Video Context
		dii_media_jni::AndroidVideoCapturerJni::SetAndroidObjects(jni, context);
		//* Set Audio Context
		dii_media_kit::JVM::Initialize(dii_media_jni::GetJVM(), context);
		av_static_initialized = true;
        
        jclass j_eglbase14_context_class = dii_media_jni::FindClass(jni, "org/dii/webrtc/EglBase14$Context");
        if (jni->IsInstanceOf(egl_context, j_eglbase14_context_class)) {
            dii_media_jni::MediaCodecVideoEncoderFactory* java_encoder = new dii_media_jni::MediaCodecVideoEncoderFactory();
            dii_rtc::scoped_ptr<cricket::WebRtcVideoEncoderFactory> external_encoder(java_encoder);
            java_encoder->SetEGLContext(jni, egl_context);

//            static dii_media_kit::DiiRtmpCore apollo_rtmp_core;
//            apollo_rtmp_core.SetExternalVideoEncoderFactory(external_encoder.release());
        }
	}
}


REG_JNI_FUNC(jstring, DiiMediaCore_nativeVersion)(JNIEnv* jni, jclass j_app)
{
    std::string ver = dii_media_kit::DiiMediaKit::Version();
    jstring jversion = JavaStringFromStdString(jni, ver);
    return jversion;
}

REG_JNI_FUNC(jint, DiiMediaCore_nativeSetTraceLog)(JNIEnv* jni, jclass j_app, jstring jpath, jint level)
{
    std::string path = JavaToStdString(jni, jpath);
    return dii_media_kit::DiiMediaKit::SetTraceLog(path.data(), dii_media_kit::LogSeverity(level));
}


REG_JNI_FUNC(void, DiiMediaCore_nativeSetDebugLog)(JNIEnv* jni, jclass l, jint level) {
    dii_media_kit::DiiMediaKit::SetDebugLog(dii_media_kit::LogSeverity(level));
}


//
////=================================================================
////* DiiRtmpHosterKit.class
////=================================================================
//REG_JNI_FUNC(jlong, DiiRtmpHosterKit_nativeCreate)(JNIEnv* jni, jclass, jobject j_obj)
//{
//	JRTMPHosterImpl* jApp = new JRTMPHosterImpl(j_obj);
//	return dii_media_jni::jlongFromPointer(jApp);
//}
//
//REG_JNI_FUNC(void, DiiRtmpHosterKit_nativeSetAudioEnable)(JNIEnv* jni, jobject j_app, jboolean j_enable)
//{
//	JRTMPHosterImpl* jApp = (JRTMPHosterImpl*)GetJApp(jni, j_app);
//	jApp->Hoster()->SetAudioEnable(j_enable);
//}
//
//REG_JNI_FUNC(void, DiiRtmpHosterKit_nativeSetVideoMode)(JNIEnv* jni, jobject j_app, jint j_mode) {
//    JRTMPHosterImpl* jApp = (JRTMPHosterImpl*)GetJApp(jni, j_app);
//    jApp->Hoster()->SetVideoMode(dii_media_kit::RTMPVideoMode(j_mode));
//}
//
//REG_JNI_FUNC(void, DiiRtmpHosterKit_nativeSetVideoEnable)(JNIEnv* jni, jobject j_app, jboolean j_enable)
//{
//	JRTMPHosterImpl* jApp = (JRTMPHosterImpl*)GetJApp(jni, j_app);
//	jApp->Hoster()->SetVideoEnable(j_enable);
//}
//
//REG_JNI_FUNC(void, DiiRtmpHosterKit_nativeSetVideoCapturer)(JNIEnv* jni, jobject j_app, jobject j_video_capturer, jlong j_renderer_pointer)
//{
//	JRTMPHosterImpl* jApp = (JRTMPHosterImpl*)GetJApp(jni, j_app);
//
//	if(j_video_capturer != NULL) {
//		jobject j_egl_context = NULL;
//		// Create a cricket::VideoCapturer from |j_video_capturer|.
//		dii_rtc::scoped_refptr<dii_media_kit::AndroidVideoCapturerDelegate> delegate =
//		  new dii_rtc::RefCountedObject<AndroidVideoCapturerJni>(
//			  jni, j_video_capturer, j_egl_context);
//		std::unique_ptr<cricket::VideoCapturer> capturer(
//		  new dii_media_kit::AndroidVideoCapturer(delegate));
//
//		jApp->Hoster()->SetVideoRender(reinterpret_cast<dii_rtc::VideoSinkInterface<cricket::VideoFrame>*>(j_renderer_pointer));
//		jApp->Hoster()->SetVideoCapturer(capturer.release());
//	}
//	else
//	{
//		jApp->Hoster()->SetVideoCapturer(NULL);
//	}
//}
//
//REG_JNI_FUNC(void, DiiRtmpHosterKit_nativeStartRtmpStream)(JNIEnv* jni, jobject j_app, jstring j_rtmp_url)
//{
//	JRTMPHosterImpl* jApp = (JRTMPHosterImpl*)GetJApp(jni, j_app);
//
//	std::string rtmp_url = JavaToStdString(jni, j_rtmp_url);
//	jApp->Hoster()->StartRtmpStream(rtmp_url.c_str());
//}
//
//REG_JNI_FUNC(void, DiiRtmpHosterKit_nativeStopRtmpStream)(JNIEnv* jni, jobject j_app)
//{
//	JRTMPHosterImpl* jApp = (JRTMPHosterImpl*)GetJApp(jni, j_app);
//
//	jApp->Hoster()->StopRtmpStream();
//}
//
//REG_JNI_FUNC(void, DiiRtmpHosterKit_nativeDestroy)(JNIEnv* jni, jobject j_app)
//{
//	JRTMPHosterImpl* jApp = (JRTMPHosterImpl*)GetJApp(jni, j_app);
//	jApp->Close();
//	delete jApp;
//}

//=============================================================================
// DiiPlayer
//=============================================================================

REG_JNI_FUNC(jlong , DiiPlayer_nativeInit)(JNIEnv* jni, jobject j_app,  jlong jrender) {
    dii_media_kit::DiiPlayer* ins = new dii_media_kit::DiiPlayer((void*)jrender);
    return (jlong)ins;
}

REG_JNI_FUNC(void , DiiPlayer_nativeDestroy)(JNIEnv* jni, jobject j_app,  jlong jplayer, jlong jthis) {
    dii_media_kit::DiiPlayer* player = (dii_media_kit::DiiPlayer*)jplayer;
    if(player) {
        delete player;
    }
    jobject ref = (jobject)jthis;
    if(ref) {
        jni->DeleteGlobalRef(ref);
    }
}

REG_JNI_FUNC(jlong, DiiPlayer_nativeSetPlayerCallback)(JNIEnv* jni, jobject obj, jlong ins) {
    dii_media_kit::DiiPlayer* player = (dii_media_kit::DiiPlayer*)ins;
    if(player) {
        dii_media_kit::DiiPlayerCallback cb;
        jobject thiz = reinterpret_cast<jclass> (jni->NewGlobalRef(obj));;
        // state callback
        cb.state_callback = [=](dii_media_kit::DiiPlayerState state, int32_t code, const char* msg, void* custom_data) -> void {
            dii_media_kit::AttachThreadScoped ats(dii_media_jni::GetJVM());
            JNIEnv* env = ats.env();
            jclass clazz = env->GetObjectClass(thiz);
            jmethodID method = env->GetMethodID(clazz, "OnPlayerState", "(II)V");
            env->CallVoidMethod(thiz, method, state, code);
        };
        // sync ts callback
        cb.sync_ts_callback = [=](uint64_t ts) {
            dii_media_kit::AttachThreadScoped ats(dii_media_jni::GetJVM());
            JNIEnv* env = ats.env();
            jclass clazz = env->GetObjectClass(thiz);

            jmethodID method = env->GetMethodID(clazz, "OnStreamSyncTs", "(J)V");
            env->CallVoidMethod(thiz, method, ts);
        };

        cb.resolution_callback = [=](int32_t width, int32_t height) {
            dii_media_kit::AttachThreadScoped ats(dii_media_jni::GetJVM());
            JNIEnv* env = ats.env();
            jclass clazz = env->GetObjectClass(thiz);

            jmethodID method = env->GetMethodID(clazz, "OnResolutionChange", "(II)V");
            env->CallVoidMethod(thiz, method, width, height);
        };

        cb.statistics_callback = [=](dii_media_kit::DiiPlayerStatistics& statistics) {
            char json[1024] = {0};
            sprintf(json, "{\"video_width\" : \"%d\", "
                        "\"video_height\": \"%d\", "
                        "\"decoder_fps\" : \"%d\", "
                        "\"render_fps\" : \"%d\", "
                        "\"audio_sample_rate\": \"%d\", "
                        "\"cache_len\": \"%d\", "
                        "\"audio_bps\": \"%d\", "
                        "\"video_bps\": \"%d\", "
                        "\"fluency\":\"%d\"}",
                    statistics.video_width_,
                    statistics.video_height_,
                    statistics.video_decode_framerate,
                    statistics.video_render_framerate,
                    statistics.audio_samplerate_,
                    statistics.cache_len_,
                    statistics.audio_bps_,
                    statistics.video_bps_,
                    statistics.fluency);
            dii_media_kit::AttachThreadScoped ats(dii_media_jni::GetJVM());
            JNIEnv* env = ats.env();
            jclass clazz = env->GetObjectClass(thiz);

            jmethodID method = env->GetMethodID(clazz, "OnStatistics", "(Ljava/lang/String;)V");
            jstring str = env->NewStringUTF(json);
            env->CallVoidMethod(thiz, method, str);
        };

        player->SetPlayerCallback(&cb);
        return (jlong)thiz;
    }
}

REG_JNI_FUNC(int32_t, DiiPlayer_nativeStart)(JNIEnv* jni, jobject j_app, jlong ins, jstring jurl, jlong pos, jboolean pause)
{
    dii_media_kit::DiiPlayer* player = (dii_media_kit::DiiPlayer*)ins;
    if(player) {
        std::string url = JavaToStdString(jni, jurl);
        player->Start(url.c_str(), pos, pause);
    } else {
    }
}

REG_JNI_FUNC(int32_t, DiiPlayer_nativePause)(JNIEnv* jni, jobject j_app, jlong ins)
{
    dii_media_kit::DiiPlayer* player = (dii_media_kit::DiiPlayer*)ins;
    if(player) {
        player->Pause();
    } else {

    }
}

REG_JNI_FUNC(int32_t, DiiPlayer_nativeResume)(JNIEnv* jni, jobject j_app, jlong ins)
{
    dii_media_kit::DiiPlayer* player = (dii_media_kit::DiiPlayer*)ins;
    if(player) {
        player->Resume();
    } else {
    }
}

REG_JNI_FUNC(int32_t, DiiPlayer_nativeStop)(JNIEnv* jni, jobject j_app, jlong ins)
{
    dii_media_kit::DiiPlayer* player = (dii_media_kit::DiiPlayer*)ins;
    if(player) {
        player->Stop();
    } else {
    }
}

REG_JNI_FUNC(int32_t, DiiPlayer_nativeSeek)(JNIEnv* jni, jobject j_app, jlong ins, jlong pos)
{
    dii_media_kit::DiiPlayer* player = (dii_media_kit::DiiPlayer*)ins;
    if(player) {
        player->Seek(pos);
    } else {
    }
}

REG_JNI_FUNC(int32_t, DiiPlayer_nativeSetMute)(JNIEnv* jni, jobject j_app, jlong ins, jboolean mute)
{
    dii_media_kit::DiiPlayer* player = (dii_media_kit::DiiPlayer*)ins;
    if(player) {
        player->SetMute(mute);
    } else {
    }
    return 0;
}

REG_JNI_FUNC(int64_t , DiiPlayer_nativePosition)(JNIEnv* jni, jobject j_app, jlong ins)
{
    dii_media_kit::DiiPlayer* player = (dii_media_kit::DiiPlayer*)ins;
    if(player) {
        return player->Position();
    } else {
        return -1;
    }
}

REG_JNI_FUNC(int64_t, DiiPlayer_nativeDuration)(JNIEnv* jni, jobject j_app, jlong ins)
{
    dii_media_kit::DiiPlayer* player = (dii_media_kit::DiiPlayer*)ins;
    if(player) {
        return player->Duration();
    } else {
        return -1;
    }
}

REG_JNI_FUNC(int64_t, DiiPlayer_nativeClearDisplayView)(JNIEnv* jni, jobject j_app, jlong ins, jint width, jint height, jint r, jint g, jint b)
{
    dii_media_kit::DiiPlayer* player = (dii_media_kit::DiiPlayer*)ins;
    if(player) {
        return player->ClearDisplayView(width, height, r, g, b);
    } else {
        return -1;
    }
}











//=============================================================================
//* For VideoRenderer
JOW(void, VideoRenderer_freeWrappedVideoRenderer)(JNIEnv*, jclass, jlong j_p) {
  delete reinterpret_cast<JavaVideoRendererWrapper*>(j_p);
}

JOW(void, VideoRenderer_releaseNativeFrame)(
    JNIEnv* jni, jclass, jlong j_frame_ptr) {
  delete reinterpret_cast<const cricket::VideoFrame*>(j_frame_ptr);
}

JOW(jlong, VideoRenderer_nativeWrapVideoRenderer)(
    JNIEnv* jni, jclass, jobject j_callbacks) {
  std::unique_ptr<JavaVideoRendererWrapper> renderer(
      new JavaVideoRendererWrapper(jni, j_callbacks));
  return (jlong)renderer.release();
}

JOW(void, VideoRenderer_nativeCopyPlane)(
    JNIEnv *jni, jclass, jobject j_src_buffer, jint width, jint height,
    jint src_stride, jobject j_dst_buffer, jint dst_stride) {
  size_t src_size = jni->GetDirectBufferCapacity(j_src_buffer);
  size_t dst_size = jni->GetDirectBufferCapacity(j_dst_buffer);
  RTC_CHECK(src_stride >= width) << "Wrong source stride " << src_stride;
  RTC_CHECK(dst_stride >= width) << "Wrong destination stride " << dst_stride;
  RTC_CHECK(src_size >= src_stride * height)
      << "Insufficient source buffer capacity " << src_size;
  RTC_CHECK(dst_size >= dst_stride * height)
      << "Insufficient destination buffer capacity " << dst_size;
  uint8_t *src =
      reinterpret_cast<uint8_t*>(jni->GetDirectBufferAddress(j_src_buffer));
  uint8_t *dst =
      reinterpret_cast<uint8_t*>(jni->GetDirectBufferAddress(j_dst_buffer));
  if (src_stride == dst_stride) {
    memcpy(dst, src, src_stride * height);
  } else {
    for (int i = 0; i < height; i++) {
      memcpy(dst, src, width);
      src += src_stride;
      dst += dst_stride;
    }
  }
}
}	// namespace dii_media_jni
