# WARP.md

This file provides guidance to WARP (warp.dev) when working with code in this repository.

## Project Overview

MinimalGL is a PC Intro (PC Demo Scene) authoring tool specialized for creating PC 4K Intros (executables under 4096 bytes). It supports real-time GLSL shader development with hot-reloading and can export minified executables using Shader Minifier and Crinkler compression.

**Key Features:**
- Real-time graphics shader development with fragment shaders
- Real-time audio synthesis using compute shaders
- Live hot-reloading of shaders during development
- Export to compressed executables (targeting <4KB for PC 4K Intro category)
- Multiple Render Targets (MRT) support
- Camera controls and cubemap capture
- User texture loading (PNG/DDS) for development
- Texture unit binding system for shader resources

## Common Development Commands

### Building the Project
```powershell
# Build using the provided script (uses MSBuild via Visual Studio)
scripts\build.bat

# Or build directly with MSBuild (requires Visual Studio tools in PATH)
msbuild minimal_gl.sln /p:Configuration=Release /p:Platform=x64
```

### Cleaning Build Artifacts
```powershell
scripts\clean.bat
```

### Creating Release Archive
```powershell
scripts\make-release-archive.bat
```

### Running Tests
There are no automated unit tests. Testing is done by:
1. Loading shader examples from `examples/` directory
2. Testing compute shader integration with `test_compute_*.glsl` files
3. Manual validation of exported executables on different GPU vendors

## Architecture Overview

### Core Components

**Application Layer** (`src/app.cpp`, `src/app.h`)
- Main application logic and GUI management
- Uses ImGui for interface rendering
- Handles shader file hot-reloading via file timestamp monitoring
- Manages project settings serialization (JSON-based)
- Coordinates between graphics, sound, and export subsystems

**Graphics System** (`src/graphics.cpp`, `src/graphics.h`)
- OpenGL 4.3 core profile renderer
- Manages multiple render targets (MRT) with double-buffering
- Handles vertex/fragment/compute shader compilation and pipeline management
- Texture unit binding system:
  - Units 0-3: MRT back buffers
  - Units 4-7: Compute shader render targets
  - Units 8+: User textures
- Supports various pixel formats (UNORM8, FP16, FP32)

**Sound System** (`src/sound.cpp`, `src/sound.h`)
- Compute shader-based audio synthesis
- Real-time audio buffer management with partitioned dispatch
- 48kHz stereo output with configurable buffer sizes
- Integration with Windows multimedia APIs

**Export System** (`src/export_executable.cpp`, `src/export_executable.h`)
- Coordinates shader minification via Shader Minifier
- Executable compression via Crinkler
- Template-based code generation for standalone executables
- Handles resource embedding and size optimization

### Shader System Architecture

MinimalGL uses a dual-pipeline approach:

1. **Development Pipeline**: Full-featured environment with debugging capabilities
2. **Export Pipeline**: Minified and optimized for size compression

**Texture Unit Allocation:**
- `binding=0-3`: MRT back buffers from previous frame
- `binding=4-7`: Compute shader render targets (when compute shaders are used)
- `binding=8+`: User-loaded textures (development only)

**Uniform Locations** (defined in `src/config.h`):
- `location=0`: waveOutPosition (audio playback position)
- `location=1`: frameCount
- `location=2`: time (elapsed seconds)
- `location=3`: resolution (screen resolution)
- `location=4`: mousePos
- `location=5`: mouseButtons
- `location=6`: tanFovY (camera field of view)
- `location=7`: cameraCoord (current camera transform)
- `location=8`: prevCameraCoord (previous frame camera)

### Pipeline Description Prototype

- `src/pipeline_description.h` / `.cpp` define a JSON-serializable frame-graph prototype matching the design in `pipeline_design_jp.txt`.
- Resources model pixel format, resolution source, history length, and sampler defaults while reserving slots for GL texture handles.
- Passes capture type (fragment/compute/present), shader path, IO bindings with access modes, optional clear directives, and compute work-group overrides.
- `GraphicsUpdate` now interprets a `PipelineDescription`; when no custom pipeline is supplied it synthesizes the legacy {compute → fragment → present} sequence automatically.
- Exported runtimes embed the same description via `pipeline_description.inl`, so `src/resource/main.cpp` replays the identical frame-graph instead of hard-coded ping-pong logic.
- See `examples/pipeline_sample.json` for a reference that expresses a fragment → compute → fragment → present flow with ping-pong history access.

### External Dependencies

**Required Tools** (must be in PATH or same directory as executable):
- `shader_minifier.exe`: GLSL code minification
- `crinkler.exe`: Executable compression for size optimization

**Embedded Libraries:**
- `src/external/imgui/`: Dear ImGui for GUI
- `src/external/cJSON/`: JSON parsing for project files
- `src/external/stb/`: STB libraries for image I/O
- `src/GL/gl3w.*`: OpenGL function loading

## Development Workflow

### Working with Shaders

1. **Graphics Shaders**: Use `*.gfx.glsl` extension
   - Fragment shaders for visual content
   - Hot-reloaded when file timestamp changes
   - See `examples/` for templates

2. **Sound Shaders**: Use `*.snd.glsl` extension
   - Compute shaders for audio synthesis
   - Include special pragma directives for export compatibility

3. **Compute Shaders**: Use `*.compute.glsl` extension
   - For advanced graphics computation
   - Write to render targets via image units
   - Example: `test_compute_simple.compute.glsl`

### Texture Unit Management

When writing shaders, respect the binding allocation:
```glsl
// MRT back buffers (previous frame)
layout(binding = 0) uniform sampler2D backBuffer0;
layout(binding = 1) uniform sampler2D backBuffer1;
// ...

// Compute render targets (current frame)
layout(binding = 4) uniform sampler2D computeRT0;
layout(binding = 5) uniform sampler2D computeRT1;
// ...

// User textures (development only)
layout(binding = 8) uniform sampler2D userTexture0;
// ...
```

### Export Process

1. Test shaders in development environment
2. Use "Export Executable" dialog to generate `.exe`
3. Check generated `.crinkler_report.html` for size analysis
4. Optimize based on size report and minified `.inl` files
5. Test exported executable on multiple GPU vendors (NVIDIA, AMD, Intel)

## Platform-Specific Notes

**Windows Only**: This project is Windows-specific and requires:
- Microsoft Visual Studio 2019+ with C++ tools
- Windows SDK
- OpenGL 4.3+ compatible GPU

**File Handling**: Uses Windows-specific paths and APIs throughout the codebase. File drag-and-drop is supported for shader loading.

## Current Development Focus

Based on `compute_plan_jp.txt`, active development is focused on:
- Compute shader integration with double-buffered render targets
- Maintaining compatibility between editor and exported executable behavior
- Texture unit binding system refinement
- Hot-reload stability improvements

The project maintains careful attention to size optimization since the target is sub-4KB executables for the demo scene community.
