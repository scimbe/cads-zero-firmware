/*
 * CaDS Zero - host FS tool. Reads/writes files inside a littlefs image dumped
 * off the board (st-flash read 0x08120000 <size>), using the exact same
 * cads/storage + littlefs code the firmware runs, so the on-disk format always
 * matches. Backs scripts/cads_config.py's edit-on-the-Mac workflow.
 *
 *   cads_fs <image> get <fs-path> [out-file]   # read a file (stdout if no out)
 *   cads_fs <image> put <fs-path> <in-file>    # write/replace a file
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cads/storage/storage.h"

int cads_flash_host_load_image(const char* path);
int cads_flash_host_save_image(const char* path);

static int do_get(const char* fs_path, const char* out_path) {
    cads_storage_file_t* f = NULL;
    int rc = cads_storage_open(&f, fs_path, CADS_STORAGE_RDONLY);
    if(rc != CADS_STORAGE_OK) { fprintf(stderr, "open %s: %d\n", fs_path, rc); return 1; }
    FILE* out = out_path ? fopen(out_path, "wb") : stdout;
    if(!out) { fprintf(stderr, "cannot write %s\n", out_path); cads_storage_close(f); return 1; }
    char buf[512]; int32_t n;
    while((n = cads_storage_read(f, buf, sizeof(buf))) > 0) fwrite(buf, 1u, (size_t)n, out);
    if(out != stdout) fclose(out);
    cads_storage_close(f);
    return 0;
}

static int do_put(const char* fs_path, const char* in_path, const char* image) {
    FILE* in = fopen(in_path, "rb");
    if(!in) { fprintf(stderr, "cannot read %s\n", in_path); return 1; }
    cads_storage_file_t* f = NULL;
    int rc = cads_storage_open(&f, fs_path, CADS_STORAGE_WRONLY | CADS_STORAGE_CREAT | CADS_STORAGE_TRUNC);
    if(rc != CADS_STORAGE_OK) { fprintf(stderr, "open %s: %d\n", fs_path, rc); fclose(in); return 1; }
    char buf[512]; size_t n;
    while((n = fread(buf, 1u, sizeof(buf), in)) > 0) {
        if(cads_storage_write(f, buf, (uint32_t)n) != (int32_t)n) { fprintf(stderr, "write failed\n"); fclose(in); cads_storage_close(f); return 1; }
    }
    fclose(in);
    cads_storage_close(f);
    if(cads_flash_host_save_image(image) != 0) { fprintf(stderr, "save image failed\n"); return 1; }
    return 0;
}

int main(int argc, char** argv) {
    if(argc < 4) { fprintf(stderr, "usage: %s <image> get|put <fs-path> [file]\n", argv[0]); return 2; }
    const char* image = argv[1];
    const char* cmd = argv[2];
    const char* fs_path = argv[3];
    if(cads_flash_host_load_image(image) < 0) { fprintf(stderr, "cannot load image %s\n", image); return 1; }
    if(cads_storage_mount() != CADS_STORAGE_OK) { fprintf(stderr, "mount failed (not a littlefs image?)\n"); return 1; }
    if(strcmp(cmd, "get") == 0) return do_get(fs_path, argc >= 5 ? argv[4] : NULL);
    if(strcmp(cmd, "put") == 0) { if(argc < 5) { fprintf(stderr, "put needs an input file\n"); return 2; } return do_put(fs_path, argv[4], image); }
    fprintf(stderr, "unknown command %s\n", cmd); return 2;
}
