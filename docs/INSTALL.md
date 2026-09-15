## 📄 `docs/INSTALL.md`

```markdown
# Installation Guide — AutoSortLoot

This guide walks you through installing **AutoSortLoot** on **SurrounDead v0.8.0** with **UE4SS**.

---

## 📋 Prerequisites

Before installing the mod, make sure you have:

| Requirement | Version | Notes |
|---|---|---|
| **SurrounDead** | v0.8.0 | Older/newer versions untested |
| **UE4SS** | v3.0.1 Beta or newer | C++ mod support required |
| **Windows** | 10 or 11 (x64) | No Linux/Steam Deck support |
| **Disk space** | ~1 MB for the mod | + UE4SS itself (~50 MB) |

> ⚠️ **If you don't have UE4SS installed yet**, follow its official guide first:
> https://github.com/UE4SS-RE/RE-UE4SS
> Come back here once UE4SS works and mods load (check `UE4SS.log` for `Starting C++ mod 'SomeMod'`).

---

## 🎯 Step 1 — Locate your game folder

1. Open **Steam** → right-click **SurrounDead** → **Manage** → **Browse local files**.
   *(Epic Games / GOG — navigate manually to the install folder.)*

2. You should see something like:
   ```
   ...\SurrounDead\SurrounDead\Binaries\Win64\
   ```
   Inside, there should be a folder called `ue4ss`:
   ```
   ...\SurrounDead\Binaries\Win64\ue4ss\
   ├── Mods\
   ├── UE4SS.dll
   ├── UE4SS-settings.ini
   └── UE4SS.log
   ```

**If `ue4ss` is missing** → install UE4SS first (see Prerequisites).

---

## 📥 Step 2 — Download the mod

1. Go to the **Releases** page of this repository:
   `https://github.com/YOUR_USERNAME/surroundead-autosortloot/releases/latest`

2. Download `AutoSortLoot-vX.Y.Z.zip` from the **Assets** section.

3. **Extract** the archive. You'll get a folder structure:
   ```
   AutoSortLoot/
   ├── dlls/
   │   └── main.dll
   ├── AutoSortLoot.ini
   ├── README.txt
   └── LICENSE
   ```

---

## 📂 Step 3 — Copy files into the game

1. Navigate to your mods folder:
   ```
   ...\SurrounDead\Binaries\Win64\ue4ss\Mods\
   ```

2. Copy the entire `AutoSortLoot` folder from the archive into it.

   Final path **must** look like this:
   ```
   ...\ue4ss\Mods\AutoSortLoot\dlls\main.dll
   ...\ue4ss\Mods\AutoSortLoot\AutoSortLoot.ini   ← optional (auto-created)
   ```

   ⚠️ **Common mistake:** Don't put the DLL directly in `Mods\AutoSortLoot\` — it must be inside `dlls\`.
   The subfolder `dlls` is required by UE4SS's C++ mod loader.

---

## 🔧 Step 4 — Register the mod

UE4SS loads C++ mods by reading `mods.txt`. You must add AutoSortLoot there.

1. Open this file in a text editor (Notepad++ or VSCode recommended):
   ```
   ...\ue4ss\Mods\mods.txt
   ```

2. Find a line that starts with `AutoSortLoot`. It may or may not already exist.

   **If it exists**, make sure it's enabled (`: 1`):
   ```
   AutoSortLoot : 1
   ```

   **If it's missing**, add it anywhere in the file. Order doesn't matter for this mod, but a common pattern is to put custom mods at the bottom:
   ```
   ; ... existing entries ...
   AutoSortLoot : 1
   ```

   **If it says `: 0`**, change to `: 1`:
   ```
   AutoSortLoot : 0   ← disabled, mod won't load
   AutoSortLoot : 1   ← enabled
   ```

3. Save and close the file.

---

## 🎮 Step 5 — Launch and verify

1. **Start SurrounDead.**

2. Open the log file in a text editor:
   ```
   ...\ue4ss\UE4SS.log
   ```

3. Search (`Ctrl+F`) for `AutoSortLoot`. You should see:

   ```
   Starting C++ mod 'AutoSortLoot'
   [AutoSortLoot]: Init.
   [AutoSortLoot] Install()
   [AutoSortLoot] config path: ...\AutoSortLoot\AutoSortLoot.ini
   [AutoSortLoot] config loaded: hotkey=0x78, rules=14, verbose=0
   [AutoSortLoot] PLSF hook установлен.
   ```

   `hotkey=0x78` = `VK_F9` (default). `rules=14` = количество правил из INI.

4. **If `AutoSortLoot` is missing from the log entirely** → mod wasn't loaded. See **Troubleshooting** below.

5. **In-game:** press **F9** with items scattered in your backpack and containers equipped/opened once. Everything should shuffle into the right places.

---

## ⚙️ Step 6 — First-time configuration

On first launch, the mod creates a config file next to the DLL:

```
...\ue4ss\Mods\AutoSortLoot\AutoSortLoot.ini
```

Open it with a text editor. All options are documented in Russian inside the file. See **[CONFIG.md](CONFIG.md)** for full reference.

**Key options:**
- `Hotkey=F9` — change the sorting key
- `VerboseLogs=false` — set to `true` for detailed logs
- `UnknownItemAction=Skip` — `Skip` (leave item) or `MoveToRoot` (send to root container)
- `[Mapping]` — per-item-type rules

> ⚠️ **Restart the game after any INI change.** The config is only read on launch.

---

## 🔍 Troubleshooting

### Mod doesn't appear in the log

**Symptom:** `Starting C++ mod 'AutoSortLoot'` is missing from `UE4SS.log`.

**Checks:**
1. Is `mods.txt` correctly edited? Look for `AutoSortLoot : 1` — the space before `:` matters in some UE4SS builds.
2. Is the path exactly `Mods\AutoSortLoot\dlls\main.dll`? (Not `Mods\AutoSortLoot\main.dll`.)
3. Is UE4SS itself installed properly? Try another C++ mod — if it also fails, the problem is UE4SS, not this mod.
4. Check for typos in the folder name: **`AutoSortLoot`** (case-sensitive on some systems).

### `hotkey=0x0` in log

**Symptom:** `[AutoSortLoot] config loaded: hotkey=0x0, rules=N`

**Cause:** The `Hotkey=` value in INI isn't recognized.

**Fix:** Use one of the supported formats:
- `F1` .. `F12`
- `0` .. `9`
- `A` .. `Z` (single uppercase letter)
- `INSERT`, `HOME`, `END`, `DELETE`, `PAGEUP`, `PAGEDOWN`, `SPACE`, `TAB`

Example: `Hotkey=F9`

### `rules=0` in log

**Symptom:** `config loaded: hotkey=0x78, rules=0`

**Cause:** The `[Mapping]` section is missing, empty, or malformed.

**Fix:** Ensure entries are written like:
```ini
[Mapping]
MedicalConsumable=MedBag
Ammunition=AmmoTin,WeaponsCase
```
No spaces around `=`, no quotes, one rule per line.

### Pressing F9 does nothing

**Possible causes:**
1. **No containers** — mod has nothing to sort into. Equip your backpack, MedBag, WeaponsCase, AmmoTin, etc. and open each at least once this session.
2. **Hotkey conflict** — another mod already uses F9. Change `Hotkey=F9` to e.g. `Hotkey=F11` in INI, restart.
3. **Nothing to sort** — all items are already in correct containers. Check the log for:
   ```
   [Sort] план: 0 (skip: ...)
   ```
   If `plan: 0`, the mod found no moves to make.
4. **Verbose logs disabled** — set `VerboseLogs=true` in INI, restart, look for `[Sort]` blocks.

### Items don't move even though log says "OK"

**Symptom:** Log shows `MOVE 'X' -> Y slot=Z` / `OK`, but visually item is still in backpack.

**Cause:** The target container's UI widget isn't loaded. This can happen if you never equipped/opened that container this session.

**Fix:**
1. Equip the target container (`MedBag`, `WeaponsCase`), or open it once.
2. Try sorting again.

### Crash to desktop when pressing F9

Very rare. If it happens:

1. Note the last lines of `UE4SS.log`.
2. Open an **Issue** on GitHub with:
   - The full `UE4SS.log` (or the tail of it after crash)
   - Your `mods.txt`
   - Your `AutoSortLoot.ini`
   - In-game context (what was open, what you were doing)
3. As a temporary workaround: set `VerboseLogs=false` and try again.

### "Some items are missing" after sort

**This mod never deletes items.** If something seems missing, check:
- It may have been moved to a container you didn't look in (AmmoTin → WeaponsCase fallback, for example).
- Weapons with attached magazines: the magazine moves with the weapon. This is normal.
- Verify with a save reload — the game's item state is persisted.

If you're sure an item disappeared, **stop playing**, don't save, report with logs.

---

## 🔄 Updating the mod

1. Delete the old `dlls\main.dll` in `...\ue4ss\Mods\AutoSortLoot\dlls\`.
2. Copy the new `main.dll` from the latest release.
3. **Keep your `AutoSortLoot.ini`** — the mod will not touch it if it exists.
4. Restart the game.

> 💡 Since `v1.0.0`, the config format is **backward-compatible**. New options get added with default values if your INI is missing them.

---

## 🗑️ Uninstallation

1. Remove the folder:
   ```
   ...\ue4ss\Mods\AutoSortLoot\
   ```
2. (Optional) Remove the line `AutoSortLoot : 1` from `mods.txt`.
3. Restart the game.

No registry keys, no leftover files elsewhere.

---

## 🎁 Bonus — sharing your config

If you've tuned your `AutoSortLoot.ini` for a specific playstyle (e.g., "milsim hardcore"), feel free to share it on the NexusMods forum or in a GitHub Gist. Include the INI and a note about what changed.

---

## 🙏 Thanks for using AutoSortLoot!

If you find bugs or want new features — open an issue:
`https://github.com/YOUR_USERNAME/surroundead-autosortloot/issues`
```

---