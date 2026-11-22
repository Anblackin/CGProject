#include "planet_sandbox/project_history.h"
#include <sstream>

namespace planet_sandbox {

std::string project_document(const Project& project) {
    std::ostringstream output;
    write_project(output, project);
    return output.str();
}

Project project_from_document(const std::string& text) {
    std::istringstream input(text);
    return read_project(input);
}

ProjectHistory::ProjectHistory(const Project& project, std::size_t max_entries,
                               std::size_t max_bytes)
    : max_entries_(max_entries), max_bytes_(max_bytes),
      current_(project_document(project)), saved_(current_) {}

bool ProjectHistory::record(const Project& next, const std::string& label,
                            const std::string& body_id, const std::string& group) {
    auto text = project_document(next);
    if (text == current_) return false;
    while (entries_.size() > cursor_) {
        bytes_ -= entries_.back().bytes();
        entries_.pop_back();
    }
    bool removed_group = false;
    if (!group.empty() && group == group_ && cursor_ > 0) {
        auto& entry = entries_.back();
        bytes_ -= entry.bytes();
        entry.after = text;
        bytes_ += entry.bytes();
        if (entry.before == text) {
            bytes_ -= entry.bytes();
            entries_.pop_back();
            --cursor_;
            removed_group = true;
        }
    } else {
        entries_.push_back({current_, text, label, body_id});
        bytes_ += entries_.back().bytes();
        ++cursor_;
    }
    current_ = std::move(text);
    group_ = removed_group ? std::string{} : group;
    trim();
    return true;
}

void ProjectHistory::trim() {
    while (!entries_.empty() && (entries_.size() > max_entries_ || bytes_ > max_bytes_)) {
        bytes_ -= entries_.front().bytes();
        entries_.pop_front();
        if (cursor_ > 0) --cursor_;
    }
    if (entries_.empty()) group_.clear();
}

void ProjectHistory::finish() { group_.clear(); }

std::optional<ProjectEdit> ProjectHistory::undo() {
    finish();
    if (!can_undo()) return std::nullopt;
    const auto& entry = entries_[cursor_ - 1];
    ProjectEdit result{project_from_document(entry.before), entry.label, entry.body_id};
    --cursor_;
    current_ = entry.before;
    return result;
}

std::optional<ProjectEdit> ProjectHistory::redo() {
    finish();
    if (!can_redo()) return std::nullopt;
    const auto& entry = entries_[cursor_];
    ProjectEdit result{project_from_document(entry.after), entry.label, entry.body_id};
    ++cursor_;
    current_ = entry.after;
    return result;
}

std::string ProjectHistory::undo_label() const {
    return can_undo() ? entries_[cursor_ - 1].label : std::string{};
}

std::string ProjectHistory::redo_label() const {
    return can_redo() ? entries_[cursor_].label : std::string{};
}

void ProjectHistory::mark_saved() { finish(); saved_ = current_; saved_known_ = true; }

void ProjectHistory::reset_saved(const Project& project) {
    finish();
    current_ = saved_ = project_document(project);
    saved_known_ = true;
    entries_.clear();
    cursor_ = bytes_ = 0;
}

} // namespace planet_sandbox
