#include "planet_sandbox/studio_render_pipeline.h"

#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace planet_sandbox {

struct StudioRenderWorker::Impl {
    RenderFunction render;
    std::mutex mutex;
    std::condition_variable condition;
    std::optional<StudioRenderRequest> pending;
    std::optional<StudioRenderResult> completed;
    std::uint64_t serial = 0;
    bool stopping = false;
    std::thread thread;

    explicit Impl(RenderFunction function) : render(std::move(function)), thread([this] { run(); }) {}

    void run() {
        for (;;) {
            StudioRenderRequest request;
            {
                std::unique_lock<std::mutex> lock(mutex);
                condition.wait(lock, [&] { return stopping || pending.has_value(); });
                if (stopping) return;
                request = std::move(*pending);
                pending.reset();
            }
            StudioRenderResult result;
            result.request = std::move(request);
            try {
                result.frame = render(result.request);
            } catch (...) {
                result.error = std::current_exception();
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                completed = std::move(result);
            }
        }
    }

    ~Impl() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
            pending.reset();
        }
        condition.notify_one();
        if (thread.joinable()) thread.join();
    }
};

StudioRenderWorker::StudioRenderWorker(RenderFunction render) {
    if (!render) {
        render = [](const StudioRenderRequest& request) {
            if(request.input.reference_project) return render_comparison_scene(*request.input.reference_project,
                request.input.snapshot,request.input.config,request.input.config.sampling,request.input.config.sampling,
                request.right_first,&request.input.project);
            return request.compare_modes
                ? render_comparison_scene(request.input.project, request.input.snapshot,
                    request.input.config, request.left, request.right, request.right_first)
                : render_scene(request.input.project, request.input.snapshot, request.input.config);
        };
    }
    impl_ = std::make_unique<Impl>(std::move(render));
}

StudioRenderWorker::~StudioRenderWorker() = default;

std::uint64_t StudioRenderWorker::submit(StudioRenderRequest request) {
    std::uint64_t serial;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        serial = ++impl_->serial;
        request.serial = serial;
        impl_->pending = std::move(request);
    }
    impl_->condition.notify_one();
    return serial;
}

std::optional<StudioRenderResult> StudioRenderWorker::poll() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto result = std::move(impl_->completed);
    impl_->completed.reset();
    return result;
}

} // namespace planet_sandbox
