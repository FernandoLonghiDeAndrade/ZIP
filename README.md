# **ZIP** (Zero-latency Instant Payment)

## Prerequisites

- [VS Code](https://code.visualstudio.com/)
- Extensions: [C/C++](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cpptools) and [CMake Tools](https://marketplace.visualstudio.com/items?itemName=ms-vscode.cmake-tools)
- [CMake](https://cmake.org/download) installed

## Build and Run Methods

### 1. Launch Button (Recommended)

> Click the **Launch** button (▶️) on the bottom status bar of VS Code (bottom left).  
> Press **Ctrl+Shift+P** and search for `CMake: Set Launch/Debug Target` to change the target (client or server).

### 2. Run with F5 (Runs on debug mode)

> In the **Run and Debug** tab, select the **Run Server** or **Run Client** configuration for your OS.  
> After that, just press **F5**.

### 3. Terminal

**Setup (first time only):**

```sh
mkdir build && cd build && cmake ..
```

**Build and run (in the build folder):**

```sh
# On Windows:

# Server:
cmake --build . && server.exe

# Client:
cmake --build . && client.exe
```

```sh
# On Linux/Mac:

# Server:
cmake --build . && ./server

# Client:
cmake --build . && ./server
```

- The **setup** command creates the `build` folder, enters it, and runs CMake to configure the project.
- The **build and run** command compiles the project and executes the generated binary.

> Run the setup command the first time you clone the project or if you delete the `build` folder.  
> After that, you can just use the build and run command.
