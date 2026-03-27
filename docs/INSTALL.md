# Installation Guide

This document provides step-by-step instructions for setting up the Linux DSP Acceleration system on native Linux and on Windows via WSL.

## 1. Native Linux (Ubuntu/Debian recommended)

### Hardware Requirements
- NVIDIA GPU (for Vulkan compute) OR NPU/DSP extension cards.
- PipeWire 0.3.x+ installed.

### Dependencies
Install the required build tools and libraries:
```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build libpipewire-0.3-dev libspa-0.3-dev \
                     libvulkan-dev vulkan-tools glslang-tools nodejs npm
```

### Installation
1. Clone the repository.
2. Run the automated setup script:
   ```bash
   chmod +x setup_linux_env.sh
   ./setup_linux_env.sh
   ```
3. Build the project:
   ```bash
   mkdir -p build && cd build
   cmake -G Ninja ..
   ninja
   ```

---

## 2. Windows via WSL (Virtual Environment)

Since this project targets the Linux audio stack, Windows users must use **WSL2** for development and testing (note: real-time audio performance may vary in WSL).

### Step 1: Install WSL2
Open PowerShell as Administrator and run:
```powershell
wsl --install
```
Restart your computer after installation.

### Step 2: Access the Project from WSL
1. Open your WSL terminal (e.g., Ubuntu).
2. Navigate to your Windows project folder (usually under `/mnt/`):
   ```bash
   cd /mnt/d/programmazione/linux_dsp_acceleration
   ```

### Step 3: GPU Acceleration in WSL
To enable Vulkan/GPU acceleration inside WSL:
1. Ensure you have the latest **NVIDIA Windows Drivers**.
2. Inside WSL, install the Vulkan loader:
   ```bash
   sudo apt install -y libvulkan1
   ```

### Step 4: Build & Run Tests
Follow the **Native Linux** instructions above starting from the "Dependencies" section. Use `./run_tests.sh` to verify your environment.

---

## 3. Running the System
1. **Load the PipeWire Module**:
   ```bash
   pw-cli load-module ./build/daemon/pipewire/module-dsp-accel.so
   ```
2. **Start a Hardware Worker**:
   ```bash
   ./build/daemon/dsp-accel-worker --type vulkan --id 0
   ```
3. **Monitor with MCP**:
   ```bash
   cd mcp-server && npm install && npm start
   ```

---

## 4. Configuring Advanced AI Agents (Optional)

If you are using an AI agent (like Antigravity) to help manage the system, you can integrate the **Engram** memory server for context persistence.

### Step 1: Install Engram
1. Download the latest `engram.exe` (Windows) or binary (Linux).
2. Place it in a directory in your PATH (e.g., `C:\Users\<user>\.bin\`).

### Step 2: Update `mcp_config.json`
Located in your Antigravity app data folder (usually `~/.gemini/antigravity/`):
```json
{
  "mcpServers": {
    "engram": {
      "command": "C:\\Users\\<user>\\.bin\\engram.exe",
      "args": ["mcp", "--tools=agent"]
    },
    "github": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-github"],
      "env": {
        "GITHUB_PERSONAL_ACCESS_TOKEN": "IL_TUO_TOKEN_QUI"
      }
    }
  }
}
```
**Important**: Ensure the `args` are set to `["mcp", "--tools=agent"]` (not `["serve"]`) to enable the correct stdio interface.
