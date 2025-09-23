# **ZIP** (Zero-latency Instant Payment)

## Prerequisites

- [CMake](https://cmake.org/download) installed

## Recommended IDE Setup

- [VS Code](https://code.visualstudio.com/)
- Extensions: [C/C++](vscode:extension/ms-vscode.cpptools) and [CMake Tools](vscode:extension/ms-vscode.cmake-tools)

## Build and Run Methods

*Methods 1 and 2 require VS Code with the recommended extensions. Method 3 works with any terminal or IDE.*

### 1. Launch Button (Recommended)

> Click the **Launch** button (▶️) on the bottom status bar of VS Code (bottom left).  
> Press **Ctrl+Shift+P** and search for `CMake: Set Launch/Debug Target` to change the target (client or server).

### 2. Run with F5 (Runs on debug mode)

> In the **Run and Debug** tab, select the **Run Server** or **Run Client** configuration for your OS.  
> After that, just press **F5**.

### 3. Terminal

**Setup (first time only, run in project root):**

```sh
~/ZIP$ mkdir build && cd build && cmake ..
```

**Build and run (in the build folder):**

```sh
# On Windows:

# Server:
~\ZIP\build> cmake --build . && server.exe

# Client:
~\ZIP\build> cmake --build . && client.exe
```

```sh
# On Linux/Mac:

# Server:
~/ZIP/build$ cmake --build . && ./server

# Client:
~/ZIP/build$ cmake --build . && ./client
```

- The **setup** command creates the `build` folder, enters it, and runs CMake to configure the project.
- The **build and run** command compiles the project and executes the generated binary.

> Run the setup command the first time you clone the project or if you delete the `build` folder.  
> After that, you can just use the build and run command.
