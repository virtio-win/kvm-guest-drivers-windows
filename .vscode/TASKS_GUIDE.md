# VS Code Tasks Quick Start Guide

## Prerequisites

Before running build tasks, ensure you have:
- Enterprise Windows Driver Kit (EWDK) installed - [See installation guide](https://virtio-win.github.io/Development/Building-the-drivers-using-Windows-11-24H2-EWDK#:~:text=Copy%20Windows%2011,EWDK11_DIR%3Dc%3A%5Cewdk11)

## How to Execute VS Code Tasks

There are several ways to run build tasks:

### Method 1: Command Palette (Recommended)
1. Press `Ctrl+Shift+P`to open the Command Palette
2. Type "Tasks: Run Task" and select it
3. Browse and select your desired task (e.g., "Build: NetKVM")
4. Follow any prompts for Windows version and architecture

### Method 2: Terminal Menu
1. Click `Terminal` in the top menu bar
2. Select `Run Task...`
3. Choose your task from the list
4. Follow any prompts for Windows version and architecture


## Available Build Tasks

### Full Project Builds
- **▶ build_AllNoSdv: Win10 [All Archs]** - Build all drivers for Windows 10
- **▶ build_AllNoSdv: Win10 [Choose Arch]** - Build all drivers for Windows 10 with specific architecture
- **▶ build_AllNoSdv: Win11 [All Archs]** - Build all drivers for Windows 11
- **▶ build_AllNoSdv: Win11 [Choose Arch]** - Build all drivers for Windows 11 with specific architecture

### Individual Driver Builds
- **Build: NetKVM**
- **Build: viostor**
- **Build: vioserial**
- **Build: Balloon**
- And many more...

### Other Tasks
- **Clean: All Drivers** - Clean all build artifacts
- **🔐 Sign All Drivers** - Sign all built drivers

## Understanding Build Output

### Terminal Panel
The build output appears in the **Terminal** panel at the bottom of VS Code:
Build progress and compiler messages are shown in real-time.


## Navigating to Errors

### Clicking on Error Messages
When build errors occur, you can quickly jump to the problematic code:

1. **In the Terminal panel:**
   - Hold `Ctrl` key
   - Click on any error line that shows a file path
   - Example: `C:\path\to\file.cpp(280,32): error C2039`
   - VS Code will open the file and jump to the exact line and column
