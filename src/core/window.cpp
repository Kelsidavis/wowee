#include "core/window.hpp"
#include "core/logger.hpp"
#include <GL/glew.h>

namespace wowee {
namespace core {

Window::Window(const WindowConfig& config)
    : config(config)
    , width(config.width)
    , height(config.height) {
}

Window::~Window() {
    shutdown();
}

bool Window::initialize() {
    LOG_INFO("Initializing window: ", config.title);

    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        LOG_ERROR("Failed to initialize SDL: ", SDL_GetError());
        return false;
    }

    // Set OpenGL attributes
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    // Create window
    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
    if (config.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    if (config.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }

    window = SDL_CreateWindow(
        config.title.c_str(),
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        flags
    );

    if (!window) {
        LOG_ERROR("Failed to create window: ", SDL_GetError());
        return false;
    }

    // Create OpenGL context
    glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        LOG_ERROR("Failed to create OpenGL context: ", SDL_GetError());
        return false;
    }

    // Set VSync
    if (SDL_GL_SetSwapInterval(config.vsync ? 1 : 0) != 0) {
        LOG_WARNING("Failed to set VSync: ", SDL_GetError());
    }

    // Initialize GLEW
    glewExperimental = GL_TRUE;
    GLenum glewError = glewInit();
    if (glewError != GLEW_OK) {
        LOG_ERROR("Failed to initialize GLEW: ", glewGetErrorString(glewError));
        return false;
    }

    // Log OpenGL info
    LOG_INFO("OpenGL Version: ", glGetString(GL_VERSION));
    LOG_INFO("GLSL Version: ", glGetString(GL_SHADING_LANGUAGE_VERSION));
    LOG_INFO("Renderer: ", glGetString(GL_RENDERER));
    LOG_INFO("Vendor: ", glGetString(GL_VENDOR));

    // Set up OpenGL defaults
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    LOG_INFO("Window initialized successfully");
    return true;
}

void Window::shutdown() {
    if (glContext) {
        SDL_GL_DeleteContext(glContext);
        glContext = nullptr;
    }

    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    SDL_Quit();
    LOG_INFO("Window shutdown complete");
}

void Window::swapBuffers() {
    SDL_GL_SwapWindow(window);
}

void Window::pollEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // ImGui will handle events in UI manager
        // For now, just handle quit
        if (event.type == SDL_QUIT) {
            shouldCloseFlag = true;
        }
        else if (event.type == SDL_WINDOWEVENT) {
            if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                width = event.window.data1;
                height = event.window.data2;
                glViewport(0, 0, width, height);
                LOG_DEBUG("Window resized to ", width, "x", height);
            }
        }
    }
}

} // namespace core
} // namespace wowee
