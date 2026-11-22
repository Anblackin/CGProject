#include "planet_sandbox/project_recovery.h"
#include "planet_sandbox/project_history.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace planet_sandbox {
namespace {

std::string read_text(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > 64 * 1024 * 1024) {
        throw std::runtime_error("Document is too large to recover");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read document");
    std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad()) throw std::runtime_error("Could not finish reading document");
    return text;
}

} // namespace

std::filesystem::path document_identity(const std::filesystem::path& path) {
    return std::filesystem::weakly_canonical(std::filesystem::absolute(path));
}

std::filesystem::path recovery_path(const std::filesystem::path& project) {
    auto path = document_identity(project);
    path += ".recovery.json";
    return path;
}

void atomic_write_document(const std::filesystem::path& path, const std::string& text) {
    static std::atomic<unsigned long long> sequence{0};
    auto temporary = path;
    temporary += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                 "-" + std::to_string(++sequence);
    try {
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("Cannot create temporary document");
            output.write(text.data(), static_cast<std::streamsize>(text.size()));
            output.flush();
            if (!output) throw std::runtime_error("Could not finish writing document");
            output.close();
            if (!output) throw std::runtime_error("Could not close document");
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                    "Could not replace document");
        }
#else
        std::filesystem::rename(temporary, path);
#endif
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

std::optional<RecoveryDraft> read_recovery(const std::filesystem::path& project) {
    const auto identity = document_identity(project);
    const auto path = recovery_path(identity);
    if (!std::filesystem::exists(path)) return std::nullopt;
    const auto json = nlohmann::json::parse(read_text(path));
    if (json.at("version") != 1 || json.at("source") != identity.generic_u8string()) {
        throw std::runtime_error("Recovery draft belongs to another document or version");
    }
    RecoveryDraft draft;
    draft.document = project_document(project_from_document(json.at("document").get<std::string>()));
    draft.base_document = project_document(project_from_document(json.at("base").get<std::string>()));
    const bool exists = std::filesystem::exists(identity);
    draft.source_changed = exists != json.at("source_existed").get<bool>();
    if (exists) {
        try {
            draft.source_changed = draft.source_changed ||
                project_document(project_from_document(read_text(identity))) != draft.base_document;
        } catch (const std::exception&) {
            draft.source_changed = true;
        }
    }
    return draft;
}

struct RecoveryWriter::Impl {
    struct Request { std::filesystem::path path; std::string base, document; bool source_existed; };
    std::mutex mutex;
    std::condition_variable changed;
    std::optional<Request> pending;
    bool active = false, stopping = false;
    std::string failure;
    std::thread thread;
    Impl() : thread([this] { run(); }) {}
    void run() {
        for (;;) {
            Request request;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock, [&] { return stopping || pending.has_value(); });
                if (stopping) return;
                request = std::move(*pending);
                pending.reset();
                active = true;
            }
            std::string error;
            try {
                nlohmann::json json{{"version", 1}, {"source", request.path.generic_u8string()},
                    {"source_existed", request.source_existed}, {"base", request.base},
                    {"document", request.document}};
                atomic_write_document(recovery_path(request.path), json.dump());
            } catch (const std::exception& exception) { error = exception.what(); }
            catch (...) { error = "Unknown recovery error"; }
            {
                std::lock_guard<std::mutex> lock(mutex);
                failure = std::move(error);
                active = false;
            }
            changed.notify_all();
        }
    }
};

RecoveryWriter::RecoveryWriter() : impl_(std::make_unique<Impl>()) {}
RecoveryWriter::~RecoveryWriter() {
    flush();
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->stopping = true;
    }
    impl_->changed.notify_all();
    impl_->thread.join();
}

void RecoveryWriter::submit(const std::filesystem::path& project, std::string base, std::string document) {
    const auto path = document_identity(project);
    const bool exists = std::filesystem::exists(path);
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->pending = Impl::Request{path, std::move(base), std::move(document), exists};
    }
    impl_->changed.notify_all();
}

void RecoveryWriter::flush() {
    std::unique_lock<std::mutex> lock(impl_->mutex);
    impl_->changed.wait(lock, [&] { return !impl_->pending && !impl_->active; });
}

void RecoveryWriter::clear(const std::filesystem::path& project) {
    flush();
    std::filesystem::remove(recovery_path(project));
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->failure.clear();
}

std::string RecoveryWriter::error() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->failure;
}

bool RecoveryWriter::busy() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->pending.has_value() || impl_->active;
}

} // namespace planet_sandbox
