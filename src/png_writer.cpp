#include "planet_sandbox/aa_benchmark.h"

#define STBIW_WINDOWS_UTF8
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <limits>
#include <stdexcept>

namespace planet_sandbox {

void write_png(const std::string& path, const Image& image) {
    if (image.width <= 0 || image.height <= 0 || image.width > std::numeric_limits<int>::max() / 3 ||
        image.rgb.size() != static_cast<std::size_t>(image.width) * image.height * 3u) {
        throw std::invalid_argument("Invalid RGB image for PNG export");
    }
    if (!stbi_write_png(path.c_str(), image.width, image.height, 3,
                        image.rgb.data(), image.width * 3)) {
        throw std::runtime_error("Could not write PNG image: " + path);
    }
}

} // namespace planet_sandbox
