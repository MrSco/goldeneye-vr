package com.gevr.port;

import android.content.Context;
import android.opengl.GLSurfaceView;
import android.view.MotionEvent;
import android.view.KeyEvent;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class GameView extends GLSurfaceView {
    private GameRenderer renderer;
    private TouchControls touchControls;
    
    public GameView(Context context) {
        super(context);
        
        // Create OpenGL ES 3.0+ context
        setEGLContextClientVersion(3);
        
        renderer = new GameRenderer();
        setRenderer(renderer);
        
        touchControls = new TouchControls(context);
        
        // Render continuously
        setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
    }
    
    @Override
    public boolean onTouchEvent(MotionEvent event) {
        return touchControls.onTouchEvent(event);
    }
    
    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        nativeKeyDown(keyCode);
        return true;
    }
    
    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        nativeKeyUp(keyCode);
        return true;
    }
    
    private class GameRenderer implements GLSurfaceView.Renderer {
        @Override
        public void onSurfaceCreated(GL10 gl, EGLConfig config) {
            nativeSurfaceCreated();
        }
        
        @Override
        public void onSurfaceChanged(GL10 gl, int width, int height) {
            nativeSurfaceChanged(width, height);
        }
        
        @Override
        public void onDrawFrame(GL10 gl) {
            nativeDrawFrame();
        }
    }
    
    // Native methods
    private native void nativeSurfaceCreated();
    private native void nativeSurfaceChanged(int width, int height);
    private native void nativeDrawFrame();
    private native void nativeKeyDown(int keyCode);
    private native void nativeKeyUp(int keyCode);
}
