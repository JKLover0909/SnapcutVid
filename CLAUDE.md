# CLAUDE.md

Guidance for Claude Code (and similar agents) working in this repository.

## What this repo is

Windows GUI app (Win32 API, không phải console) để trích xuất frame từ video, dùng OpenCV. Build bằng CMake + MSVC.

```
src/video_frame_extractor.cpp   # toàn bộ app: GUI Win32 + OpenCV videoio/imgcodecs
CmakeLists.txt                  # trỏ OpenCV_DIR vào third_party/opencv/build_minimal
third_party/opencv/              # OpenCV vendor trong repo (BSD-3-Clause) — xem license riêng
  sources/                       # mã nguồn OpenCV đầy đủ
  build_minimal/                 # ⚠️ xem mục dưới — KHÔNG dọn khi chưa hỏi
```

## ⚠️ third_party/opencv/build_minimal/ — không tự dọn

Thư mục này **vừa chứa output build thật sự cần thiết** (`bin/Release/*.dll`, `lib/`, config CMake để `find_package(OpenCV)` hoạt động — `CmakeLists.txt` phụ thuộc trực tiếp vào đây để copy DLL sau khi build) **vừa chứa rác trung gian** (`*.dir/` object files, `CMakeFiles/`, `*.vcxproj` — sản phẩm phụ của MSBuild, không cần cho việc dùng lại thư viện đã build).

Tách đúng phần nào an toàn để xóa đòi hỏi hiểu rõ layout output của CMake/OpenCV build — **không tự ý xóa/untrack bất kỳ phần nào trong `build_minimal/` khi chưa hỏi người dùng**, vì lỡ tay xóa nhầm phần `bin/`/`lib`/config sẽ làm hỏng build hiện tại của project.

## Ràng buộc khác

- `build_log.txt` (log MSBuild, lộ path máy cá nhân) đã untrack + thêm vào `.gitignore` — không commit lại log build.
- Đây là project C++/CMake/MSVC thuần Windows (dùng `<windows.h>`, `comctl32`) — không build được trên môi trường agent (không có MSVC/OpenCV build sẵn). Chỉ kiểm tra syntax bằng mắt, không thử compile.

## Kiểm thử an toàn

Không có toolchain MSVC + OpenCV build trong môi trường agent — không chạy `cmake`/`build`. Chỉ đọc/sửa code nguồn.
