#!/bin/bash
set -e

echo "====================================================="
echo "   Linux DSP Acceleration - Environment Setup        "
echo "====================================================="

echo "--- Installing build tools and dependencies ---"
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config libpipewire-0.3-dev libspa-0.2-dev \
                     libvulkan-dev vulkan-tools glslang-tools nodejs npm

echo "--- Verifying Vulkan installation ---"
if command -v vulkaninfo >/dev/null 2>&1; then
    echo "Vulkan loader detected. Checking for Real-Time extension (VK_EXT_global_priority)..."
    if vulkaninfo | grep -q "VK_EXT_global_priority"; then
        echo "SUCCESS: Hardware supports Real-Time Scheduling."
    else
        echo "INFO: VK_EXT_global_priority not found. System will use standard priority (fully functional, but less resilient to GPU load)."
    fi
else
    echo "WARNING: vulkan-tools not found. GPU acceleration may require manual driver setup in WSL."
fi

echo "--- Preparing CLAP SDK ---"
if [ ! -d "clap-sdk" ]; then
    git clone --depth 1 https://github.com/free-audio/clap.git clap-sdk
else
    echo "CLAP SDK already present, skipping clone."
fi

echo "--- Setup complete! ---"
echo ""
echo "To compile the project, run these commands inside the build directory:"
echo "  # Da root del progetto:"
echo "  mkdir -p build && cd build && rm -rf *"
echo "  cmake -G Ninja .."
echo "  ninja"
echo ""
echo "Execution sequence:"
echo "1. Load the Supervisor module into PipeWire:"
echo "   pw-cli load-module ./build/daemon/pipewire/module-dsp-accel.so"
echo "2. Start a Hardware Worker (e.g., Vulkan):"
echo "   ./build/daemon/dsp-accel-worker --type vulkan --id 0"
echo ""
echo "Resource Validation:"
echo "   ./run_tests.sh"
