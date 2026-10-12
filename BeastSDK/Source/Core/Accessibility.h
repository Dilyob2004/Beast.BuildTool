#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <optional>
#include <deque>
#include <functional>

namespace Beast {
    enum class AccessibleRole { Text, Button, TextField, ScrollView };
    enum class AccessibleAction { Focus, Invoke, SetValue, SetSelection, ScrollForward, ScrollBackward, ScrollTo };
    struct AccessibleNode {
        int id = 0;
        AccessibleRole role = AccessibleRole::Text;
        std::u16string label, value, error;
        float x = 0, y = 0, width = 0, height = 0; // clipped physical client coordinates
        bool enabled = true, focused = false, password = false, readOnly = false, multiline = false;
        int anchor = 0, caret = 0;
        bool horizontal = false;
        float scrollOffset = 0, scrollExtent = 0, viewport = 0;
        bool operator==(const AccessibleNode&) const = default;
    };
    struct AccessibilityAction {
        int id = 0;
        AccessibleAction action = AccessibleAction::Focus;
        std::u16string value;
        int anchor = 0, caret = 0;
        double scrollPercent = 0;
    };
    // Native providers read immutable copies. Widget actions run only on the UI thread.
    class Accessibility {
        mutable std::mutex mutex_;
        std::vector<AccessibleNode> nodes_;
        std::deque<AccessibilityAction> actions_;
        bool attached_ = true;
    protected:
        virtual void Changed(const std::vector<AccessibleNode>&) {}
    public:
        virtual ~Accessibility() = default;
        virtual bool Enabled() const { return true; }
        void Publish(std::vector<AccessibleNode> nodes) {
            { std::lock_guard lock(mutex_); if (!attached_ || nodes_ == nodes) return; nodes_ = nodes; }
            Changed(nodes);
        }
        std::vector<AccessibleNode> Snapshot() const { std::lock_guard lock(mutex_); return nodes_; }
        bool Attached() const { std::lock_guard lock(mutex_); return attached_; }
        void Detach() { std::lock_guard lock(mutex_); attached_ = false; nodes_.clear(); actions_.clear(); }
        bool Enqueue(AccessibilityAction action) {
            std::lock_guard lock(mutex_);
            if (!attached_ || actions_.size() >= 32 || action.value.size() > 4096) return false;
            for (const auto& node : nodes_) if (node.id == action.id && node.enabled) { actions_.push_back(std::move(action)); return true; }
            return false;
        }
        virtual std::optional<AccessibilityAction> PollAction() {
            std::lock_guard lock(mutex_); if (actions_.empty()) return {};
            auto action = std::move(actions_.front()); actions_.pop_front(); return action;
        }
    };
}
