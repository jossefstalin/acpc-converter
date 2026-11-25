#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include "viewer.h"

/* GDI+ flat API declarations. System GDI+ handles PNG/JPEG on Windows. */
typedef void GpImage;
typedef struct { unsigned version; void *debug; BOOL no_thread, no_codecs; } GpStartup;
typedef struct { int X, Y, Width, Height; } GpRect;
typedef struct {
    unsigned width, height;
    int stride, format;
    void *scan0;
    ULONG_PTR reserved;
} GpBits;
int WINAPI GdiplusStartup(ULONG_PTR *, const GpStartup *, void *);
void WINAPI GdiplusShutdown(ULONG_PTR);
int WINAPI GdipLoadImageFromFile(const wchar_t *, GpImage **);
int WINAPI GdipGetImageWidth(GpImage *, unsigned *);
int WINAPI GdipGetImageHeight(GpImage *, unsigned *);
int WINAPI GdipBitmapLockBits(GpImage *, const GpRect *, unsigned, int, GpBits *);
int WINAPI GdipBitmapUnlockBits(GpImage *, GpBits *);
int WINAPI GdipDisposeImage(GpImage *);

#define APP_TITLE L"Air Conflicts Workshop ｜ 模型查看器"
#define APP_CLASS L"AirConflictsNativeViewer"
#define READ_CHARS 4096
#define PREVIEW_LIMIT 1920
#define PLAY_TIMER 1

enum {
    CMD_OPEN = 100, CMD_DIFFUSE, CMD_FIT, CMD_MODE, CMD_LOD, CMD_MATERIAL, CMD_DEMO,
    CMD_PREVIOUS, CMD_PLAY, CMD_NEXT, CMD_SCRUB, CMD_BMP, CMD_OBJ, CMD_EXIT,
    CMD_OBJ_Y, CMD_HELP
};

static const wchar_t mode_labels[][16] = {
    L"颜色贴图", L"灰模", L"线框", L"颜色 UV 棋盘", L"法线", L"光照贴图", L"光照 UV 棋盘"
};

typedef struct {
    HWND window, open, diffuse, fit, mode_select, lod_select, material_select, demo;
    HWND previous, play, next, scrub, stat, help;
    HINSTANCE instance;
    HFONT font;
    ULONG_PTR gdiplus;
    int dpi, dragging, last_x, last_y, playing, obj_z_up, dirty;
    unsigned lod;
    AvMode mode;
    AvModel model;
    AvView view;
    AvSurface surface;
    RECT viewport;
} Application;

static Application app;

static int scale_ui(int pixels) { return (pixels * app.dpi + 48) / 96; }

static int to_utf8(char *out, const wchar_t *text, int size) {
    return WideCharToMultiByte(CP_UTF8, 0, text, -1, out, size, NULL, NULL) > 0;
}

static int to_wide(wchar_t *out, const char *text, int size) {
    int ok = MultiByteToWideChar(CP_UTF8, 0, text, -1, out, size) > 0;
    if (!ok) out[0] = 0;
    return ok;
}

static int full_path_utf8(char out[AV_PATH], const wchar_t *path) {
    wchar_t absolute[READ_CHARS];
    DWORD count = GetFullPathNameW(path, READ_CHARS, absolute, NULL);
    return count > 0 && count < READ_CHARS && to_utf8(out, absolute, AV_PATH);
}

FILE *av_fopen(const char *path, const char *mode) {
    wchar_t filename[AV_PATH], wide_mode[8];
    if (!to_wide(filename, path, AV_PATH) || !to_wide(wide_mode, mode, 8)) return NULL;
    return _wfopen(filename, wide_mode);
}

int av_platform_image(const char *path, AvImage *image) {
    if (!app.gdiplus) return 0;
    wchar_t name[AV_PATH];
    GpImage *loaded = NULL;
    if (!to_wide(name, path, AV_PATH) || GdipLoadImageFromFile(name, &loaded) || !loaded) return 0;
    unsigned width = 0, height = 0;
    int ok = 0;
    if (GdipGetImageWidth(loaded, &width) == 0 && GdipGetImageHeight(loaded, &height) == 0
            && width && height && width <= 8192 && height <= 8192 && (uint64_t)width * height <= 16777216) {
        GpRect bounds = {0, 0, (int)width, (int)height};
        GpBits locked = {0};
        if (GdipBitmapLockBits(loaded, &bounds, 1, 0x0026200a, &locked) == 0) {
            uint32_t *pixels = malloc((size_t)width * height * sizeof(uint32_t));
            if (pixels && locked.scan0) {
                for (unsigned y = 0; y < height; ++y) {
                    const void *row = (const unsigned char *)locked.scan0 + (ptrdiff_t)y * locked.stride;
                    memcpy(&pixels[(size_t)y * width], row, (size_t)width * 4);
                }
                *image = (AvImage){(int)width, (int)height, pixels};
                ok = 1;
            } else free(pixels);
            GdipBitmapUnlockBits(loaded, &locked);
        }
    }
    GdipDisposeImage(loaded);
    return ok;
}

static void append_wide(wchar_t *out, size_t capacity, const wchar_t *suffix) {
    size_t cursor = 0;
    while (cursor < capacity && out[cursor]) ++cursor;
    if (cursor == capacity) return;
    while (*suffix && cursor + 1 < capacity) out[cursor++] = *suffix++;
    out[cursor] = 0;
}

static void append_count(wchar_t *out, size_t capacity, size_t count) {
    wchar_t digits[24];
    size_t cursor = sizeof(digits)/sizeof(*digits) - 1;
    digits[cursor] = 0;
    do { digits[--cursor] = (wchar_t)(L'0' + count % 10); count /= 10; } while (count && cursor);
    append_wide(out, capacity, digits + cursor);
}

static void set_model_title(void) {
    wchar_t text[640] = L"";
    if (!to_wide(text, av_filename(app.model.path), 640) || !text[0]) {
        append_wide(text, 640, L"内置示例");
    }
    append_wide(text, 640, L"  —  " APP_TITLE);
    SetWindowTextW(app.window, text);
}

static void set_stat(void) {
    const AvMesh *mesh = &app.model.lods[app.lod];
    wchar_t text[480] = L"";
    append_wide(text, 480, L"LOD ");
    append_count(text, 480, app.lod);
    append_wide(text, 480, L"   ·   三角面 ");
    append_count(text, 480, mesh->triangle_count);
    append_wide(text, 480, L"   ·   显示顶点 ");
    append_count(text, 480, mesh->vertex_count);
    append_wide(text, 480, mesh->has_light_uv ? L"   ·   UV0 / UV1" : L"   ·   UV0");
    append_wide(text, 480, L"   ·   帧 ");
    append_count(text, 480, app.model.frame + 1);
    append_wide(text, 480, L" / ");
    append_count(text, 480, app.model.frame_count);
    if (app.model.missing_textures) {
        append_wide(text, 480, L"   ·   贴图未找到 ");
        append_count(text, 480, (size_t)app.model.missing_textures);
    }
    if (!mesh->has_light_uv && (app.mode == AV_LIGHT || app.mode == AV_UV1)) {
        append_wide(text, 480, L"   ·   当前 LOD 没有 UV1");
    }
    SetWindowTextW(app.stat, text);
}

static void redraw(void) {
    app.dirty = 1;
    InvalidateRect(app.window, &app.viewport, FALSE);
}

static void reset_view(void) {
    av_view_reset(&app.view);
    redraw();
}

static HWND control(const wchar_t *class_name, const wchar_t *label, DWORD style, unsigned id) {
    HWND window = CreateWindowExW(0, class_name, label, style | WS_CHILD | WS_VISIBLE, 0, 0, 100, 28,
        app.window, (HMENU)(uintptr_t)id, app.instance, NULL);
    if (window) SendMessageW(window, WM_SETFONT, (WPARAM)app.font, TRUE);
    return window;
}

static void layout(void) {
    RECT client;
    GetClientRect(app.window, &client);
    const int pad = scale_ui(12), gap = scale_ui(7), h = scale_ui(28);
    const int y0 = scale_ui(10), y1 = scale_ui(46);
    const int width = (int)client.right, height = (int)client.bottom;
    int x = pad;
    HWND top[] = {app.open, app.diffuse, app.fit, app.mode_select, app.lod_select, app.material_select, app.demo};
    const int widths[] = {93, 90, 90, 145, 74, 145, 90};
    for (unsigned i = 0; i < sizeof(top)/sizeof(*top); ++i) {
        const int is_combo = i >= 3 && i <= 5;
        MoveWindow(top[i], x, y0, scale_ui(widths[i]), is_combo ? scale_ui(280) : h, TRUE);
        x += scale_ui(widths[i]) + gap;
    }
    x = pad;
    HWND buttons[] = {app.previous, app.play, app.next};
    const int button_width[] = {70, 76, 70};
    for (unsigned i = 0; i < 3; ++i) {
        MoveWindow(buttons[i], x, y1, scale_ui(button_width[i]), h, TRUE);
        x += scale_ui(button_width[i]) + gap;
    }
    if (width > x + pad) MoveWindow(app.scrub, x, y1, width - pad - x, h, TRUE);
    int view_top = scale_ui(84), view_bottom = height - scale_ui(58);
    if (view_bottom < view_top) view_bottom = view_top;
    app.viewport = (RECT){0, view_top, width, view_bottom};
    MoveWindow(app.stat, pad, view_bottom + scale_ui(6), width - pad*2, scale_ui(19), TRUE);
    MoveWindow(app.help, pad, view_bottom + scale_ui(29), width - pad*2, scale_ui(20), TRUE);
    app.dirty = 1;
    InvalidateRect(app.window, NULL, FALSE);
}

static int controls_init(void) {
    app.font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    app.open = control(L"BUTTON", L"打开模型…", WS_TABSTOP, CMD_OPEN);
    app.diffuse = control(L"BUTTON", L"指定贴图…", WS_TABSTOP, CMD_DIFFUSE);
    app.fit = control(L"BUTTON", L"复位视角", WS_TABSTOP, CMD_FIT);
    app.mode_select = control(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, CMD_MODE);
    app.lod_select = control(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, CMD_LOD);
    app.material_select = control(L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, CMD_MATERIAL);
    app.demo = control(L"BUTTON", L"内置示例", WS_TABSTOP, CMD_DEMO);
    app.previous = control(L"BUTTON", L"上一帧", WS_TABSTOP, CMD_PREVIOUS);
    app.play = control(L"BUTTON", L"播放", WS_TABSTOP, CMD_PLAY);
    app.next = control(L"BUTTON", L"下一帧", WS_TABSTOP, CMD_NEXT);
    app.scrub = control(L"msctls_trackbar32", L"", WS_TABSTOP, CMD_SCRUB);
    app.stat = control(L"STATIC", L"", SS_LEFTNOWORDWRAP, 0);
    app.help = control(L"STATIC", L"左键旋转  ·  右键平移  ·  滚轮缩放  ·  双击复位  ·  可把模型拖入窗口", SS_LEFTNOWORDWRAP, 0);
    if (!app.open || !app.diffuse || !app.fit || !app.mode_select || !app.lod_select || !app.material_select
            || !app.demo || !app.previous || !app.play || !app.next || !app.scrub || !app.stat || !app.help) return 0;
    for (unsigned i = 0; i < sizeof(mode_labels)/sizeof(*mode_labels); ++i) {
        SendMessageW(app.mode_select, CB_ADDSTRING, 0, (LPARAM)mode_labels[i]);
    }
    SendMessageW(app.mode_select, CB_SETCURSEL, AV_TEXTURED, 0);
    return 1;
}

static void set_playing(int yes) {
    app.playing = yes && app.model.frame_count > 1;
    if (app.playing) SetTimer(app.window, PLAY_TIMER, 110, NULL);
    else KillTimer(app.window, PLAY_TIMER);
    SetWindowTextW(app.play, app.playing ? L"暂停" : L"播放");
}

static void refresh_model_controls(void) {
    app.lod = 0;
    SendMessageW(app.lod_select, CB_RESETCONTENT, 0, 0);
    for (unsigned lod = 0; lod < app.model.lod_count; ++lod) {
        wchar_t text[30] = L"LOD ";
        append_count(text, 30, lod);
        SendMessageW(app.lod_select, CB_ADDSTRING, 0, (LPARAM)text);
    }
    SendMessageW(app.lod_select, CB_SETCURSEL, 0, 0);
    EnableWindow(app.lod_select, app.model.lod_count > 1);
    SendMessageW(app.material_select, CB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < app.model.material_count; ++i) {
        wchar_t name[128];
        if (!to_wide(name, app.model.materials[i].name, 128) || !name[0]) {
            append_wide(name, 128, L"材质 ");
            append_count(name, 128, i + 1);
        }
        SendMessageW(app.material_select, CB_ADDSTRING, 0, (LPARAM)name);
    }
    SendMessageW(app.material_select, CB_SETCURSEL, 0, 0);
    const int animated = app.model.frame_count > 1;
    EnableWindow(app.previous, animated);
    EnableWindow(app.play, animated);
    EnableWindow(app.next, animated);
    EnableWindow(app.scrub, animated);
    SendMessageW(app.scrub, TBM_SETRANGEMIN, FALSE, 0);
    SendMessageW(app.scrub, TBM_SETRANGEMAX, TRUE, (LPARAM)(app.model.frame_count - 1));
    SendMessageW(app.scrub, TBM_SETPOS, TRUE, 0);
    set_playing(0);
    set_model_title();
    set_stat();
    reset_view();
}

static int load(const wchar_t *name, int report_error) {
    char utf8[AV_PATH];
    AvModel *candidate = calloc(1, sizeof(*candidate));
    if (!candidate) return 0;
    int ok = name ? full_path_utf8(utf8, name) && av_model_open(candidate, utf8, app.obj_z_up)
        : av_model_demo(candidate);
    if (!ok) {
        wchar_t reason[360];
        if (!candidate->error[0]) av_copy(candidate->error, sizeof(candidate->error), "File path is too long or memory is unavailable.");
        to_wide(reason, candidate->error, 360);
        wchar_t message[480] = L"无法打开模型。\n\n";
        append_wide(message, 480, reason);
        if (report_error) MessageBoxW(app.window, message, APP_TITLE, MB_ICONERROR);
        av_model_free(candidate);
        free(candidate);
        return 0;
    }
    av_model_free(&app.model);
    app.model = *candidate;
    free(candidate);
    refresh_model_controls();
    return 1;
}

static const wchar_t model_filters[] = L"模型 (*.bmf; *.msh; *.obj)\0*.bmf;*.msh;*.obj\0"
    L"BMF 网格 (*.bmf)\0*.bmf\0MSH 网格 (*.msh)\0*.msh\0Wavefront OBJ (*.obj)\0*.obj\0所有文件\0*.*\0";
static const wchar_t texture_filters[] = L"颜色贴图 (*.dds; *.png; *.jpg; *.bmp)\0*.dds;*.png;*.jpg;*.jpeg;*.bmp\0"
    L"所有文件\0*.*\0";

static int ask_path(wchar_t *out, const wchar_t *filters, const wchar_t *title, const wchar_t *ext, int save) {
    OPENFILENAMEW dialog = {0};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = app.window;
    dialog.lpstrFilter = filters;
    dialog.nFilterIndex = 1;
    dialog.lpstrFile = out;
    dialog.nMaxFile = READ_CHARS;
    dialog.lpstrTitle = title;
    dialog.lpstrDefExt = ext;
    dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY
        | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    return save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
}

static void open_model_dialog(void) {
    wchar_t path[READ_CHARS] = L"";
    if (ask_path(path, model_filters, L"打开模型", NULL, 0)) load(path, 1);
}

static void open_texture_dialog(void) {
    wchar_t path[READ_CHARS] = L"";
    if (!ask_path(path, texture_filters, L"为当前材质指定颜色贴图", NULL, 0)) return;
    char utf8[AV_PATH];
    LRESULT mat = SendMessageW(app.material_select, CB_GETCURSEL, 0, 0);
    if (mat < 0 || !to_utf8(utf8, path, sizeof(utf8)) || !av_model_set_diffuse(&app.model, (unsigned)mat, utf8)) {
        MessageBoxW(app.window, L"无法读取贴图。支持 DXT1/DXT3/DXT5 和常见 RGB/RGBA DDS、PNG、JPEG、BMP。", APP_TITLE, MB_ICONERROR);
        return;
    }
    app.mode = AV_TEXTURED;
    SendMessageW(app.mode_select, CB_SETCURSEL, AV_TEXTURED, 0);
    set_stat();
    redraw();
}

static void change_frame(uint32_t frame) {
    if (frame == app.model.frame) return;
    if (!av_model_set_frame(&app.model, frame)) {
        set_playing(0);
        return;
    }
    SendMessageW(app.scrub, TBM_SETPOS, TRUE, frame);
    set_stat();
    redraw();
}

static void step_frame(int offset) {
    unsigned frames = app.model.frame_count;
    if (frames <= 1) return;
    uint32_t new_frame = offset > 0 ? (app.model.frame + 1) % frames : (app.model.frame + frames - 1) % frames;
    change_frame(new_frame);
}

static void update_surface(void) {
    int width = app.viewport.right - app.viewport.left, height = app.viewport.bottom - app.viewport.top;
    if (width < 1 || height < 1) return;
    if (width > PREVIEW_LIMIT || height > PREVIEW_LIMIT) {
        int largest = width > height ? width : height;
        width = width * PREVIEW_LIMIT / largest;
        height = height * PREVIEW_LIMIT / largest;
        if (width < 1) width = 1;
        if (height < 1) height = 1;
    }
    if (av_surface_size(&app.surface, width, height)) {
        if (app.dirty) av_render(&app.surface, &app.model, app.lod, &app.view, app.mode);
        app.dirty = 0;
    }
}

static void paint_window(void) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(app.window, &paint);
    RECT client;
    GetClientRect(app.window, &client);
    FillRect(dc, &client, GetSysColorBrush(COLOR_BTNFACE));
    update_surface();
    if (app.surface.pixels) {
        BITMAPINFO info = {0};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = app.surface.width;
        info.bmiHeader.biHeight = -app.surface.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        StretchDIBits(dc, app.viewport.left, app.viewport.top, app.viewport.right-app.viewport.left,
            app.viewport.bottom-app.viewport.top, 0, 0, app.surface.width, app.surface.height,
            app.surface.pixels, &info, DIB_RGB_COLORS, SRCCOPY);
    }
    EndPaint(app.window, &paint);
}

static void save_dialog(int mesh) {
    static const wchar_t bmp[] = L"位图 (*.bmp)\0*.bmp\0";
    static const wchar_t obj[] = L"Wavefront OBJ (*.obj)\0*.obj\0";
    wchar_t path[READ_CHARS] = L"";
    if (!ask_path(path, mesh ? obj : bmp, mesh ? L"导出当前 LOD 和帧的 OBJ" : L"保存当前预览",
            mesh ? L"obj" : L"bmp", 1)) return;
    char dest[AV_PATH];
    update_surface();
    int ok = to_utf8(dest, path, sizeof(dest)) && (mesh ? av_write_obj(&app.model, dest, app.lod)
        : av_save_bmp(dest, &app.surface));
    if (!ok) MessageBoxW(app.window, L"无法写入选定文件。", APP_TITLE, MB_ICONERROR);
}

static void build_menu(void) {
    HMENU menu = CreateMenu(), file = CreatePopupMenu(), view = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, CMD_OPEN, L"打开模型…\tCtrl+O");
    AppendMenuW(file, MF_STRING, CMD_DIFFUSE, L"为当前材质指定贴图…");
    AppendMenuW(file, MF_STRING, CMD_BMP, L"保存预览 BMP…");
    AppendMenuW(file, MF_STRING, CMD_OBJ, L"导出 OBJ / UV0…");
    AppendMenuW(file, MF_SEPARATOR, 0, NULL);
    AppendMenuW(file, MF_STRING, CMD_EXIT, L"退出");
    AppendMenuW(view, MF_STRING, CMD_FIT, L"复位视角\tHome");
    AppendMenuW(view, MF_STRING, CMD_DEMO, L"打开内置示例");
    AppendMenuW(view, MF_STRING, CMD_OBJ_Y, L"切换 OBJ 的 Y 向上 / Z 向上");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)file, L"文件");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)view, L"视图");
    AppendMenuW(menu, MF_STRING, CMD_HELP, L"使用说明");
    SetMenu(app.window, menu);
}

static void handle_command(unsigned id, unsigned notification) {
    switch (id) {
        case CMD_OPEN: open_model_dialog(); break;
        case CMD_DIFFUSE: open_texture_dialog(); break;
        case CMD_FIT: reset_view(); break;
        case CMD_DEMO: load(NULL, 1); break;
        case CMD_BMP: save_dialog(0); break;
        case CMD_OBJ: save_dialog(1); break;
        case CMD_EXIT: DestroyWindow(app.window); return;
        case CMD_PREVIOUS: set_playing(0); step_frame(-1); break;
        case CMD_NEXT: set_playing(0); step_frame(1); break;
        case CMD_PLAY: set_playing(!app.playing); break;
        case CMD_MODE:
            if (notification == CBN_SELCHANGE) {
                LRESULT selected = SendMessageW(app.mode_select, CB_GETCURSEL, 0, 0);
                if (selected >= 0 && selected <= AV_UV1) app.mode = (AvMode)selected;
                set_stat();
                redraw();
            }
            break;
        case CMD_LOD:
            if (notification == CBN_SELCHANGE) {
                LRESULT selected = SendMessageW(app.lod_select, CB_GETCURSEL, 0, 0);
                if (selected >= 0 && (uint32_t)selected < app.model.lod_count) app.lod = (unsigned)selected;
                set_stat();
                redraw();
            }
            break;
        case CMD_MATERIAL: return;
        case CMD_OBJ_Y:
            app.obj_z_up = !app.obj_z_up;
            if (app.model.kind == AV_OBJ) {
                wchar_t path[AV_PATH];
                if (to_wide(path, app.model.path, AV_PATH)) load(path, 1);
            }
            MessageBoxW(app.window, app.obj_z_up ? L"OBJ 坐标已设为 Z 向上（Blender / Max）。"
                : L"OBJ 坐标已设为 Y 向上（游戏坐标）。", APP_TITLE, MB_ICONINFORMATION);
            break;
        case CMD_HELP:
            MessageBoxW(app.window, L"打开或拖入 .bmf、.msh、.obj。\n"
                L"贴图优先按同目录 MTL 的路径寻找。也可以选材质后手动指定贴图。\n"
                L"BMF 含多帧时可拖动滑块，或按空格播放。\n"
                L"MSH 可以切换文件内已有 LOD。座舱光照可用光照 UV 或光照贴图模式检查。\n"
                L"OBJ 导出保留当前几何、法线、颜色 UV 和 MTL 材质路径。",
                APP_TITLE, MB_ICONINFORMATION);
            break;
        default: return;
    }
    SetFocus(app.window);
}

static void handle_drag(unsigned message, LPARAM data) {
    int x = (int)(int16_t)(data & 0xffff), y = (int)(int16_t)((data >> 16) & 0xffff);
    if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN) {
        if (y < app.viewport.top || y >= app.viewport.bottom) return;
        app.dragging = message == WM_LBUTTONDOWN ? 1 : 2;
        app.last_x = x;
        app.last_y = y;
        SetFocus(app.window);
        SetCapture(app.window);
        return;
    }
    if (!app.dragging) return;
    const int dx = x - app.last_x, dy = y - app.last_y;
    if (app.dragging == 1) {
        app.view.yaw -= (float)dx * 0.008f;
        app.view.pitch = av_clamp(app.view.pitch + (float)dy * 0.008f, -1.5f, 1.5f);
    } else {
        int height = app.viewport.bottom - app.viewport.top;
        if (app.viewport.right > 0) app.view.pan_x = av_clamp(app.view.pan_x + (float)dx / (float)app.viewport.right, -6, 6);
        if (height > 0) app.view.pan_y = av_clamp(app.view.pan_y + (float)dy / (float)height, -6, 6);
    }
    app.last_x = x;
    app.last_y = y;
    redraw();
}

static LRESULT CALLBACK window_proc(HWND wnd, UINT message, WPARAM w, LPARAM l) {
    switch (message) {
        case WM_CREATE:
            app.window = wnd;
            if (!controls_init()) return -1;
            build_menu();
            DragAcceptFiles(wnd, TRUE);
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO *limits = (MINMAXINFO *)l;
            limits->ptMinTrackSize.x = scale_ui(810);
            limits->ptMinTrackSize.y = scale_ui(510);
            return 0;
        }
        case WM_SIZE: layout(); return 0;
        case WM_PAINT: paint_window(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_COMMAND: handle_command((unsigned)(w & 0xffff), (unsigned)((w >> 16) & 0xffff)); return 0;
        case WM_HSCROLL:
            if ((HWND)l == app.scrub && app.model.frame_count > 1) {
                set_playing(0);
                change_frame((uint32_t)SendMessageW(app.scrub, TBM_GETPOS, 0, 0));
            }
            return 0;
        case WM_DROPFILES: {
            HDROP drop = (HDROP)w;
            wchar_t path[READ_CHARS] = L"";
            int got = DragQueryFileW(drop, 0, path, READ_CHARS) > 0;
            DragFinish(drop);
            if (got) load(path, 1);
            return 0;
        }
        case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MOUSEMOVE:
            handle_drag(message, l);
            return 0;
        case WM_LBUTTONUP: case WM_RBUTTONUP:
            app.dragging = 0;
            ReleaseCapture();
            return 0;
        case WM_CAPTURECHANGED: app.dragging = 0; return 0;
        case WM_LBUTTONDBLCLK: reset_view(); return 0;
        case WM_MOUSEWHEEL: {
            int delta = (int)(int16_t)((w >> 16) & 0xffff);
            float factor = delta > 0 ? 0.88f : 1.13636f;
            int notches = delta > 0 ? delta : -delta;
            if (notches && notches < 120) notches = 120;
            for (int i = 120; i <= notches; i += 120) app.view.zoom *= factor;
            app.view.zoom = av_clamp(app.view.zoom, 0.15f, 32);
            redraw();
            return 0;
        }
        case WM_TIMER:
            if (w == PLAY_TIMER && app.playing) step_frame(1);
            return 0;
        case WM_KEYDOWN:
            if (w == VK_HOME || w == 'F') reset_view();
            else if (w == VK_SPACE) set_playing(!app.playing);
            else if (w == VK_LEFT) { set_playing(0); step_frame(-1); }
            else if (w == VK_RIGHT) { set_playing(0); step_frame(1); }
            return 0;
        case WM_CLOSE: DestroyWindow(wnd); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        default: return DefWindowProcW(wnd, message, w, l);
    }
}

static int smoke_test(int count, LPWSTR *command) {
    if (count < 3 || (count > 3 && !load(command[3], 0))) return 2;
    char output[AV_PATH];
    UpdateWindow(app.window);
    update_surface();
    int rendered = 0;
    if (app.surface.depth) {
        const size_t pixels = (size_t)app.surface.width * app.surface.height;
        for (size_t i = 0; i < pixels; ++i) {
            if (app.surface.depth[i] > 0) { rendered = 1; break; }
        }
    }
    return rendered && to_utf8(output, command[2], sizeof(output)) && av_save_bmp(output, &app.surface) ? 0 : 3;
}

static int start(HINSTANCE instance, int show) {
    app.instance = instance;
    app.dpi = 96;
    app.obj_z_up = 1;
    app.mode = AV_TEXTURED;
    SetProcessDPIAware();
    HDC screen = GetDC(NULL);
    if (screen) {
        int dpi = GetDeviceCaps(screen, LOGPIXELSX);
        if (dpi > 0) app.dpi = dpi;
        ReleaseDC(NULL, screen);
    }
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    GpStartup gdiplus = {1, NULL, FALSE, FALSE};
    if (GdiplusStartup(&app.gdiplus, &gdiplus, NULL) != 0) app.gdiplus = 0;
    WNDCLASSEXW window = {0};
    window.cbSize = sizeof(window);
    window.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    window.lpfnWndProc = window_proc;
    window.hInstance = app.instance;
    window.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    window.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    window.lpszClassName = APP_CLASS;
    if (!RegisterClassExW(&window)) return 1;
    HWND main_window = CreateWindowExW(0, APP_CLASS, APP_TITLE, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, scale_ui(1080), scale_ui(740), NULL, NULL, app.instance, NULL);
    if (!main_window) return 1;
    load(NULL, 1);
    ShowWindow(main_window, show);
    UpdateWindow(main_window);
    int arguments = 0;
    LPWSTR *command = CommandLineToArgvW(GetCommandLineW(), &arguments);
    char option[24];
    const int smoke = command && arguments > 1 && to_utf8(option, command[1], sizeof(option))
        && strcmp(option, "--smoke-test") == 0;
    int status = 0;
    if (smoke) {
        status = smoke_test(arguments, command);
        DestroyWindow(main_window);
    } else if (command && arguments > 1) {
        load(command[1], 1);
    }
    if (command) LocalFree(command);
    MSG message;
    BOOL read;
    while ((read = GetMessageW(&message, NULL, 0, 0)) > 0) {
        if (message.message == WM_KEYDOWN && message.wParam == 'O' && GetKeyState(VK_CONTROL) < 0) {
            open_model_dialog();
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    av_surface_free(&app.surface);
    av_model_free(&app.model);
    if (app.gdiplus) GdiplusShutdown(app.gdiplus);
    return read < 0 ? 1 : status;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show) {
    AV_UNUSED(previous);
    AV_UNUSED(command);
    return start(instance, show);
}
