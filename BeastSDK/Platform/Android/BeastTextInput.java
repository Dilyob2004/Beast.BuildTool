package com.beast.sdk;

import android.app.Activity;
import android.content.Context;
import android.graphics.Rect;
import android.os.Build;
import android.text.Editable;
import android.text.InputFilter;
import android.text.TextWatcher;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.view.inputmethod.*;
import android.widget.EditText;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;

// An invisible system editor transports IME state. Beast renders the real field.
public final class BeastTextInput {
    private final Activity activity;
    private final InputMethodManager imm;
    private Proxy editor;
    private volatile BeastAccessibility accessibility;
    private volatile long requestedSession;
    private long session, revision, epoch;
    private int maxBytes = 4096, batches, insetAnimations;
    private boolean applying, scheduled, keyboardWasVisible, closing;
    private boolean multiline, password;
    private final ArrayDeque<String> events = new ArrayDeque<>();
    private volatile int availableBottom = -1;
    public static BeastTextInput create(Activity activity) { return new BeastTextInput(activity); }
    public String readClipboard() {
        android.content.ClipboardManager clipboard = (android.content.ClipboardManager)activity.getSystemService(Context.CLIPBOARD_SERVICE);
        android.content.ClipData clip = clipboard.getPrimaryClip();
        if (clip == null || clip.getItemCount() == 0) return null;
        // Do not resolve URIs or invoke external content providers for a text paste.
        CharSequence text = clip.getItemAt(0).getText();
        return text != null && text.length() <= 4096 ? text.toString() : null;
    }
    public boolean writeClipboard(String text) {
        if (text == null || text.length() > 4096) return false;
        android.content.ClipboardManager clipboard = (android.content.ClipboardManager)activity.getSystemService(Context.CLIPBOARD_SERVICE);
        clipboard.setPrimaryClip(android.content.ClipData.newPlainText("", text)); return true;
    }
    private BeastTextInput(Activity activity) {
        this.activity = activity;
        imm = (InputMethodManager)activity.getSystemService(Context.INPUT_METHOD_SERVICE);
        activity.runOnUiThread(() -> {
            accessibility = new BeastAccessibility(activity);
            editor = new Proxy(activity);
            editor.setSingleLine(true);
            editor.setInputType(android.text.InputType.TYPE_CLASS_TEXT | android.text.InputType.TYPE_TEXT_FLAG_CAP_SENTENCES | android.text.InputType.TYPE_TEXT_FLAG_AUTO_CORRECT);
            editor.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
            editor.setAlpha(0); editor.setPadding(0, 0, 0, 0); editor.setBackground(null);
            editor.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
            editor.setSaveEnabled(false); editor.setFocusable(false);
            editor.setFilters(new InputFilter[]{(source, start, end, dest, dstart, dend) -> {
                if (applying) return null;
                String candidate = dest.subSequence(0, dstart).toString() + source.subSequence(start, end) + dest.subSequence(dend, dest.length());
                if (candidate.getBytes(StandardCharsets.UTF_8).length > maxBytes) return dest.subSequence(dstart, dend);
                for (int i = 0; i < candidate.length();) {
                    int cp = candidate.codePointAt(i);
                    if ((cp < 32 && !(multiline && (cp == 10 || cp == 13 || cp == 9))) || (cp >= 127 && cp <= 159) || (cp >= 0xd800 && cp <= 0xdfff) || cp == 0x2028 || cp == 0x2029) return dest.subSequence(dstart, dend);
                    i += Character.charCount(cp);
                }
                return null;
            }});
            editor.addTextChangedListener(new TextWatcher() {
                public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
                public void onTextChanged(CharSequence s, int start, int before, int count) { schedule(); }
                public void afterTextChanged(Editable value) { schedule(); }
            });
            editor.setOnEditorActionListener((view, action, event) -> {
                if (multiline) return false;
                if (event == null || event.getAction() == KeyEvent.ACTION_DOWN) publish(1);
                return true;
            });
            activity.addContentView(editor, new ViewGroup.LayoutParams(1, 1));
            activity.getWindow().clearFlags(WindowManager.LayoutParams.FLAG_ALT_FOCUSABLE_IM);
            activity.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
            activity.getWindow().getDecorView().getViewTreeObserver().addOnGlobalLayoutListener(this::insets);
            if (Build.VERSION.SDK_INT >= 30) editor.setWindowInsetsAnimationCallback(new android.view.WindowInsetsAnimation.Callback(android.view.WindowInsetsAnimation.Callback.DISPATCH_MODE_CONTINUE_ON_SUBTREE) {
                @Override public void onPrepare(android.view.WindowInsetsAnimation animation) { insetAnimations++; }
                @Override public WindowInsets onProgress(WindowInsets value, java.util.List<android.view.WindowInsetsAnimation> running) { insets(value); return value; }
                @Override public void onEnd(android.view.WindowInsetsAnimation animation) { insetAnimations = Math.max(0, insetAnimations - 1); insets(); }
            });
            insets();
        });
    }
    // Bottom coordinate, not a height blindly subtracted from an already resized surface.
    public int getAvailableBottom() { return availableBottom; }
    public void accessibilityUpdate(String packet) { activity.runOnUiThread(() -> { if (accessibility != null) accessibility.update(packet); }); }
    public String accessibilityPoll() { return accessibility == null ? null : accessibility.poll(); }
    public boolean accessibilityEnabled() { return accessibility != null && accessibility.enabled(); }
    private void insets() { insets(activity.getWindow().getDecorView().getRootWindowInsets()); }
    @SuppressWarnings("deprecation") private void insets(WindowInsets insets) {
        View decor = activity.getWindow().getDecorView();
        boolean visible = false; int bottom = decor.getHeight();
        if (insets != null && Build.VERSION.SDK_INT >= 30) {
            visible = insets.isVisible(WindowInsets.Type.ime());
            bottom -= insets.getInsets(WindowInsets.Type.ime()).bottom;
        } else if (insets != null) {
            Rect frame = new Rect(); decor.getWindowVisibleDisplayFrame(frame);
            int[] origin = new int[2]; decor.getLocationOnScreen(origin);
            int occlusion = decor.getRootView().getHeight() - (frame.bottom - origin[1]);
            visible = Math.max(occlusion, insets.getSystemWindowInsetBottom()) > Math.max(insets.getStableInsetBottom(), (int)(80 * decor.getResources().getDisplayMetrics().density));
            bottom = frame.bottom - origin[1];
        }
        availableBottom = visible ? Math.max(0, bottom) : -1;
        if (visible) keyboardWasVisible = true;
        else if (insetAnimations == 0 && keyboardWasVisible && session != 0 && !closing) { publish(2); hide(); }
        if (!visible && insetAnimations == 0) keyboardWasVisible = false;
    }
    public void update(long id, long version, String text, int anchor, int caret, int composingStart, int composingEnd, int limit, int flags) {
        requestedSession = id;
        activity.runOnUiThread(() -> {
            if (requestedSession != id || editor == null || activity.isFinishing()) return;
            if (session == id && version < revision) return;
            session = id; revision = version; maxBytes = limit; epoch++; batches = 0; closing = false;
            synchronized (events) { events.clear(); }
            applying = true;
            multiline = (flags & 1) != 0; password = (flags & 2) != 0;
            int inputType = android.text.InputType.TYPE_CLASS_TEXT | (password ? android.text.InputType.TYPE_TEXT_VARIATION_PASSWORD : android.text.InputType.TYPE_TEXT_FLAG_CAP_SENTENCES | android.text.InputType.TYPE_TEXT_FLAG_AUTO_CORRECT);
            if (multiline) inputType |= android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE;
            editor.setSingleLine(!multiline); editor.setInputType(inputType);
            editor.setImeOptions((multiline ? EditorInfo.IME_ACTION_NONE : EditorInfo.IME_ACTION_DONE) | EditorInfo.IME_FLAG_NO_EXTRACT_UI | (password ? EditorInfo.IME_FLAG_NO_PERSONALIZED_LEARNING : 0));
            editor.setText(text); editor.setSelection(anchor, caret);
            if (composingStart >= 0 && composingEnd > composingStart)
                new BaseInputConnection(editor, true) { @Override public Editable getEditable() { return editor.getText(); } }.setComposingRegion(composingStart, composingEnd);
            applying = false;
            editor.setFocusableInTouchMode(true); editor.requestFocus(); imm.restartInput(editor); imm.showSoftInput(editor, InputMethodManager.SHOW_IMPLICIT);
        });
    }
    public void close(long id) {
        if (requestedSession != id) return;
        requestedSession = 0;
        activity.runOnUiThread(() -> { if (session == id && requestedSession == 0) hide(); });
    }
    private void hide() {
        closing = true; session = 0; epoch++; keyboardWasVisible = false;
        if (editor != null) { imm.hideSoftInputFromWindow(editor.getWindowToken(), 0); editor.clearFocus(); editor.setFocusable(false); }
    }
    public String poll() { synchronized (events) { return events.pollFirst(); } }
    private void schedule() {
        if (applying || scheduled || editor == null) return;
        scheduled = true; editor.post(() -> { scheduled = false; if (batches == 0) publish(0); });
    }
    private void publish(int action) {
        if (applying || editor == null || session == 0 || session != requestedSession) return;
        Editable value = editor.getText();
        int begin = BaseInputConnection.getComposingSpanStart(value), end = BaseInputConnection.getComposingSpanEnd(value);
        if (begin < 0 || end <= begin) begin = end = -1;
        String packet = session + "\n" + revision + "\n" + editor.getSelectionStart() + "\n" + editor.getSelectionEnd() + "\n" + begin + "\n" + end + "\n" + action + "\n" + value;
        synchronized (events) {
            // Keep actions; only consecutive ordinary edits may coalesce.
            if (action == 0 && !events.isEmpty() && events.peekLast().split("\n", 8)[6].equals("0")) events.removeLast();
            if (events.size() == 8) events.removeFirst();
            events.addLast(packet);
        }
    }
    private final class Proxy extends EditText {
        Proxy(Context context) { super(context); }
        @Override protected void onSelectionChanged(int start, int end) { super.onSelectionChanged(start, end); schedule(); }
        @Override public boolean onKeyPreIme(int code, KeyEvent event) {
            if (code == KeyEvent.KEYCODE_BACK && session != 0) {
                if (event.getAction() == KeyEvent.ACTION_UP) { publish(2); hide(); } return true;
            }
            return super.onKeyPreIme(code, event);
        }
        @Override public InputConnection onCreateInputConnection(EditorInfo info) {
            InputConnection actual = super.onCreateInputConnection(info); if (actual == null) return null;
            final long token = epoch;
            return new InputConnectionWrapper(actual, false) {
                private boolean valid() { return token == epoch && session != 0 && session == requestedSession; }
                private boolean changed(boolean result) { schedule(); return result; }
                @Override public boolean commitText(CharSequence text, int cursor) { return valid() && changed(super.commitText(text, cursor)); }
                @Override public boolean commitText(CharSequence text, int cursor, TextAttribute attributes) { return valid() && changed(super.commitText(text, cursor, attributes)); }
                @Override public boolean setComposingText(CharSequence text, int cursor) { return valid() && changed(super.setComposingText(text, cursor)); }
                @Override public boolean setComposingText(CharSequence text, int cursor, TextAttribute attributes) { return valid() && changed(super.setComposingText(text, cursor, attributes)); }
                @Override public boolean setComposingRegion(int start, int end) { return valid() && changed(super.setComposingRegion(start, end)); }
                @Override public boolean setComposingRegion(int start, int end, TextAttribute attributes) { return valid() && changed(super.setComposingRegion(start, end, attributes)); }
                @Override public boolean replaceText(int start, int end, CharSequence text, int cursor, TextAttribute attributes) { return valid() && changed(super.replaceText(start, end, text, cursor, attributes)); }
                @Override public boolean finishComposingText() { return valid() && changed(super.finishComposingText()); }
                @Override public boolean setSelection(int start, int end) { return valid() && changed(super.setSelection(start, end)); }
                @Override public boolean deleteSurroundingText(int before, int after) { return valid() && changed(super.deleteSurroundingText(before, after)); }
                @Override public boolean deleteSurroundingTextInCodePoints(int before, int after) { return valid() && changed(super.deleteSurroundingTextInCodePoints(before, after)); }
                @Override public boolean beginBatchEdit() { if (!valid()) return false; batches++; return super.beginBatchEdit(); }
                @Override public boolean endBatchEdit() { if (!valid()) return false; if (batches > 0) batches--; return changed(super.endBatchEdit()); }
                @Override public boolean sendKeyEvent(KeyEvent event) { return valid() && changed(super.sendKeyEvent(event)); }
                @Override public boolean performEditorAction(int action) { if (!valid()) return false; if (multiline) return changed(super.performEditorAction(action)); publish(1); return true; }
            };
        }
    }
}
