cmake_minimum_required(VERSION 3.20)
project(cloth_sim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ── GLFW 3.4 ─────────────────────────────────────────────────────────────────
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# ── GLM 1.0.1 (header-only) ───────────────────────────────────────────────────
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.1
    GIT_SHALLOW    TRUE
)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

# ── Dear ImGui v1.91.5 ────────────────────────────────────────────────────────
# ImGui has no CMakeLists.txt we want to use; populate without add_subdirectory
# so we can build its sources directly as a static lib below.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
    GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui_lib PRIVATE glfw)
target_compile_definitions(imgui_lib PUBLIC
    GLFW_INCLUDE_NONE       # prevent GLFW from pulling in its own GL headers
    GL_SILENCE_DEPRECATION  # suppress macOS OpenGL deprecation warnings
)
if(APPLE)
    target_link_libraries(imgui_lib PUBLIC "-framework OpenGL")
endif()

# ── OpenCL — macOS system framework (no FetchContent) ────────────────────────
find_package(OpenCL REQUIRED)

# ── cloth_sim executable ──────────────────────────────────────────────────────
add_executable(cloth_sim
    src/main.cpp
    src/gpu/compute_pipeline.cpp
    src/gpu/render_pipeline.cpp
    src/gpu/buffer_manager.cpp
    src/sim/cloth.cpp
    src/sim/interaction.cpp
    src/render/cloth_mesh.cpp
    src/render/bowl_mesh.cpp
)

target_include_directories(cloth_sim PRIVATE
    src/
    ${glm_SOURCE_DIR}   # GLM headers at <glm/glm.hpp> relative to repo root
)

target_compile_definitions(cloth_sim PRIVATE
    GLFW_INCLUDE_NONE       # we include <OpenGL/gl3.h> manually before <GLFW/glfw3.h>
    GL_SILENCE_DEPRECATION  # macOS deprecates OpenGL; silence the noise
)

target_link_libraries(cloth_sim PRIVATE
    glfw
    imgui_lib
    OpenCL::OpenCL
)

if(APPLE)
    target_link_libraries(cloth_sim PRIVATE "-framework OpenGL")
endif()
