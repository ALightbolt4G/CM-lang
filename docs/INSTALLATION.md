# ⚙️ CM Installation Guide (v2-SPEC)

Before installing the CM compiler, ensure that you have a C compiler installed (`GCC`, `Clang`, or `MSVC`) and added to your system's `PATH`.

## 🪟 Windows Setup (PowerShell)

1. Open a PowerShell terminal (Administrator recommended).
2. Navigate to the CM project root.
3. Execute the installation script:

```powershell
powershell -ExecutionPolicy Bypass -File .\install.ps1
```

This will compile the CM source code and add the resulting `cm.exe` and its dependencies to your local environment.

## 🐧 Linux / 🍎 macOS Setup (Bash)

1. Open your terminal of choice.
2. Navigate to the CM project root.
3. Run the setup shell script:

```bash
chmod +x install.sh
./install.sh
```

The script will detect your package manager (`apt-get`, `brew`, etc.), install necessary C toolchains if missing, and compile the CM binary locally.

## ✅ Verification

Once the installation is complete, verify it by checking the version in a **new** terminal session:

```bash
cm --version
```

If the command is not found, ensure that the CM binary location has been successfully added to your system's `PATH`.

---
*✨ CM Installation: Get started with CM in minutes.*
