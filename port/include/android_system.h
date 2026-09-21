#ifndef ANDROID_SYSTEM_H
#define ANDROID_SYSTEM_H

#ifdef ANDROID

#include <jni.h>
#include <android/log.h>

// Android-specific system functions
const char* sysGetDataPath(void);
bool sysIsAndroid(void);

// Input functions for Android
void inputGetStickInput(int stick, float* x, float* y);
bool inputGetButtonState(int button);

// JNI function declarations for GEVR (com.gevr.port)
JNIEXPORT void JNICALL Java_com_gevr_port_MainActivity_nativeInit(JNIEnv* env, jobject thiz, jstring dataPath);
JNIEXPORT void JNICALL Java_com_gevr_port_MainActivity_nativeStartGame(JNIEnv* env, jobject thiz);
JNIEXPORT void JNICALL Java_com_gevr_port_MainActivity_nativeDestroy(JNIEnv* env, jobject thiz);
JNIEXPORT void JNICALL Java_com_gevr_port_MainActivity_nativeAudioPause(JNIEnv* env, jobject thiz);
JNIEXPORT void JNICALL Java_com_gevr_port_MainActivity_nativeAudioResume(JNIEnv* env, jobject thiz);
JNIEXPORT void JNICALL Java_com_gevr_port_GameView_nativeSurfaceCreated(JNIEnv* env, jobject thiz);
JNIEXPORT void JNICALL Java_com_gevr_port_GameView_nativeSurfaceChanged(JNIEnv* env, jobject thiz, jint width, jint height);
JNIEXPORT void JNICALL Java_com_gevr_port_GameView_nativeDrawFrame(JNIEnv* env, jobject thiz);
JNIEXPORT void JNICALL Java_com_gevr_port_GameView_nativeKeyDown(JNIEnv* env, jobject thiz, jint keyCode);
JNIEXPORT void JNICALL Java_com_gevr_port_GameView_nativeKeyUp(JNIEnv* env, jobject thiz, jint keyCode);
JNIEXPORT void JNICALL Java_com_gevr_port_TouchControls_nativeStickInput(JNIEnv* env, jobject thiz, jint stick, jfloat x, jfloat y);
JNIEXPORT void JNICALL Java_com_gevr_port_TouchControls_nativeButtonDown(JNIEnv* env, jobject thiz, jint button);
JNIEXPORT void JNICALL Java_com_gevr_port_TouchControls_nativeButtonUp(JNIEnv* env, jobject thiz, jint button);

// Legacy aliases for compatibility
JNIEXPORT void JNICALL Java_com_perfectdark_port_MainActivity_nativeInit(JNIEnv* env, jobject thiz, jstring dataPath);
JNIEXPORT void JNICALL Java_com_perfectdark_port_MainActivity_nativeDestroy(JNIEnv* env, jobject thiz);
JNIEXPORT void JNICALL Java_com_perfectdark_port_GameView_nativeSurfaceCreated(JNIEnv* env, jobject thiz);
JNIEXPORT void JNICALL Java_com_perfectdark_port_GameView_nativeSurfaceChanged(JNIEnv* env, jobject thiz, jint width, jint height);
JNIEXPORT void JNICALL Java_com_perfectdark_port_GameView_nativeDrawFrame(JNIEnv* env, jobject thiz);
JNIEXPORT void JNICALL Java_com_perfectdark_port_GameView_nativeKeyDown(JNIEnv* env, jobject thiz, jint keyCode);
JNIEXPORT void JNICALL Java_com_perfectdark_port_GameView_nativeKeyUp(JNIEnv* env, jobject thiz, jint keyCode);
JNIEXPORT void JNICALL Java_com_perfectdark_port_TouchControls_nativeStickInput(JNIEnv* env, jobject thiz, jint stick, jfloat x, jfloat y);
JNIEXPORT void JNICALL Java_com_perfectdark_port_TouchControls_nativeButtonDown(JNIEnv* env, jobject thiz, jint button);
JNIEXPORT void JNICALL Java_com_perfectdark_port_TouchControls_nativeButtonUp(JNIEnv* env, jobject thiz, jint button);

#endif // ANDROID

#endif // ANDROID_SYSTEM_H