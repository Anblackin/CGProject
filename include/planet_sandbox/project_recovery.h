#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace planet_sandbox {

void atomic_write_document(const std::filesystem::path& path, const std::string& text);
std::filesystem::path document_identity(const std::filesystem::path& path);
std::filesystem::path recovery_path(const std::filesystem::path& project);

struct RecoveryDraft {
    std::string document;
    std::string base_document;
    bool source_changed = false;
};

std::optional<RecoveryDraft> read_recovery(const std::filesystem::path& project);

// A single writer owns at most one active and one replaceable pending checkpoint.
class RecoveryWriter {
public:
    RecoveryWriter();
    ~RecoveryWriter();
    RecoveryWriter(const RecoveryWriter&) = delete;
    RecoveryWriter& operator=(const RecoveryWriter&) = delete;
    void submit(const std::filesystem::path& project, std::string base, std::string document);
    void flush();
    void clear(const std::filesystem::path& project);
    std::string error() const;
    bool busy() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace planet_sandbox
