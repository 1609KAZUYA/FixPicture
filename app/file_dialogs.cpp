#include "file_dialogs.h"

#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

#if defined(_WIN32)
#  define NOMINMAX
#  include <windows.h>
#  include <commdlg.h>
#  include <objbase.h>
#  include <shlobj.h>
#endif

static std::string trim_copy(const std::string& text) {
  size_t start = 0;
  while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
    ++start;
  }

  size_t end = text.size();
  while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }

  return text.substr(start, end - start);
}

#if defined(_WIN32)
static std::string wide_to_utf8(const wchar_t* value) {
  if (!value || value[0] == L'\0') {
    return std::string();
  }

  const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) {
    return std::string();
  }

  std::string result(static_cast<size_t>(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
  return result;
}

bool prompt_open_image_files(std::vector<fs::path>& out_paths) {
  static const wchar_t kFilter[] =
      L"Image Files\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.pdf\0All Files\0*.*\0";
  wchar_t buffer[32768] = L"";
  OPENFILENAMEW dialog = {};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = nullptr;
  dialog.lpstrFilter = kFilter;
  dialog.lpstrFile = buffer;
  dialog.nMaxFile = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
  dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
  dialog.lpstrTitle = L"Select image files";

  if (!GetOpenFileNameW(&dialog)) {
    return false;
  }

  out_paths.clear();
  const wchar_t* current = buffer;
  const std::wstring first_entry = current;
  current += first_entry.size() + 1;

  if (*current == L'\0') {
    out_paths.emplace_back(fs::path(wide_to_utf8(first_entry.c_str())));
    return !out_paths.empty();
  }

  const fs::path base_dir = fs::path(wide_to_utf8(first_entry.c_str()));
  while (*current != L'\0') {
    out_paths.emplace_back(base_dir / wide_to_utf8(current));
    current += std::wcslen(current) + 1;
  }

  return !out_paths.empty();
}

bool prompt_open_image_file(fs::path& out_path) {
  std::vector<fs::path> out_paths;
  if (!prompt_open_image_files(out_paths) || out_paths.empty()) {
    return false;
  }

  out_path = out_paths.front();
  return true;
}

static bool prompt_select_directory(fs::path& out_path, const wchar_t* title) {
  HRESULT init_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  const bool should_uninit = SUCCEEDED(init_result);

  BROWSEINFOW dialog = {};
  dialog.lpszTitle = title;
  dialog.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;

  PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&dialog);
  if (!item) {
    if (should_uninit) {
      CoUninitialize();
    }
    return false;
  }

  wchar_t buffer[MAX_PATH] = L"";
  const BOOL ok = SHGetPathFromIDListW(item, buffer);
  CoTaskMemFree(item);

  if (should_uninit) {
    CoUninitialize();
  }

  if (!ok) {
    return false;
  }

  out_path = fs::path(wide_to_utf8(buffer));
  return !out_path.empty();
}

bool prompt_select_image_directory(fs::path& out_path) {
  return prompt_select_directory(out_path, L"Select an image folder");
}

bool prompt_select_output_directory(fs::path& out_path) {
  return prompt_select_directory(out_path, L"Select output folder");
}
#elif defined(__APPLE__)
static std::string run_command_and_capture(const char* command) {
  if (!command) {
    return std::string();
  }

  FILE* pipe = popen(command, "r");
  if (!pipe) {
    return std::string();
  }

  std::string output;
  char buffer[512];
  while (std::fgets(buffer, static_cast<int>(sizeof(buffer)), pipe) != nullptr) {
    output += buffer;
  }
  pclose(pipe);
  return trim_copy(output);
}

bool prompt_open_image_files(std::vector<fs::path>& out_paths) {
  const std::string result = run_command_and_capture(
      "osascript <<'APPLESCRIPT'\n"
      "set chosenFiles to choose file with prompt \"Select image files\" multiple selections allowed true\n"
      "set outputText to \"\"\n"
      "repeat with oneFile in chosenFiles\n"
      "  set outputText to outputText & POSIX path of oneFile & linefeed\n"
      "end repeat\n"
      "return outputText\n"
      "APPLESCRIPT");
  if (result.empty()) {
    return false;
  }

  out_paths.clear();
  size_t start = 0;
  while (start < result.size()) {
    const size_t end = result.find('\n', start);
    const std::string line = trim_copy(result.substr(start, end == std::string::npos ? std::string::npos : end - start));
    if (!line.empty()) {
      out_paths.emplace_back(fs::path(line));
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }

  return !out_paths.empty();
}

bool prompt_open_image_file(fs::path& out_path) {
  std::vector<fs::path> out_paths;
  if (!prompt_open_image_files(out_paths) || out_paths.empty()) {
    return false;
  }

  out_path = out_paths.front();
  return true;
}

static bool prompt_select_directory(fs::path& out_path, const char* title) {
  const std::string prompt = title ? title : "Select a folder";
  const std::string command =
      "osascript -e 'POSIX path of (choose folder with prompt \"" + prompt + "\")'";
  const std::string result = run_command_and_capture(command.c_str());
  if (result.empty()) {
    return false;
  }

  out_path = fs::path(result);
  return true;
}

bool prompt_select_image_directory(fs::path& out_path) {
  return prompt_select_directory(out_path, "Select an image folder");
}

bool prompt_select_output_directory(fs::path& out_path) {
  return prompt_select_directory(out_path, "Select output folder");
}
#else
bool prompt_open_image_file(fs::path& out_path) {
  (void)out_path;
  return false;
}

bool prompt_open_image_files(std::vector<fs::path>& out_paths) {
  (void)out_paths;
  return false;
}

bool prompt_select_image_directory(fs::path& out_path) {
  (void)out_path;
  return false;
}

bool prompt_select_output_directory(fs::path& out_path) {
  (void)out_path;
  return false;
}
#endif
