#include "planet_sandbox/renderer.h"
#include "planet_sandbox/studio_ui.h"
#include "planet_sandbox/studio_render_pipeline.h"
#include "planet_sandbox/solar_system.h"

#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <string>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace {

void glfw_error(int code, const char* message) {
    std::fprintf(stderr, "GLFW %d: %s\n", code, message);
}

} // namespace

int main(int argc, char** argv) {
    try {
        const char* path = argc > 1 ? argv[1] : "my-solar-system.json";
        planet_sandbox::StudioState state(
            planet_sandbox::open_studio_project(std::filesystem::u8path(path), argc <= 1), path);
        planet_sandbox::initialize_studio_document(state);

        glfwSetErrorCallback(glfw_error);
        if (!glfwInit()) throw std::runtime_error("Could not initialize GLFW");
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        GLFWwindow* window = glfwCreateWindow(1280, 800, "Planet Studio", nullptr, nullptr);
        if (!window) {
            glfwTerminate();
            throw std::runtime_error("Could not create the editor window");
        }
        glfwSetWindowSizeLimits(window, 1100, 700, GLFW_DONT_CARE, GLFW_DONT_CARE);
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();
        ImGui::GetStyle().WindowRounding = 0.0f;
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 130");

        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        std::uint64_t uploaded_frame = 0;
        planet_sandbox::StudioRenderPipeline pipeline;
        double previous = glfwGetTime();
        while (!glfwWindowShouldClose(window) && !state.request_close) {
            glfwPollEvents();
            state.preview_interacting = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS ||
                                        glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
            const double now = glfwGetTime();
            planet_sandbox::advance_studio(state, std::max(0.0, now - previous));
            planet_sandbox::update_studio_document(state, std::max(0.0, now - previous));
            previous = now;

            const auto input = planet_sandbox::make_studio_render_input(state);
            const auto& frame = pipeline.update(state, input, now);
            const auto& image = frame.image;
            if (uploaded_frame != state.frame_cache.render_count) {
                glBindTexture(GL_TEXTURE_2D, texture);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, image.width, image.height, 0,
                             GL_RGB, GL_UNSIGNED_BYTE, image.rgb.data());
                uploaded_frame = state.frame_cache.render_count;
            }

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            planet_sandbox::draw_studio(state, static_cast<ImTextureID>(texture), input, frame, false);
            ImGui::Render();

            int width = 0, height = 0;
            glfwGetFramebufferSize(window, &width, &height);
            glViewport(0, 0, width, height);
            glClearColor(0.045f, 0.063f, 0.085f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(window);
        }

        planet_sandbox::shutdown_studio_document(state);
        glDeleteTextures(1, &texture);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Planet Studio: %s\n", error.what());
        return 1;
    }
}
