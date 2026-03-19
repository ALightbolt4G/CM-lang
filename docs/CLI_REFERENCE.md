# 📟 CM CLI Reference (v2-SPEC)

The `cm` command is the central tool for interacting with the CM language. It handles building, running, and managing your projects.

## 🚀 Basic Usage

```bash
cm <command> [options] <entry.cm>
```

### `cm build <entry.cm>`
Compiles your CM program and generates an executable named `a.exe` (Windows) or `a.out` (Unix) by default.

- `-o output` — Specify a custom filename for the resulting executable.
- `cm build main.cm -o demo.exe`

### `cm run <entry.cm>`
Builds and executes your CM program in one step. It automatically handles the intermediate C transpilation and final native compilation.

- `cm run main.cm`

### `cm emitc <entry.cm>`
Generates the intermediate C code for your CM program without calling the native compiler. Use this to inspect the transpiled output.

- `-o main.c` — Specify the output C filename.

## 📦 Package Management

CM features a modern package manager to handle project dependencies.

| Command | Description |
|---|---|
| `cm packages init` | Initialize a new project in the current folder. |
| `cm packages install <name>` | Download and install a package. |
| `cm packages list` | View installed project dependencies. |
| `cm packages update` | Check for updates to installed packages. |

## 🛠️ Utility Commands

- **`cm highlight <file>`**: Generate colorized terminal output for a CM file.
- **`cm new <name>`**: Scaffold a new CM project with a standard folder structure.
- **`cm --version`**: Display the current CM compiler version and build info.
- **`cm --info`**: Show detailed information about the compiler and local environment.

---
*✨ CM CLI: Simplifying high-performance development.*
