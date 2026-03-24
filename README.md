# ImageTool (Mac / Windows)

Dear ImGui + GLFW + OpenGL + stb で作る、クロスプラットフォーム画像リサイズツールです。

- UI: C++ (`app/`)
- 画像処理コア: C (`core/`)
- ビルド: CMake Presets
- 対応OS: Windows / macOS

## 主な機能

- 画像ファイル / ディレクトリ読み込み
- 画像一覧表示とプレビュー
- 幅・高さを指定して単体リサイズ保存
- 幅・高さを指定して一括リサイズ保存
- 保存形式の選択 (`PNG / JPG / PDF`)
- 保存先フォルダのダイアログ選択

読込対応形式:

- `PNG`
- `JPG / JPEG`
- `PDF` (`macOS` では読込対応)
- `HEIC / HEIF` (`macOS` では読込対応、保存は既存の `PNG / JPG / PDF` へ変換)

保存ファイル名は `元ファイル名_幅x高さ.<拡張子>` です。

## ディレクトリ構成

```text
ImageTool/
├─ CMakeLists.txt
├─ CMakePresets.json
├─ README.md
├─ .github/
│  └─ workflows/
│     └─ ci.yml
├─ app/
│  └─ main.cpp
└─ core/
   ├─ image_core.c
   └─ image_core.h
```

## 前提

- CMake 3.24+
- C++17対応コンパイラ
- Git (依存取得に使用)

依存ライブラリ（GLFW / Dear ImGui / stb）は `FetchContent` で configure 時に自動取得します。

## ビルド

### macOS

```bash
cmake --preset macos
cmake --build --preset build-macos
```

実行ファイル: `build/macos/ImageTool`

### Windows (Visual Studio 2022)

```powershell
cmake --preset win64
cmake --build --preset build-win64
```

実行ファイル例: `build\win64\Release\ImageTool.exe`

## VS Code / Visual Studio

- VS Code: CMake Tools で `macos` / `win64` プリセットを選択して Build
- Visual Studio: リポジトリを `Open Folder` で開いて `win64` を選択して Build

## GitHub での開始手順

```bash
git init
git add .
git commit -m "Initial scaffold: cross-platform ImageTool"
git branch -M main
git remote add origin <your-repo-url>
git push -u origin main
```

## CI

`.github/workflows/ci.yml` で Windows / macOS のビルドを実行します。

## 注意

- 初回 configure 時に依存ライブラリをネットワーク取得します。
- 画像の向き（EXIF Orientation）はこの初期版では補正していません。
- 巨大画像はメモリ使用量が増えるため、段階的な最適化が必要です。
- `PDF` 保存は、リサイズ後画像を1ページPDFとして書き出します。
- `PDF` 読込は現時点では `macOS` 実装を優先しており、Windows では今後PDFレンダラ統合が必要です。
- `HEIC / HEIF` 読込も現時点では `macOS` 実装です。Windows では今後デコーダ統合が必要です。
