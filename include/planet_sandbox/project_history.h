#pragma once

#include "planet_sandbox/project.h"
#include <deque>
#include <optional>

namespace planet_sandbox {

std::string project_document(const Project& project);
Project project_from_document(const std::string& text);

struct ProjectEdit {
    Project project;
    std::string label;
    std::string body_id;
};

// One chronological history for parameter gestures, resets and sculpt strokes.
class ProjectHistory {
public:
    explicit ProjectHistory(const Project& project, std::size_t max_entries = 48,
                            std::size_t max_bytes = 32 * 1024 * 1024);
    bool record(const Project& next, const std::string& label,
                const std::string& body_id = {}, const std::string& group = {});
    void finish();
    std::optional<ProjectEdit> undo();
    std::optional<ProjectEdit> redo();
    bool can_undo() const { return cursor_ > 0; }
    bool can_redo() const { return cursor_ < entries_.size(); }
    std::size_t undo_count() const { return cursor_; }
    std::size_t memory_bytes() const { return bytes_; }
    std::string undo_label() const;
    std::string redo_label() const;
    bool dirty() const { return !saved_known_ || current_ != saved_; }
    const std::string& current_document() const { return current_; }
    const std::string& saved_document() const { return saved_; }
    void mark_saved();
    void invalidate_saved() { saved_known_ = false; }
    void reset_saved(const Project& project);

private:
    struct Entry {
        std::string before, after, label, body_id;
        std::size_t bytes() const { return before.size() + after.size(); }
    };
    std::deque<Entry> entries_;
    std::size_t cursor_ = 0, bytes_ = 0, max_entries_, max_bytes_;
    std::string current_, saved_, group_;
    bool saved_known_ = true;
    void trim();
};

} // namespace planet_sandbox
