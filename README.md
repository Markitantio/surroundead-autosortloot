# AutoSortLoot

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![SurrounDead](https://img.shields.io/badge/SurrounDead-v0.8.0-blue)]()
[![UE4SS](https://img.shields.io/badge/UE4SS-3.0.1+-green)]()

One-key automatic inventory sorting mod for **SurrounDead**.

Press **F9** and loot goes to the right specialized containers:
- 🩹 Medical → MedBag
- 🔫 Ammo, attachments, weapons → AmmoTin / WeaponsCase
- 🍖 Food & drinks → LunchBox
- 🛠 Materials & tools → Toolbox
- 💰 Currency & keycards → Wallet / Safe / Briefcase

## ✨ Features
- Type-based sorting rules
- Multi-target fallback
- Fully configurable via `AutoSortLoot.ini`
- Works with inventory closed
- No memory hacks — uses the game's native `EventOnInventoryAction`

## 📦 Installation
[См. подробнее в docs/INSTALL.md](docs/INSTALL.md)

## ⚙️ Configuration
[См. описание всех опций в docs/CONFIG.md](docs/CONFIG.md)

## 🔧 Building from source
Требуется MSVC 14.51+, CMake 3.20+, Ninja
./build.ps1

DLL появится в `Output_ninja/MyCPPMods/AutoSortLoot/AutoSortLoot.dll`.

## 🧩 Compatibility
- ✅ Cosmetic, spawn, gameplay mods
- ⚠️ Inventory overhauls — могут конфликтовать
- ❌ Моды с хуком на `EventOnInventoryAction`

## 🐛 Report bugs
Прикладывай `UE4SS.log` и шаги воспроизведения.

## 📜 License
MIT — см. [LICENSE](LICENSE)

## 🙏 Credits
- Built with [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS)
- Special thanks to modding community