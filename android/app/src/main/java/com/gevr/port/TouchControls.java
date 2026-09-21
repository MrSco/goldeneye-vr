package com.gevr.port;

import android.content.Context;
import android.view.MotionEvent;

public class TouchControls {
    private Context context;
    private boolean leftStickActive = false;
    private boolean rightStickActive = false;
    private float leftStickCenterX, leftStickCenterY;
    private float rightStickCenterX, rightStickCenterY;
    private int leftStickPointerId = -1;
    private int rightStickPointerId = -1;
    
    // Touch zones (normalized coordinates 0-1)
    private static final float LEFT_STICK_X = 0.15f;
    private static final float LEFT_STICK_Y = 0.7f;
    private static final float RIGHT_STICK_X = 0.85f;
    private static final float RIGHT_STICK_Y = 0.7f;
    private static final float STICK_RADIUS = 0.1f;
    
    // Button zones
    private static final float FIRE_BUTTON_X = 0.9f;
    private static final float FIRE_BUTTON_Y = 0.3f;
    private static final float AIM_BUTTON_X = 0.1f;
    private static final float AIM_BUTTON_Y = 0.3f;
    private static final float BUTTON_RADIUS = 0.08f;
    
    public TouchControls(Context context) {
        this.context = context;
    }
    
    public boolean onTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        int pointerIndex = event.getActionIndex();
        int pointerId = event.getPointerId(pointerIndex);
        
        float x = event.getX(pointerIndex) / context.getResources().getDisplayMetrics().widthPixels;
        float y = event.getY(pointerIndex) / context.getResources().getDisplayMetrics().heightPixels;
        
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                handleTouchDown(x, y, pointerId);
                break;
                
            case MotionEvent.ACTION_MOVE:
                handleTouchMove(event);
                break;
                
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                handleTouchUp(pointerId);
                break;
        }
        
        return true;
    }
    
    private void handleTouchDown(float x, float y, int pointerId) {
        if (isInZone(x, y, LEFT_STICK_X, LEFT_STICK_Y, STICK_RADIUS * 1.5f)) {
            leftStickActive = true;
            leftStickPointerId = pointerId;
            leftStickCenterX = x;
            leftStickCenterY = y;
        } else if (isInZone(x, y, RIGHT_STICK_X, RIGHT_STICK_Y, STICK_RADIUS * 1.5f)) {
            rightStickActive = true;
            rightStickPointerId = pointerId;
            rightStickCenterX = x;
            rightStickCenterY = y;
        } else if (isInZone(x, y, FIRE_BUTTON_X, FIRE_BUTTON_Y, BUTTON_RADIUS)) {
            nativeButtonDown(0); // Fire button
        } else if (isInZone(x, y, AIM_BUTTON_X, AIM_BUTTON_Y, BUTTON_RADIUS)) {
            nativeButtonDown(1); // Aim button
        }
    }
    
    private void handleTouchMove(MotionEvent event) {
        for (int i = 0; i < event.getPointerCount(); i++) {
            int pointerId = event.getPointerId(i);
            float x = event.getX(i) / context.getResources().getDisplayMetrics().widthPixels;
            float y = event.getY(i) / context.getResources().getDisplayMetrics().heightPixels;
            
            if (pointerId == leftStickPointerId && leftStickActive) {
                float dx = (x - leftStickCenterX) / STICK_RADIUS;
                float dy = (y - leftStickCenterY) / STICK_RADIUS;
                float len = (float) Math.sqrt(dx * dx + dy * dy);
                if (len > 1.0f) {
                    dx /= len;
                    dy /= len;
                }
                nativeStickInput(0, dx, dy);
            } else if (pointerId == rightStickPointerId && rightStickActive) {
                float dx = (x - rightStickCenterX) / STICK_RADIUS;
                float dy = (y - rightStickCenterY) / STICK_RADIUS;
                float len = (float) Math.sqrt(dx * dx + dy * dy);
                if (len > 1.0f) {
                    dx /= len;
                    dy /= len;
                }
                nativeStickInput(1, dx, dy);
            }
        }
    }
    
    private void handleTouchUp(int pointerId) {
        if (pointerId == leftStickPointerId) {
            leftStickActive = false;
            leftStickPointerId = -1;
            nativeStickInput(0, 0, 0);
        } else if (pointerId == rightStickPointerId) {
            rightStickActive = false;
            rightStickPointerId = -1;
            nativeStickInput(1, 0, 0);
        } else {
            nativeButtonUp(0);
            nativeButtonUp(1);
        }
    }
    
    private boolean isInZone(float x, float y, float zoneX, float zoneY, float radius) {
        float dx = x - zoneX;
        float dy = y - zoneY;
        return (dx * dx + dy * dy) <= (radius * radius);
    }
    
    // Native methods
    private native void nativeStickInput(int stick, float x, float y);
    private native void nativeButtonDown(int button);
    private native void nativeButtonUp(int button);
}
