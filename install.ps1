param (
    [switch]$InstallDeps
)

Write-Host "⚡ CM Language Installer for Windows" -ForegroundColor Cyan
Write-Host "====================================" -ForegroundColor Cyan

$hasGcc = Get-Command gcc -ErrorAction SilentlyContinue
$hasCMake = Get-Command cmake -ErrorAction SilentlyContinue

if (-not $hasCMake) {
    Write-Host "❌ CMake is not installed or not in PATH." -ForegroundColor Red
    Write-Host "Please install CMake from https://cmake.org/download/ or run: winget install CMake" -ForegroundColor Yellow
    exit 1
}

if (-not $hasGcc) {
    Write-Host "❌ GCC (MinGW) is not installed or not in PATH." -ForegroundColor Red
    if ($InstallDeps) {
        Write-Host "Attempting to install GCC via winget..." -ForegroundColor Yellow
        winget install --id MSYS2.MSYS2 --accept-source-agreements --accept-package-agreements
        Write-Host "MSYS2 installed. You must open MSYS2 MINGW64 shell and run: pacman -S mingw-w64-x86_64-toolchain" -ForegroundColor Yellow
    } else {
        Write-Host "Please install MinGW-w64 or enable it in your path." -ForegroundColor Yellow
        Write-Host "Tip: To auto-install MSYS2, run: .\install.ps1 -InstallDeps" -ForegroundColor Yellow
    }
    exit 1
}

Write-Host "✅ Dependencies found. Building CM..." -ForegroundColor Green
cmake -B build -G "MinGW Makefiles"
cmake --build build

if ($LASTEXITCODE -eq 0) {
    Write-Host "✨ CM Compiler successfully built!" -ForegroundColor Green
    Write-Host "Executable is located at: .\build\cm.exe" -ForegroundColor Green
    Write-Host "Copying cm.exe to root directory..." -ForegroundColor Cyan
    Copy-Item ".\build\cm.exe" -Destination ".\cm.exe" -Force
    Write-Host "✅ Done! You can now use: .\cm.exe run tests\oop_test.cm" -ForegroundColor Green
} else {
    Write-Host "❌ Build failed!" -ForegroundColor Red
    exit 1
}
