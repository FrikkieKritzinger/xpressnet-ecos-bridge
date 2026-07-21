# GitHub Repository Setup Guide

This guide explains how to take the generated files and set up a complete GitHub repository.

## Step 1: Create Repository Structure Locally

### Option A: Manual Setup (Windows)

1. **Create folder structure:**
   ```
   E:\Claude\Bridge\
   ├── docs/
   ├── src/
   │   ├── protocols/
   │   │   ├── xpressnet/
   │   │   ├── ecos/
   │   │   ├── loconet/
   │   │   └── z21lan/
   │   ├── display/
   │   ├── interfaces/
   │   └── utils/
   ├── libraries/
   ├── tests/
   └── .gitignore
   ```

2. **Place downloaded files:**
   - `00_README.md` → `E:\Claude\Bridge\README.md`
   - `01_DESIGN_DOCUMENT.md` → `E:\Claude\Bridge\docs\01_DESIGN_DOCUMENT.md`
   - `src_config.h` → `E:\Claude\Bridge\src\config.h`
   - `src_definitions.h` → `E:\Claude\Bridge\src\definitions.h`
   - `src_state_engine.h` → `E:\Claude\Bridge\src\state_engine.h`
   - `src_command_router.h` → `E:\Claude\Bridge\src\command_router.h`
   - `src_interfaces_interface_base.h` → `E:\Claude\Bridge\src\interfaces\interface_base.h`
   - `src_main.ino` → `E:\Claude\Bridge\src\main.ino`
   - `.gitignore` → `E:\Claude\Bridge\.gitignore`

### Option B: Automated Setup (Git Bash / PowerShell)

```powershell
# PowerShell commands
$basePath = "E:\Claude\Bridge"

# Create folder structure
mkdir -p "$basePath/docs"
mkdir -p "$basePath/src/protocols/{xpressnet,ecos,loconet,z21lan}"
mkdir -p "$basePath/src/display"
mkdir -p "$basePath/src/interfaces"
mkdir -p "$basePath/src/utils"
mkdir -p "$basePath/libraries"
mkdir -p "$basePath/tests"

# Copy files (adjust paths to where you downloaded them)
Copy-Item ".\00_README.md" "$basePath\README.md"
Copy-Item ".\01_DESIGN_DOCUMENT.md" "$basePath\docs\"
Copy-Item ".\src_*.h" "$basePath\src\"
Copy-Item ".\src_main.ino" "$basePath\src\main.ino"
Copy-Item ".\.gitignore" "$basePath\.gitignore"
```

## Step 2: Create Skeleton Files (Placeholders)

For now, create empty placeholder files for protocol implementations:

```powershell
# Create empty header files for future implementation
@"
/*
 * Placeholder - Implementation coming
 */

#ifndef XPRESSNET_INTERFACE_H
#define XPRESSNET_INTERFACE_H

#include "../../interfaces/interface_base.h"

class XpressNetInterface : public ProtocolInterface {
public:
    bool begin() override { return false; }
    void update() override {}
    void sendSpeedCommand(uint16_t address, uint8_t speed, uint8_t direction) override {}
    void sendFunctionCommand(uint16_t address, uint32_t functions) override {}
    ComponentStatus getStatus() const override { return ComponentStatus::DISCONNECTED; }
    const char* getName() const override { return "XpressNet"; }
};

#endif
"@ | Out-File -Encoding UTF8 "$basePath\src\protocols\xpressnet\xpressnet_interface.h"

# Repeat for other protocol headers...
```

## Step 3: Initialize Git Repository

### Install Git (if not already installed)
- Download from https://git-scm.com/download/win
- Use default installation options

### Initialize Repository

```powershell
cd "E:\Claude\Bridge"

# Initialize git repo
git init

# Configure git (one time)
git config user.name "Your Name"
git config user.email "your.email@example.com"

# Add all files
git add .

# Create initial commit
git commit -m "Initial commit: Project skeleton and architecture"

# View commit history
git log
```

## Step 4: Create GitHub Repository (Web)

1. **Go to GitHub.com**
   - Sign in (or create account)
   - Click "+" → "New repository"

2. **Repository settings:**
   - **Name:** `xpressnet-ecos-bridge`
   - **Description:** "Modular protocol bridge: XpressNet ↔ Ecos via ESP8266"
   - **Visibility:** Public (open source) or Private (personal)
   - **Initialize:** Do NOT add README (we have our own)
   - **License:** MIT or Apache 2.0 (recommended for open source)
   - Click **Create repository**

3. **Copy the repository URL**
   - Should look like: `https://github.com/yourname/xpressnet-ecos-bridge.git`

## Step 5: Connect Local Repo to GitHub

```powershell
cd "E:\Claude\Bridge"

# Add remote repository
git remote add origin https://github.com/yourname/xpressnet-ecos-bridge.git

# Rename default branch to 'main' (GitHub standard)
git branch -M main

# Push to GitHub
git push -u origin main

# Verify
git remote -v
```

You should see:
```
origin  https://github.com/yourname/xpressnet-ecos-bridge.git (fetch)
origin  https://github.com/yourname/xpressnet-ecos-bridge.git (push)
```

## Step 6: Verify on GitHub

1. Go to your repository URL: `https://github.com/yourname/xpressnet-ecos-bridge`
2. Verify files are present:
   - README.md visible
   - src/ folder with files
   - docs/ folder
   - .gitignore applied

## Step 7: Ongoing Workflow

### After Making Changes

```powershell
cd "E:\Claude\Bridge"

# See what changed
git status

# Stage changes
git add .

# Commit with message
git commit -m "Add XpressNet interface skeleton"

# Push to GitHub
git push
```

### Common Git Commands

```powershell
# View commit history
git log --oneline

# See differences
git diff

# Undo uncommitted changes
git checkout -- filename.h

# Undo last commit (before push)
git reset HEAD~1

# Create a branch for new feature
git checkout -b feature/loconet-support
git push -u origin feature/loconet-support

# Merge branch (via GitHub Pull Request recommended)
git checkout main
git merge feature/loconet-support
git push
```

## Step 8: Add .gitignore Rules

The `.gitignore` file is already included. It automatically ignores:
- Arduino build artifacts
- IDE files (.vscode, .idea)
- macOS files (.DS_Store)
- Windows files (Thumbs.db)

No additional configuration needed.

## File Structure After Setup

```
E:\Claude\Bridge\
├── .git/                          ← Git metadata (auto-created)
├── .gitignore                     ← Git ignore rules
├── README.md                      ← Project overview
├── docs/
│   ├── 01_DESIGN_DOCUMENT.md
│   ├── 02_PROTOCOL_XPRESSNET.md  ← (To be created)
│   ├── 02_PROTOCOL_ECOS.md       ← (To be created)
│   └── 03_HARDWARE_WIRING.md     ← (To be created)
├── src/
│   ├── main.ino
│   ├── config.h                   ← User configuration
│   ├── definitions.h
│   ├── state_engine.h
│   ├── command_router.h
│   ├── protocols/
│   │   ├── xpressnet/
│   │   │   └── xpressnet_interface.h
│   │   ├── ecos/
│   │   │   └── ecos_interface.h
│   │   ├── loconet/
│   │   └── z21lan/
│   ├── display/
│   ├── interfaces/
│   │   └── interface_base.h
│   └── utils/
├── libraries/
│   └── README.md                  ← Instructions for external libraries
└── tests/
    └── (Unit tests to be added)
```

## Next Steps After Repository Setup

1. **Add Collaborators** (if working with others)
   - Go to Settings → Collaborators → Add people

2. **Enable GitHub Pages** (for documentation)
   - Settings → Pages → Build from docs/ folder
   - Documentation auto-published to yourname.github.io/xpressnet-ecos-bridge

3. **Add Issues/Project Board**
   - Use for tracking implementation tasks
   - Create issues for Phase 2, Phase 3, Phase 4 tasks

4. **Add Releases**
   - Tag milestones: v0.1-alpha, v0.1-beta, v1.0
   - Create release notes for each version

## Troubleshooting

### "fatal: not a git repository"
- Make sure you're in the `E:\Claude\Bridge\` folder
- Run `git init` if .git folder doesn't exist

### "error: failed to push"
- Check internet connection
- Verify repository URL: `git remote -v`
- Check GitHub credentials (token/SSH key)

### "fatal: refusing to merge unrelated histories"
- You tried to merge repo with different history
- Use: `git pull --allow-unrelated-histories`
- Or start fresh with new clone

### Files not appearing in GitHub
- Check `.gitignore` isn't blocking them
- Verify they're tracked: `git ls-files`
- Force add if needed: `git add -f filename`

## Using GitHub Desktop (Graphical Alternative)

If command line is intimidating:

1. Download GitHub Desktop: https://desktop.github.com/
2. Click "File" → "Add Local Repository"
3. Select `E:\Claude\Bridge\`
4. It will recognize existing git repo
5. Use GUI to commit and push instead of command line

---

## Recommended .gitignore Additions

If you want to exclude local configuration:

```gitignore
# Local development
src/config.local.h
.env
.env.local

# Arduino IDE specific
*.ino.bak
*.ino.cpp
```

Edit `.gitignore` and commit the change.

---

**Repository is now ready for implementation!**

Next: Begin Phase 2 - Implement skeleton code
