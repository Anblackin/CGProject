#include "planet_sandbox/aa_benchmark.h"
#include "planet_sandbox/solar_system.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

using namespace planet_sandbox;

namespace {

struct TimedFrame {
    RenderFrame frame;
    double milliseconds = 0.0;
};

std::vector<std::string> command_line_arguments(int argc, char** argv) {
#ifdef _WIN32
    (void)argc;
    (void)argv;
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide) throw std::runtime_error("Could not read Windows command line");
    std::vector<std::string> arguments;
    try {
        arguments.reserve(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) {
            const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                                    wide[index], -1, nullptr, 0, nullptr, nullptr);
            if (length == 0) throw std::runtime_error("Could not encode command line as UTF-8");
            std::vector<char> encoded(static_cast<std::size_t>(length));
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[index], -1,
                                    encoded.data(), length, nullptr, nullptr) == 0) {
                throw std::runtime_error("Could not encode command line as UTF-8");
            }
            arguments.emplace_back(encoded.data());
        }
    } catch (...) {
        LocalFree(wide);
        throw;
    }
    LocalFree(wide);
    return arguments;
#else
    return {argv, argv + argc};
#endif
}

class StagedOutput {
public:
    explicit StagedOutput(const std::filesystem::path& destination)
        : destination_(std::filesystem::absolute(destination)) {
        if (std::filesystem::exists(destination_)) {
            throw std::invalid_argument("Output directory already exists: " + destination_.u8string());
        }
        std::filesystem::create_directories(destination_.parent_path());
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            auto candidate = destination_;
            candidate += ".partial-" + std::to_string(nonce + attempt);
            if (std::filesystem::create_directory(candidate)) {
                staging_ = std::move(candidate);
                return;
            }
        }
        throw std::runtime_error("Could not create a unique benchmark staging directory");
    }

    ~StagedOutput() {
        if (!staging_.empty()) {
            std::error_code ignored;
            std::filesystem::remove_all(staging_, ignored);
        }
    }

    const std::filesystem::path& directory() const { return staging_; }

    void publish() {
        if (std::filesystem::exists(destination_)) {
            throw std::runtime_error("Output directory appeared during benchmark export");
        }
        std::filesystem::rename(staging_, destination_);
        staging_.clear();
    }

private:
    std::filesystem::path destination_;
    std::filesystem::path staging_;
};

int parse_dimension(const std::string& value) {
    std::size_t consumed = 0;
    const int dimension = std::stoi(value, &consumed);
    if (consumed != value.size()) throw std::invalid_argument("Invalid dimension: " + value);
    return dimension;
}

TimedFrame render_timed(const AABenchmarkScene& scene, SamplingMode mode) {
    RenderConfig config = scene.config;
    config.sampling = mode;
    render_scene(scene.project, scene.snapshot, config);
    const auto start = std::chrono::steady_clock::now();
    RenderFrame frame = render_scene(scene.project, scene.snapshot, config);
    const auto end = std::chrono::steady_clock::now();
    return {std::move(frame), std::chrono::duration<double, std::milli>(end - start).count()};
}

void write_row(std::ostream& output, const AABenchmarkScene& scene, const char* mode,
               const TimedFrame& timed, const AAError& error) {
    output << scene.name << ',' << mode << ',' << scene.config.width << ',' << scene.config.height
           << ',' << error.edge_pixels << ',' << error.edge_mae_linear << ','
           << error.full_mae_linear << ',' << timed.frame.shade_evaluations << ','
           << timed.milliseconds << '\n';
    if (!output) throw std::runtime_error("Could not write benchmark CSV");
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::vector<std::string> arguments = command_line_arguments(argc, argv);
        std::string output_dir, config_path;
        int width = 256, height = 192;
        for (std::size_t i = 1; i < arguments.size(); ++i) {
            const std::string& option = arguments[i];
            if (option == "--help") {
                std::cout << "Usage: planet_aa_benchmark --output-dir DIR "
                             "[--config solar-system.json] [--width N] [--height N]\n";
                return 0;
            }
            if (i + 1 >= arguments.size()) throw std::invalid_argument("Missing value for " + option);
            const std::string& value = arguments[++i];
            if (option == "--output-dir") output_dir = value;
            else if (option == "--config") config_path = value;
            else if (option == "--width") width = parse_dimension(value);
            else if (option == "--height") height = parse_dimension(value);
            else throw std::invalid_argument("Unknown option: " + option);
        }
        if (output_dir.empty()) throw std::invalid_argument("--output-dir is required");
        Project project = make_solar_system_project();
        if (!config_path.empty()) {
            std::ifstream input{std::filesystem::u8path(config_path)};
            if (!input) throw std::runtime_error("Could not open project: " + config_path);
            project = read_project(input);
        }
        const std::array<AABenchmarkScene, 3> scenes{{
            make_aa_benchmark_scene(project, AAScene::Earth, width, height),
            make_aa_benchmark_scene(project, AAScene::Saturn, width, height),
            make_aa_benchmark_scene(project, AAScene::Orbit, width, height)}};
        StagedOutput output{std::filesystem::u8path(output_dir)};
        const std::filesystem::path& directory = output.directory();
        std::ofstream project_output(directory / "project.json");
        if (!project_output) throw std::runtime_error("Could not write project.json");
        write_project(project_output, project);
        project_output.close();
        if (!project_output) throw std::runtime_error("Could not finish project.json");
        std::ofstream csv(directory / "metrics.csv");
        if (!csv) throw std::runtime_error("Could not write metrics.csv");
        csv << "scene,mode,width,height,edge_pixels,edge_mae_linear,full_mae_linear,"
               "shade_evaluations,render_ms\n" << std::setprecision(10);

        for (const AABenchmarkScene& scene : scenes) {
            const TimedFrame reference = render_timed(scene, SamplingMode::SSAA16);
            const auto mask = make_aa_edge_mask(reference.frame);
            write_png((directory / (scene.name + "_ssaa16.png")).u8string(), reference.frame.image);
            Image mask_image{width, height, std::vector<std::uint8_t>(mask.size() * 3, 0)};
            for (std::size_t pixel = 0; pixel < mask.size(); ++pixel) {
                const std::uint8_t value = mask[pixel] ? 255 : 0;
                mask_image.rgb[pixel * 3] = value;
                mask_image.rgb[pixel * 3 + 1] = value;
                mask_image.rgb[pixel * 3 + 2] = value;
            }
            write_png((directory / (scene.name + "_edges.png")).u8string(), mask_image);
            write_row(csv, scene, "ssaa16", reference,
                      measure_aa_error(reference.frame.image, reference.frame.image, mask));
            for (const auto& option : std::array<std::pair<SamplingMode, const char*>, 3>{
                     {{SamplingMode::Single, "single"}, {SamplingMode::SSAA4, "ssaa4"},
                      {SamplingMode::MSAA4, "msaa4"}}}) {
                const TimedFrame candidate = render_timed(scene, option.first);
                write_png((directory / (scene.name + "_" + option.second + ".png")).u8string(),
                          candidate.frame.image);
                write_row(csv, scene, option.second, candidate,
                          measure_aa_error(candidate.frame.image, reference.frame.image, mask));
            }
        }
        csv.close();
        if (!csv) throw std::runtime_error("Could not finish metrics.csv");
        output.publish();
        std::cout << "Wrote benchmark to " << output_dir << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
