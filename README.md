# ⚡ CM Language

**Professional C#-like/Go-hybrid Language for C-performance Systems**

CM is a modern programming language designed for high-performance systems development. It combines the safety and ergonomics of modern languages like Go and Rust with the raw power and control of C.

## ✨ Key Features

- **🚀 Performance**: Transpiles directly to C11, ensuring maximum performance and portability.
- **🛡️ Memory Safety**: Features a built-in reference-counting garbage collector for effortless memory management.
- **💎 Modern Syntax**: Clean, C#-like syntax with powerful features like pattern matching and type-agnostic `println`.
- **🛠️ Integrated CLI**: A unified tool for building, running, and managing packages.
- **🔌 C Interoperability**: Seamlessly call C functions or embed C code directly using `c { ... }` blocks.

## 🏁 Quick Start

### Installation

Download and run the installer for your platform:

- **Windows**: `powershell -ExecutionPolicy Bypass -File .\install.ps1`
- **Linux/macOS**: `bash ./install.sh`

### Your First Program

Create a file named `hello.cm`:

```cm
fn main() {
    println("Hello, CM v2!");
}
```

Run it immediately:

```bash
cm run hello.cm
```

## 📖 Documentation

- [Language Guide](docs/LANGUAGE_GUIDE.md) - Learn the syntax and core concepts.
- [CLI Reference](docs/CLI_REFERENCE.md) - Detailed guide for the `cm` command.
- [Installation Guide](docs/INSTALLATION.md) - Setup instructions for all platforms.

## 🤝 Contributing

We welcome contributions! Please check out our [GitHub repository](https://github.com/ALightbolt4G/CM-lang) for more information.

---
*✨ Happy coding with CM!*
