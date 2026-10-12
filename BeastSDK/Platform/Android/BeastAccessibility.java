package com.beast.sdk;

import android.app.Activity;
import android.graphics.Rect;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;
import android.view.MotionEvent;
import android.view.accessibility.*;
import java.util.ArrayDeque;
import java.util.LinkedHashMap;
import org.json.*;

// Virtual descendants of a transparent, non-touch-intercepting view over the native surface.
final class BeastAccessibility extends View {
    private final Activity activity;
    private final AccessibilityManager manager;
    private final ArrayDeque<String> actions = new ArrayDeque<>();
    private LinkedHashMap<Integer, JSONObject> nodes = new LinkedHashMap<>();
    private int focused = -1, hovered = -1, inputFocus = -1;
    BeastAccessibility(Activity activity) {
        super(activity); this.activity = activity;
        manager = (AccessibilityManager)activity.getSystemService(Activity.ACCESSIBILITY_SERVICE);
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_YES);
        setFocusable(false); setWillNotDraw(true); setSaveEnabled(false);
        activity.addContentView(this, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
    }
    void update(String packet) {
        activity.runOnUiThread(() -> {
            if (activity.isFinishing()) return;
            try {
                JSONArray array = new JSONArray(packet); LinkedHashMap<Integer, JSONObject> next = new LinkedHashMap<>();
                int focus = -1;
                for (int i = 0; i < array.length(); i++) { JSONObject n = array.getJSONObject(i); next.put(n.getInt("id"), n); if (n.optBoolean("focused")) focus = n.getInt("id"); }
                if (focused != -1 && !next.containsKey(focused)) { event(focused, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED); focused = -1; }
                LinkedHashMap<Integer, JSONObject> previous = nodes; nodes = next;
                if (manager.isEnabled()) {
                    event(View.NO_ID, AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED);
                    if (focus != inputFocus && focus != -1) event(focus, AccessibilityEvent.TYPE_VIEW_FOCUSED);
                    for (JSONObject n : nodes.values()) {
                        JSONObject old = previous.get(n.optInt("id"));
                        if (old != null && !n.optString("value").equals(old.optString("value"))) event(n.optInt("id"), AccessibilityEvent.TYPE_VIEW_TEXT_CHANGED);
                    }
                }
                inputFocus = focus;
            } catch (JSONException ignored) { /* Invalid snapshots never replace a valid tree. */ }
        });
    }
    String poll() { synchronized (actions) { return actions.pollFirst(); } }
    boolean enabled() { return manager.isEnabled(); }
    private boolean queue(int id, int action, String value, int start, int end) {
        if (!nodes.containsKey(id) || value.length() > 4096) return false;
        synchronized (actions) { if (actions.size() >= 32) return false; actions.addLast(id + "\n" + action + "\n" + start + "\n" + end + "\n" + value); }
        return true;
    }
    private Rect bounds(JSONObject n) { return new Rect(n.optInt("x"), n.optInt("y"), n.optInt("x") + n.optInt("width"), n.optInt("y") + n.optInt("height")); }
    private String className(JSONObject n) {
        switch (n.optInt("role")) { case 1: return "android.widget.Button"; case 2: return "android.widget.EditText"; case 3: return "android.widget.ScrollView"; default: return "android.widget.TextView"; }
    }
    private void event(int id, int type) {
        if (!manager.isEnabled() || getParent() == null) return;
        AccessibilityEvent event = AccessibilityEvent.obtain(type); event.setPackageName(activity.getPackageName());
        event.setSource(this, id); JSONObject n = nodes.get(id);
        if (n != null) { event.setClassName(className(n)); event.setPassword(n.optBoolean("password")); event.getText().add(n.optString("value").isEmpty() ? n.optString("label") : n.optString("value")); }
        if (type == AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED) event.setContentChangeTypes(AccessibilityEvent.CONTENT_CHANGE_TYPE_SUBTREE);
        getParent().requestSendAccessibilityEvent(this, event);
    }
    @Override public boolean onTouchEvent(MotionEvent event) { return false; }
    @Override public boolean dispatchHoverEvent(MotionEvent e) {
        if (!manager.isEnabled() || !manager.isTouchExplorationEnabled()) return false;
        int next = -1;
        if (e.getAction() != MotionEvent.ACTION_HOVER_EXIT) for (JSONObject n : nodes.values()) if (bounds(n).contains((int)e.getX(), (int)e.getY())) next = n.optInt("id");
        if (next != hovered) { if (next != -1) event(next, AccessibilityEvent.TYPE_VIEW_HOVER_ENTER); if (hovered != -1) event(hovered, AccessibilityEvent.TYPE_VIEW_HOVER_EXIT); hovered = next; }
        return next != -1 || e.getAction() == MotionEvent.ACTION_HOVER_EXIT;
    }
    @Override public AccessibilityNodeProvider getAccessibilityNodeProvider() { return provider; }
    private final AccessibilityNodeProvider provider = new AccessibilityNodeProvider() {
        @Override public AccessibilityNodeInfo createAccessibilityNodeInfo(int id) {
            if (id == View.NO_ID) {
                AccessibilityNodeInfo info = AccessibilityNodeInfo.obtain(BeastAccessibility.this);
                onInitializeAccessibilityNodeInfo(info);
                for (int child : nodes.keySet()) info.addChild(BeastAccessibility.this, child);
                return info;
            }
            JSONObject n = nodes.get(id); if (n == null) return null;
            AccessibilityNodeInfo info = AccessibilityNodeInfo.obtain(); info.setSource(BeastAccessibility.this, id); info.setParent(BeastAccessibility.this);
            info.setPackageName(activity.getPackageName()); info.setClassName(className(n));
            info.setEnabled(n.optBoolean("enabled")); info.setVisibleToUser(isShown());
            Rect rect = bounds(n); info.setBoundsInParent(rect); int[] origin = new int[2]; getLocationOnScreen(origin); rect.offset(origin[0], origin[1]); info.setBoundsInScreen(rect);
            int role = n.optInt("role"); boolean password = n.optBoolean("password");
            info.setAccessibilityFocused(id == focused); info.addAction(id == focused ? AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS : AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS);
            info.setFocusable(role == 1 || role == 2); info.setFocused(n.optBoolean("focused"));
            if (role == 1 || role == 2) info.addAction(AccessibilityNodeInfo.ACTION_FOCUS);
            if (role == 2) {
                info.setHintText(n.optString("label")); info.setShowingHintText(n.optString("value").isEmpty()); info.setText(n.optString("value"));
                info.setPassword(password); info.setEditable(!n.optBoolean("readonly")); info.setMultiLine(n.optBoolean("multiline"));
                if (!n.optBoolean("readonly")) info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SET_TEXT);
                if (!password) { info.setTextSelection(n.optInt("anchor"), n.optInt("caret")); info.addAction(AccessibilityNodeInfo.ACTION_SET_SELECTION); }
                if (!n.optString("error").isEmpty()) { info.setContentInvalid(true); info.setError(n.optString("error")); }
            } else { info.setText(n.optString("label")); }
            if (role == 1) { info.setClickable(true); if (n.optBoolean("enabled")) info.addAction(AccessibilityNodeInfo.ACTION_CLICK); }
            if (role == 3) { info.setScrollable(true); info.addAction(AccessibilityNodeInfo.ACTION_SCROLL_FORWARD); info.addAction(AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD); }
            return info;
        }
        @Override public AccessibilityNodeInfo findFocus(int type) { return createAccessibilityNodeInfo(type == AccessibilityNodeInfo.FOCUS_ACCESSIBILITY ? focused : inputFocus); }
        @Override public boolean performAction(int id, int action, Bundle args) {
            JSONObject n = nodes.get(id); if (n == null || !n.optBoolean("enabled")) return false;
            if (action == AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS) {
                if (focused == id) return false; if (focused != -1) event(focused, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED);
                focused = id; event(id, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUSED); return true;
            }
            if (action == AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS) { if (focused != id) return false; focused = -1; event(id, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED); return true; }
            if (action == AccessibilityNodeInfo.ACTION_FOCUS) return queue(id, 0, "", 0, 0);
            if (action == AccessibilityNodeInfo.ACTION_CLICK && n.optInt("role") == 1) { boolean ok = queue(id, 1, "", 0, 0); if (ok) event(id, AccessibilityEvent.TYPE_VIEW_CLICKED); return ok; }
            if (action == AccessibilityNodeInfo.ACTION_SET_TEXT && n.optInt("role") == 2 && !n.optBoolean("readonly") && args != null) {
                CharSequence text = args.getCharSequence(AccessibilityNodeInfo.ACTION_ARGUMENT_SET_TEXT_CHARSEQUENCE); return text != null && queue(id, 2, text.toString(), 0, 0);
            }
            if (action == AccessibilityNodeInfo.ACTION_SET_SELECTION && !n.optBoolean("password") && args != null) return queue(id, 3, "", args.getInt(AccessibilityNodeInfo.ACTION_ARGUMENT_SELECTION_START_INT), args.getInt(AccessibilityNodeInfo.ACTION_ARGUMENT_SELECTION_END_INT));
            if (n.optInt("role") == 3 && action == AccessibilityNodeInfo.ACTION_SCROLL_FORWARD) return queue(id, 4, "", 0, 0);
            if (n.optInt("role") == 3 && action == AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD) return queue(id, 5, "", 0, 0);
            return false;
        }
    };
}
