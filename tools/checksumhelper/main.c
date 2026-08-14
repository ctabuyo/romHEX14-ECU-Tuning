/*
 * checksumhelper.exe — 32-bit bridge for WinOLS-compatible checksum DLLs
 *
 * Usage:
 *   checksumhelper.exe <dll_path> <input_file> <output_file> <opcode>
 *
 * Opcodes (matching DefDLLProc dispatch table; 102=verify, 103=correct):
 *   102 = verify checksum   (exit 0=OK, 1=mismatch)
 *   103 = correct checksum  (writes corrected ROM to output_file, exit 0=OK)
 *
 * Exit codes:
 *   0 = OK / checksum correct
 *   1 = checksum mismatch (verify) / write failed (correct)
 *   2 = DLL load / function not found
 *   3 = file I/O error
 *   4 = bad arguments
 *
 * stdout: single JSON line with result details
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* DefDLLProc signature: int __stdcall DefDLLProc(void* data, DWORD size, DWORD opcode) */
typedef int (__stdcall *DefDLLProc_t)(unsigned char*, DWORD, DWORD);

static unsigned char* read_file(const char* path, DWORD* out_size) {
    HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return NULL;

    DWORD size = GetFileSize(hFile, NULL);
    if (size == INVALID_FILE_SIZE || size == 0) { CloseHandle(hFile); return NULL; }

    unsigned char* buf = (unsigned char*)malloc(size);
    if (!buf) { CloseHandle(hFile); return NULL; }

    DWORD read = 0;
    if (!ReadFile(hFile, buf, size, &read, NULL) || read != size) {
        free(buf); CloseHandle(hFile); return NULL;
    }
    CloseHandle(hFile);
    *out_size = size;
    return buf;
}

static int write_file(const char* path, const unsigned char* buf, DWORD size) {
    HANDLE hFile = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return 0;
    DWORD written = 0;
    int ok = WriteFile(hFile, buf, size, &written, NULL) && written == size;
    CloseHandle(hFile);
    return ok;
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        printf("{\"error\":\"usage: checksumhelper <dll> <input> <output> <opcode>\",\"code\":4}\n");
        return 4;
    }

    const char* dll_path    = argv[1];
    const char* input_file  = argv[2];
    const char* output_file = argv[3];
    DWORD opcode            = (DWORD)atoi(argv[4]);

    /* Load DLL */
    HMODULE hDll = LoadLibraryA(dll_path);
    if (!hDll) {
        DWORD err = GetLastError();
        printf("{\"error\":\"LoadLibrary failed\",\"code\":2,\"win32\":%lu}\n", err);
        return 2;
    }

    /* Find export — try both names */
    DefDLLProc_t fn = (DefDLLProc_t)GetProcAddress(hDll, "_DefDLLProc@12");
    if (!fn) fn = (DefDLLProc_t)GetProcAddress(hDll, "DefDLLProc");
    if (!fn) {
        printf("{\"error\":\"DefDLLProc not found\",\"code\":2}\n");
        FreeLibrary(hDll);
        return 2;
    }

    /* Read ROM */
    DWORD rom_size = 0;
    unsigned char* rom = read_file(input_file, &rom_size);
    if (!rom) {
        printf("{\"error\":\"cannot read input file\",\"code\":3}\n");
        FreeLibrary(hDll);
        return 3;
    }

    /* Call DLL */
    int result = fn(rom, rom_size, opcode);

    if (opcode == 103) {
        /* correct mode: write (possibly modified) ROM back */
        if (!write_file(output_file, rom, rom_size)) {
            printf("{\"error\":\"cannot write output file\",\"code\":3,\"dll_result\":%d}\n", result);
            free(rom);
            FreeLibrary(hDll);
            return 3;
        }
        printf("{\"ok\":true,\"opcode\":%lu,\"dll_result\":%d,\"rom_size\":%lu}\n",
               opcode, result, rom_size);
        free(rom);
        FreeLibrary(hDll);
        return (result == 0) ? 0 : 1;
    } else {
        /* verify mode: result 0=OK, non-zero=mismatch */
        printf("{\"ok\":%s,\"opcode\":%lu,\"dll_result\":%d,\"rom_size\":%lu}\n",
               result == 0 ? "true" : "false", opcode, result, rom_size);
        free(rom);
        FreeLibrary(hDll);
        return (result == 0) ? 0 : 1;
    }
}
