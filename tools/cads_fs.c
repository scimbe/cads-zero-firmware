/*
 * CaDS Zero - host FS tool. Reads/writes files inside a littlefs image dumped
 * off the board (st-flash read 0x08120000 <size>), using the exact same
 * cads/storage + littlefs code the firmware runs, so the on-disk format always
 * matches. Backs scripts/cads_config.py's edit-on-the-Mac workflow.
 *
 *   cads_fs <image> get <fs-path> [out-file]   # read a file (stdout if no out)
 *   cads_fs <image> put <fs-path> <in-file>    # write/replace a file
 *
 * Every I/O result is checked: a short read/write or a failed close is an
 * error, not a silent truncation - the image can be written back to the
 * board, so reporting a partial transfer as success risks real data loss.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

#include "cads/storage/storage.h"

int cads_flash_host_load_image(const char* path);
int cads_flash_host_save_image(const char* path);

static int do_get(const char* fs_path, const char* out_path) {
    cads_storage_file_t* f = NULL;
    int rc = cads_storage_open(&f, fs_path, CADS_STORAGE_RDONLY);
    if(rc != CADS_STORAGE_OK) {
        fprintf(stderr, "open %s: %d\n", fs_path, rc);
        return 1;
    }

    FILE* out = stdout;
    if(out_path) {
        out = fopen(out_path, "wb");
        if(!out) {
            fprintf(stderr, "cannot write %s\n", out_path);
            cads_storage_close(f);
            return 1;
        }
    } else {
#if defined(_WIN32)
        /* stdout is text-mode by default on Windows and would mangle binary
         * data (LF -> CRLF, a stray ^Z as EOF). This tool moves file bytes
         * verbatim; put stdout in binary mode. */
        _setmode(_fileno(stdout), _O_BINARY);
#endif
    }

    char buf[512];
    int result = 0;
    for(;;) {
        int32_t n = cads_storage_read(f, buf, sizeof(buf));
        if(n < 0) { /* a read error is not EOF - fail, do not exit 0 truncated */
            fprintf(stderr, "read %s: %d\n", fs_path, (int)n);
            result = 1;
            break;
        }
        if(n == 0) break; /* clean EOF */
        if(fwrite(buf, 1u, (size_t)n, out) != (size_t)n) {
            fprintf(stderr, "short write to %s\n", out_path ? out_path : "<stdout>");
            result = 1;
            break;
        }
    }

    cads_storage_close(f);
    if(out != stdout) {
        if(fclose(out) != 0) {
            fprintf(stderr, "close %s failed\n", out_path);
            result = 1;
        }
    } else if(fflush(stdout) != 0) {
        fprintf(stderr, "flush stdout failed\n");
        result = 1;
    }
    return result;
}

static int do_put(const char* fs_path, const char* in_path, const char* image) {
    FILE* in = fopen(in_path, "rb");
    if(!in) {
        fprintf(stderr, "cannot read %s\n", in_path);
        return 1;
    }
    cads_storage_file_t* f = NULL;
    int rc = cads_storage_open(&f, fs_path, CADS_STORAGE_WRONLY | CADS_STORAGE_CREAT | CADS_STORAGE_TRUNC);
    if(rc != CADS_STORAGE_OK) {
        fprintf(stderr, "open %s: %d\n", fs_path, rc);
        fclose(in);
        return 1;
    }

    char buf[512];
    int result = 0;
    for(;;) {
        size_t n = fread(buf, 1u, sizeof(buf), in);
        if(n == 0) {
            if(ferror(in)) {
                fprintf(stderr, "read %s failed\n", in_path);
                result = 1;
            }
            break;
        }
        if(cads_storage_write(f, buf, (uint32_t)n) != (int32_t)n) {
            fprintf(stderr, "write to %s failed\n", fs_path);
            result = 1;
            break;
        }
    }
    fclose(in);

    /* littlefs commits on close - a failed close means the file was NOT
     * durably written, so it must not be reported as success or the image
     * would be written back to the board missing/short the file. */
    int cl = cads_storage_close(f);
    if(cl != CADS_STORAGE_OK) {
        fprintf(stderr, "commit %s failed: %d\n", fs_path, cl);
        result = 1;
    }
    if(result != 0) {
        fprintf(stderr, "NOT writing image back - transfer failed\n");
        return 1;
    }

    if(cads_flash_host_save_image(image) != 0) {
        fprintf(stderr, "save image %s failed\n", image);
        return 1;
    }
    return 0;
}

int main(int argc, char** argv) {
    if(argc < 4) {
        fprintf(stderr, "usage: %s <image> get|put <fs-path> [file]\n", argv[0]);
        return 2;
    }
    const char* image = argv[1];
    const char* cmd = argv[2];
    const char* fs_path = argv[3];
    if(cads_flash_host_load_image(image) != 0) {
        fprintf(stderr, "cannot load image %s (missing, or not the expected size)\n", image);
        return 1;
    }
    if(cads_storage_mount() != CADS_STORAGE_OK) {
        fprintf(stderr, "mount failed - not a valid littlefs image?\n");
        return 1;
    }
    if(strcmp(cmd, "get") == 0) return do_get(fs_path, argc >= 5 ? argv[4] : NULL);
    if(strcmp(cmd, "put") == 0) {
        if(argc < 5) {
            fprintf(stderr, "put needs an input file\n");
            return 2;
        }
        return do_put(fs_path, argv[4], image);
    }
    fprintf(stderr, "unknown command %s\n", cmd);
    return 2;
}
