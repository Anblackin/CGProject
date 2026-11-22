#pragma once

#include "planet_sandbox/studio_ui.h"

#include <exception>
#include <functional>
#include <memory>

namespace planet_sandbox {

// Jobs own every input; the render thread never accesses editor state.
struct StudioRenderRequest {
    StudioRenderInput input;
    std::string key;
    std::string context_key;
    std::string measurement_key;
    std::uint64_t serial = 0;
    bool compare_modes = false;
    SamplingMode left = SamplingMode::Single;
    SamplingMode right = SamplingMode::MSAA4;
    bool right_first = false;
    bool measuring = false;
    bool preview = false;
};

struct StudioRenderResult {
    StudioRenderRequest request;
    std::optional<RenderFrame> frame;
    std::exception_ptr error;
};

class StudioRenderWorker {
public:
    using RenderFunction = std::function<RenderFrame(const StudioRenderRequest&)>;

    explicit StudioRenderWorker(RenderFunction render = {});
    ~StudioRenderWorker();
    StudioRenderWorker(const StudioRenderWorker&) = delete;
    StudioRenderWorker& operator=(const StudioRenderWorker&) = delete;

    std::uint64_t submit(StudioRenderRequest request);
    std::optional<StudioRenderResult> poll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class StudioRenderPipeline {
public:
    static constexpr double refine_delay_seconds = 0.18;

    explicit StudioRenderPipeline(StudioRenderWorker::RenderFunction render = {});
    ~StudioRenderPipeline();
    StudioRenderPipeline(const StudioRenderPipeline&) = delete;
    StudioRenderPipeline& operator=(const StudioRenderPipeline&) = delete;

    const RenderFrame& update(StudioState& state, const StudioRenderInput& input,
                              double now_seconds);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Check again at the click, since sidebar controls can change state during a UI frame.
bool studio_frame_can_interact(const StudioState& state);

} // namespace planet_sandbox
