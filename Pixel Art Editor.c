#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <windows.h>
#include <commdlg.h>

#define MAX_WIDTH 15
#define MAX_HEIGHT 15
#define PALETTE_SIZE 16

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

#ifndef ENABLE_WRAP_AT_EOL_OUTPUT
#define ENABLE_WRAP_AT_EOL_OUTPUT 0x0002
#endif

typedef int GPSTATUS;

typedef enum { TOOL_DRAW, TOOL_ERASE, TOOL_FILL } ToolType;

typedef struct {
    int color_code;
} Pixel;

typedef struct {
    int width;
    int height;
    Pixel grid[MAX_HEIGHT][MAX_WIDTH];
} Canvas;

typedef struct {
    ToolType active_tool;
    int active_color_idx;
    char status_msg[256];
    int running;
} UIState;

// GDI+ and File Dialog Dynamic Function Prototypes
typedef struct {
    unsigned int GdiplusVersion;
    void *DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
} GdiplusStartupInput;

typedef GPSTATUS (WINAPI *pfn_GdiplusStartup)(ULONG_PTR *, const GdiplusStartupInput *, void *);
typedef void (WINAPI *pfn_GdiplusShutdown)(ULONG_PTR);
typedef GPSTATUS (WINAPI *pfn_GdipLoadImageFromFile)(const WCHAR *, void **);
typedef GPSTATUS (WINAPI *pfn_GdipGetImageWidth)(void *, UINT *);
typedef GPSTATUS (WINAPI *pfn_GdipGetImageHeight)(void *, UINT *);
typedef GPSTATUS (WINAPI *pfn_GdipBitmapGetPixel)(void *, INT, INT, DWORD *);
typedef GPSTATUS (WINAPI *pfn_GdipDisposeImage)(void *);
typedef BOOL (WINAPI *pfn_GetOpenFileNameA)(LPOPENFILENAMEA);
typedef BOOL (WINAPI *pfn_GetSaveFileNameA)(LPOPENFILENAMEA);

// 16 Palette ANSI Color Codes (1 Row x 16 Columns)
const int PALETTE[PALETTE_SIZE] = {
    0,   237, 244, 15,  124, 196, 208, 226,
    28,  46,  51,  21,  18,  129, 205, 137
};

// 16 Color Display Names
const char* COLOR_NAMES[PALETTE_SIZE] = {
    "Black", "D.Gray", "Gray", "White", "D.Red", "Red", "Orange", "Yellow",
    "D.Green", "Green", "Cyan", "Blue", "Navy", "Purple", "Pink", "Brown"
};

// 16 Color Truecolor RGB Coordinates for Image I/O
const int RGB_PALETTE[PALETTE_SIZE][3] = {
    {0, 0, 0},       {58, 58, 58},    {128, 128, 128}, {255, 255, 255},
    {175, 0, 0},     {255, 0, 0},     {255, 135, 0},   {255, 255, 0},
    {0, 95, 0},      {0, 255, 0},     {0, 255, 255},   {0, 0, 255},
    {0, 0, 135},     {175, 0, 255},   {255, 95, 175},  {175, 135, 95}
};

// Function prototypes
void init_canvas(Canvas *c, int w, int h);
void render_ui(const Canvas *c, const UIState *state, HANDLE hOutput);
void set_pixel(Canvas *c, int x, int y, int color_code);
void flood_fill(Canvas *c, int x, int y, int target, int replacement);
int save_canvas_png(const Canvas *c, const char *filename);
int load_image_from_pc(Canvas *c, const char *filepath);
int open_file_dialog(char *out_path, DWORD max_len);
int save_file_dialog(char *out_path, DWORD max_len);
void map_coordinate_input(Canvas *c, UIState *state, int px, int py, HANDLE hInput);
void setup_console(HANDLE hInput, HANDLE hOutput);
int find_nearest_palette_index(int r, int g, int b);

// PNG Checksum Tables
static unsigned long crc_table[256];
static int crc_table_computed = 0;

static void make_crc_table(void) {
    for (unsigned long n = 0; n < 256; n++) {
        unsigned long c = n;
        for (int k = 0; k < 8; k++) {
            if (c & 1) c = 0xedb88320L ^ (c >> 1);
            else c = c >> 1;
        }
        crc_table[n] = c;
    }
    crc_table_computed = 1;
}

static unsigned long update_crc(unsigned long crc, const unsigned char *buf, int len) {
    unsigned long c = crc;
    if (!crc_table_computed) make_crc_table();
    for (int n = 0; n < len; n++) c = crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
    return c;
}

static unsigned long crc32(const unsigned char *buf, int len) {
    return update_crc(0xffffffffL, buf, len) ^ 0xffffffffL;
}

static unsigned long adler32(const unsigned char *buf, int len) {
    unsigned long s1 = 1, s2 = 0;
    for (int n = 0; n < len; n++) {
        s1 = (s1 + buf[n]) % 65521;
        s2 = (s2 + s1) % 65521;
    }
    return (s2 << 16) | s1;
}

int main(void) {
    Canvas canvas;
    UIState state = {TOOL_DRAW, 5, "Click elements or grid to draw!", 1};
    char selected_path[MAX_PATH];

    init_canvas(&canvas, 15, 15);

    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    setup_console(hInput, hOutput);

    INPUT_RECORD inRec;
    DWORD numRead;

    while (state.running) {
        render_ui(&canvas, &state, hOutput);

        ReadConsoleInput(hInput, &inRec, 1, &numRead);

        if (inRec.EventType == MOUSE_EVENT) {
            MOUSE_EVENT_RECORD mer = inRec.Event.MouseEvent;
            if (mer.dwButtonState == FROM_LEFT_1ST_BUTTON_PRESSED) {
                int px = mer.dwMousePosition.X + 1;
                int py = mer.dwMousePosition.Y + 1;
                map_coordinate_input(&canvas, &state, px, py, hInput);
            }
        }
        else if (inRec.EventType == KEY_EVENT && inRec.Event.KeyEvent.bKeyDown) {
            char ch = inRec.Event.KeyEvent.uChar.AsciiChar;
            if (ch == '1') { state.active_tool = TOOL_DRAW; strcpy(state.status_msg, "Tool: DRAW"); }
            else if (ch == '2') { state.active_tool = TOOL_ERASE; strcpy(state.status_msg, "Tool: ERASE"); }
            else if (ch == '3') { state.active_tool = TOOL_FILL; strcpy(state.status_msg, "Tool: FILL"); }
            else if (ch == 'k' || ch == 'K') {
                if (save_file_dialog(selected_path, MAX_PATH)) {
                    if (save_canvas_png(&canvas, selected_path)) strcpy(state.status_msg, "Exported PNG successfully!");
                }
                FlushConsoleInputBuffer(hInput);
            }
            else if (ch == 'l' || ch == 'L') {
                if (open_file_dialog(selected_path, MAX_PATH)) {
                    if (load_image_from_pc(&canvas, selected_path)) strcpy(state.status_msg, "Loaded image into canvas!");
                    else strcpy(state.status_msg, "Failed to load image file.");
                }
                FlushConsoleInputBuffer(hInput);
            }
            else if (ch == 'q' || ch == 'Q') { state.running = 0; }
        }
    }

    return 0;
}

void setup_console(HANDLE hInput, HANDLE hOutput) {
    CONSOLE_FONT_INFOEX cfi;
    cfi.cbSize = sizeof(cfi);
    cfi.nFont = 0;
    cfi.dwFontSize.X = 12;
    cfi.dwFontSize.Y = 16;
    cfi.FontFamily = FF_DONTCARE;
    cfi.FontWeight = FW_NORMAL;
    wcscpy(cfi.FaceName, L"Consolas");
    SetCurrentConsoleFontEx(hOutput, FALSE, &cfi);

    SMALL_RECT min_rect = {0, 0, 1, 1};
    SetConsoleWindowInfo(hOutput, TRUE, &min_rect);
    COORD coord = {95, 23};
    SetConsoleScreenBufferSize(hOutput, coord);
    SMALL_RECT rect = {0, 0, 94, 22};
    SetConsoleWindowInfo(hOutput, TRUE, &rect);

    DWORD inMode = 0;
    GetConsoleMode(hInput, &inMode);
    inMode &= ~ENABLE_QUICK_EDIT_MODE;
    inMode |= ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS;
    SetConsoleMode(hInput, inMode);

    DWORD outMode = 0;
    GetConsoleMode(hOutput, &outMode);
    outMode &= ~ENABLE_WRAP_AT_EOL_OUTPUT;
    outMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOutput, outMode);

    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hOutput, &cursorInfo);
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(hOutput, &cursorInfo);
}

void init_canvas(Canvas *c, int w, int h) {
    c->width = (w > MAX_WIDTH) ? MAX_WIDTH : w;
    c->height = (h > MAX_HEIGHT) ? MAX_HEIGHT : h;
    for (int y = 0; y < c->height; y++) {
        for (int x = 0; x < c->width; x++) {
            c->grid[y][x].color_code = 0;
        }
    }
}

void render_ui(const Canvas *c, const UIState *state, HANDLE hOutput) {
    COORD home = {0, 0};
    SetConsoleCursorPosition(hOutput, home);

    printf("=== CODE::BLOCKS 15x15 PIXEL ART EDITOR (16 COLORS) ===                                \n");

    printf("PALETTE 1: ");
    for (int i = 0; i < PALETTE_SIZE; i++) {
        if (i == state->active_color_idx) printf(">\033[48;5;%dm[%2d]\033[0m", PALETTE[i], i);
        else printf(" \033[48;5;%dm[%2d]\033[0m", PALETTE[i], i);
    }
    printf("     \n");

    printf("TOOLS:     ");
    printf("%s[1:DRAW]\033[0m ", state->active_tool == TOOL_DRAW ? "\033[7m" : "");
    printf("%s[2:ERASE]\033[0m ", state->active_tool == TOOL_ERASE ? "\033[7m" : "");
    printf("%s[3:FILL]\033[0m | ", state->active_tool == TOOL_FILL ? "\033[7m" : "");
    printf("[K:SAVE PNG] [L:LOAD IMG] [Q:QUIT]                                       \n");

    printf("STATUS:    Tool: %-5s | Color: %-9s | %-40s\n",
            state->active_tool == TOOL_DRAW ? "DRAW" : state->active_tool == TOOL_ERASE ? "ERASE" : "FILL",
            COLOR_NAMES[state->active_color_idx], state->status_msg);

    printf("     ");
    for (int x = 0; x < c->width; x++) printf("%2d", x % 10);
    printf("                                                               \n   +");
    for (int x = 0; x < c->width; x++) printf("--");
    printf("+\n");

    for (int y = 0; y < c->height; y++) {
        printf("%2d |", y);
        for (int x = 0; x < c->width; x++) {
            printf("\033[48;5;%dm  \033[0m", c->grid[y][x].color_code);
        }
        printf("|\n");
    }

    printf("   +");
    for (int x = 0; x < c->width; x++) printf("--");
    printf("+\n");

    fflush(stdout);
}

int open_file_dialog(char *out_path, DWORD max_len) {
    HMODULE hComdlg = LoadLibraryA("comdlg32.dll");
    if (!hComdlg) return 0;

    pfn_GetOpenFileNameA pGetOpenFileNameA = (pfn_GetOpenFileNameA)GetProcAddress(hComdlg, "GetOpenFileNameA");
    if (!pGetOpenFileNameA) { FreeLibrary(hComdlg); return 0; }

    OPENFILENAMEA ofn;
    char szFile[MAX_PATH] = {0};
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    // Modified to filter PNG, JPG, JPEG, and all image extensions natively
    ofn.lpstrFilter = "Supported Images (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0PNG Images (*.png)\0*.png\0JPEG Images (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    int res = 0;
    if (pGetOpenFileNameA(&ofn)) {
        strncpy(out_path, szFile, max_len - 1);
        out_path[max_len - 1] = '\0';
        res = 1;
    }
    FreeLibrary(hComdlg);
    return res;
}

int save_file_dialog(char *out_path, DWORD max_len) {
    HMODULE hComdlg = LoadLibraryA("comdlg32.dll");
    if (!hComdlg) return 0;

    pfn_GetSaveFileNameA pGetSaveFileNameA = (pfn_GetSaveFileNameA)GetProcAddress(hComdlg, "GetSaveFileNameA");
    if (!pGetSaveFileNameA) { FreeLibrary(hComdlg); return 0; }

    OPENFILENAMEA ofn;
    char szFile[MAX_PATH] = "my_art16.png";
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "PNG Images (*.png)\0*.png\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = "png";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    int res = 0;
    if (pGetSaveFileNameA(&ofn)) {
        strncpy(out_path, szFile, max_len - 1);
        out_path[max_len - 1] = '\0';
        res = 1;
    }
    FreeLibrary(hComdlg);
    return res;
}

// Handles both JPEG and PNG image loading automatically via Windows GDI+
int load_image_from_pc(Canvas *c, const char *filepath) {
    HMODULE hGdiplus = LoadLibraryA("gdiplus.dll");
    if (!hGdiplus) return 0;

    pfn_GdiplusStartup GdiplusStartup = (pfn_GdiplusStartup)GetProcAddress(hGdiplus, "GdiplusStartup");
    pfn_GdiplusShutdown GdiplusShutdown = (pfn_GdiplusShutdown)GetProcAddress(hGdiplus, "GdiplusShutdown");
    pfn_GdipLoadImageFromFile GdipLoadImageFromFile = (pfn_GdipLoadImageFromFile)GetProcAddress(hGdiplus, "GdipLoadImageFromFile");
    pfn_GdipGetImageWidth GdipGetImageWidth = (pfn_GdipGetImageWidth)GetProcAddress(hGdiplus, "GdipGetImageWidth");
    pfn_GdipGetImageHeight GdipGetImageHeight = (pfn_GdipGetImageHeight)GetProcAddress(hGdiplus, "GdipGetImageHeight");
    pfn_GdipBitmapGetPixel GdipBitmapGetPixel = (pfn_GdipBitmapGetPixel)GetProcAddress(hGdiplus, "GdipBitmapGetPixel");
    pfn_GdipDisposeImage GdipDisposeImage = (pfn_GdipDisposeImage)GetProcAddress(hGdiplus, "GdipDisposeImage");

    if (!GdiplusStartup || !GdipLoadImageFromFile || !GdipGetImageWidth || !GdipGetImageHeight || !GdipBitmapGetPixel || !GdipDisposeImage) {
        FreeLibrary(hGdiplus);
        return 0;
    }

    ULONG_PTR token;
    GdiplusStartupInput input = {1, NULL, FALSE, FALSE};
    if (GdiplusStartup(&token, &input, NULL) != 0) {
        FreeLibrary(hGdiplus);
        return 0;
    }

    WCHAR wpath[MAX_PATH];
    MultiByteToWideChar(CP_ACP, 0, filepath, -1, wpath, MAX_PATH);

    void *image = NULL;
    if (GdipLoadImageFromFile(wpath, &image) != 0 || !image) {
        GdiplusShutdown(token);
        FreeLibrary(hGdiplus);
        return 0;
    }

    UINT img_w = 0, img_h = 0;
    GdipGetImageWidth(image, &img_w);
    GdipGetImageHeight(image, &img_h);

    if (img_w == 0 || img_h == 0) {
        GdipDisposeImage(image);
        GdiplusShutdown(token);
        FreeLibrary(hGdiplus);
        return 0;
    }

    for (int y = 0; y < c->height; y++) {
        int src_y = (y * img_h) / c->height;
        for (int x = 0; x < c->width; x++) {
            int src_x = (x * img_w) / c->width;
            DWORD argb = 0;
            GdipBitmapGetPixel(image, src_x, src_y, &argb);

            int a = (argb >> 24) & 0xFF;
            int r = (argb >> 16) & 0xFF;
            int g = (argb >> 8) & 0xFF;
            int b = argb & 0xFF;

            if (a < 128) {
                c->grid[y][x].color_code = PALETTE[0];
            } else {
                int pal_idx = find_nearest_palette_index(r, g, b);
                c->grid[y][x].color_code = PALETTE[pal_idx];
            }
        }
    }

    GdipDisposeImage(image);
    GdiplusShutdown(token);
    FreeLibrary(hGdiplus);
    return 1;
}

int find_nearest_palette_index(int r, int g, int b) {
    int min_dist = 999999;
    int best_idx = 0;

    for (int i = 0; i < PALETTE_SIZE; i++) {
        int dr = r - RGB_PALETTE[i][0];
        int dg = g - RGB_PALETTE[i][1];
        int db = b - RGB_PALETTE[i][2];
        int dist = dr * dr + dg * dg + db * db;
        if (dist < min_dist) {
            min_dist = dist;
            best_idx = i;
        }
    }
    return best_idx;
}

void map_coordinate_input(Canvas *c, UIState *state, int px, int py, HANDLE hInput) {
    char selected_path[MAX_PATH];

    if (py == 2) {
        int col = (px - 12) / 5;
        if (col >= 0 && col < PALETTE_SIZE) {
            state->active_color_idx = col;
            snprintf(state->status_msg, sizeof(state->status_msg), "Selected Color: %s", COLOR_NAMES[col]);
        }
    } else if (py == 3) {
        if (px >= 11 && px <= 18) { state->active_tool = TOOL_DRAW; strcpy(state->status_msg, "Tool set to DRAW."); }
        else if (px >= 20 && px <= 28) { state->active_tool = TOOL_ERASE; strcpy(state->status_msg, "Tool set to ERASE."); }
        else if (px >= 30 && px <= 37) { state->active_tool = TOOL_FILL; strcpy(state->status_msg, "Tool set to FILL."); }
        else if (px >= 41 && px <= 52) {
            if (save_file_dialog(selected_path, MAX_PATH)) {
                if (save_canvas_png(c, selected_path)) strcpy(state->status_msg, "Exported PNG successfully!");
            }
            FlushConsoleInputBuffer(hInput);
        }
        else if (px >= 54 && px <= 65) {
            if (open_file_dialog(selected_path, MAX_PATH)) {
                if (load_image_from_pc(c, selected_path)) strcpy(state->status_msg, "Loaded image into canvas!");
                else strcpy(state->status_msg, "Failed to load image file.");
            }
            FlushConsoleInputBuffer(hInput);
        }
        else if (px >= 67 && px <= 74) { state->running = 0; }
    } else if (py >= 7 && py < 7 + c->height) {
        int canvas_y = py - 7;
        int canvas_x = (px - 6) / 2;

        if (canvas_x >= 0 && canvas_x < c->width) {
            if (state->active_tool == TOOL_DRAW) {
                set_pixel(c, canvas_x, canvas_y, PALETTE[state->active_color_idx]);
                snprintf(state->status_msg, sizeof(state->status_msg), "Drawn at (%d, %d)", canvas_x, canvas_y);
            } else if (state->active_tool == TOOL_ERASE) {
                set_pixel(c, canvas_x, canvas_y, PALETTE[0]);
                snprintf(state->status_msg, sizeof(state->status_msg), "Erased at (%d, %d)", canvas_x, canvas_y);
            } else if (state->active_tool == TOOL_FILL) {
                int target = c->grid[canvas_y][canvas_x].color_code;
                flood_fill(c, canvas_x, canvas_y, target, PALETTE[state->active_color_idx]);
                snprintf(state->status_msg, sizeof(state->status_msg), "Filled area from (%d, %d)", canvas_x, canvas_y);
            }
        }
    }
}

void set_pixel(Canvas *c, int x, int y, int color_code) {
    if (x >= 0 && x < c->width && y >= 0 && y < c->height) {
        c->grid[y][x].color_code = color_code;
    }
}

void flood_fill(Canvas *c, int x, int y, int target, int replacement) {
    if (x < 0 || x >= c->width || y < 0 || y >= c->height) return;
    if (target == replacement || c->grid[y][x].color_code != target) return;

    c->grid[y][x].color_code = replacement;

    flood_fill(c, x + 1, y, target, replacement);
    flood_fill(c, x - 1, y, target, replacement);
    flood_fill(c, x, y + 1, target, replacement);
    flood_fill(c, x, y - 1, target, replacement);
}

int save_canvas_png(const Canvas *c, const char *filename) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) return 0;

    int scale = 16;
    int img_w = c->width * scale;
    int img_h = c->height * scale;
    int row_bytes = 1 + img_w * 3;
    int raw_size = img_h * row_bytes;

    unsigned char *raw_data = (unsigned char *)malloc(raw_size);
    if (!raw_data) { fclose(fp); return 0; }

    for (int y = 0; y < img_h; y++) {
        int grid_y = y / scale;
        int row_start = y * row_bytes;
        raw_data[row_start] = 0;

        for (int x = 0; x < img_w; x++) {
            int grid_x = x / scale;
            int code = c->grid[grid_y][grid_x].color_code;

            int pal_idx = 0;
            for (int p = 0; p < PALETTE_SIZE; p++) {
                if (PALETTE[p] == code) { pal_idx = p; break; }
            }

            int offset = row_start + 1 + x * 3;
            raw_data[offset + 0] = RGB_PALETTE[pal_idx][0];
            raw_data[offset + 1] = RGB_PALETTE[pal_idx][1];
            raw_data[offset + 2] = RGB_PALETTE[pal_idx][2];
        }
    }

    unsigned char png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(png_sig, 1, 8, fp);

    unsigned char ihdr[25] = {
        0, 0, 0, 13, 'I', 'H', 'D', 'R',
        (img_w >> 24) & 0xFF, (img_w >> 16) & 0xFF, (img_w >> 8) & 0xFF, img_w & 0xFF,
        (img_h >> 24) & 0xFF, (img_h >> 16) & 0xFF, (img_h >> 8) & 0xFF, img_h & 0xFF,
        8, 2, 0, 0, 0
    };
    unsigned long ihdr_crc = crc32(&ihdr[4], 17);
    ihdr[21] = (ihdr_crc >> 24) & 0xFF; ihdr[22] = (ihdr_crc >> 16) & 0xFF;
    ihdr[23] = (ihdr_crc >> 8) & 0xFF; ihdr[24] = ihdr_crc & 0xFF;
    fwrite(ihdr, 1, 25, fp);

    int num_blocks = (raw_size + 65534) / 65535;
    int zlib_size = 2 + (num_blocks * 5) + raw_size + 4;

    unsigned char chunk_hdr[8] = {
        (zlib_size >> 24) & 0xFF, (zlib_size >> 16) & 0xFF, (zlib_size >> 8) & 0xFF, zlib_size & 0xFF,
        'I', 'D', 'A', 'T'
    };
    fwrite(chunk_hdr, 1, 8, fp);

    unsigned long idat_crc = update_crc(0xffffffffL, &chunk_hdr[4], 4);
    unsigned char zhdr[2] = {0x78, 0x01};
    fwrite(zhdr, 1, 2, fp);
    idat_crc = update_crc(idat_crc, zhdr, 2);

    int bytes_left = raw_size, data_offset = 0;
    while (bytes_left > 0) {
        int block_len = (bytes_left > 65535) ? 65535 : bytes_left;
        unsigned char bfinal = (bytes_left == block_len) ? 1 : 0;
        unsigned char bhdr[5] = {
            bfinal, block_len & 0xFF, (block_len >> 8) & 0xFF,
            (~block_len) & 0xFF, ((~block_len) >> 8) & 0xFF
        };

        fwrite(bhdr, 1, 5, fp);
        idat_crc = update_crc(idat_crc, bhdr, 5);

        fwrite(&raw_data[data_offset], 1, block_len, fp);
        idat_crc = update_crc(idat_crc, &raw_data[data_offset], block_len);

        data_offset += block_len;
        bytes_left -= block_len;
    }

    unsigned long adl = adler32(raw_data, raw_size);
    unsigned char adl_buf[4] = {
        (adl >> 24) & 0xFF, (adl >> 16) & 0xFF, (adl >> 8) & 0xFF, adl & 0xFF
    };
    fwrite(adl_buf, 1, 4, fp);
    idat_crc = update_crc(idat_crc, adl_buf, 4);

    idat_crc ^= 0xffffffffL;
    unsigned char idat_crc_buf[4] = {
        (idat_crc >> 24) & 0xFF, (idat_crc >> 16) & 0xFF, (idat_crc >> 8) & 0xFF, idat_crc & 0xFF
    };
    fwrite(idat_crc_buf, 1, 4, fp);

    unsigned char iend[12] = {0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAE, 0x42, 0x60, 0x82};
    fwrite(iend, 1, 12, fp);

    free(raw_data);
    fclose(fp);
    return 1;
}
