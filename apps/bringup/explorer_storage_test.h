#ifndef CADS_EXPLORER_STORAGE_TEST_H
#define CADS_EXPLORER_STORAGE_TEST_H

/**
 * M4's hardware gate: write, power-cycle, read back; verify the firmware
 * region is untouched by comparing a flash CRC before and after.
 *
 * Reached from the hardware explorer's 'u' command, meant to be run twice
 * across a real reset (not a reflash - reflashing rewrites bank 1, which
 * would make the "untouched" comparison meaningless):
 *
 *   1. First run: no filesystem exists yet. Formats the volume (erases the
 *      seven storage sectors), writes a small known test file, and reports
 *      the firmware region's CRC32 before returning.
 *   2. Reset the board (`st-flash ... reset`, or the physical button - not
 *      st-flash write, which would reflash bank 1).
 *   3. Second run: a filesystem already exists. Mounts it (without
 *      reformatting), reads the same test file back, and reports PASS only
 *      if its contents survived the reset intact. Reports the firmware
 *      region's CRC32 again - compare the two runs' values by eye; they
 *      must be identical, since nothing here ever touches bank 1.
 *
 * Portable: builds and runs on both targets, since cads/storage/storage.h
 * and its two flash backends both do. Only the CRC print is board-only
 * (#ifdef CADS_TARGET_ITSBOARD inside the .c file) - there is no comparable
 * "firmware region" to protect on the host, and no single board-only
 * function is large enough here to be worth splitting into its own _sim.c
 * companion the way explorer_fault_test.c and explorer_app_demo.c are.
 */
void cads_explorer_storage_test(void);

/**
 * Diagnostic: exercise cads/storage/flash.h directly - init, geometry,
 * erase one block, program, read back - with no littlefs anywhere in the
 * path. Exists to answer one question when something goes wrong in
 * cads_explorer_storage_test(): is the raw flash driver itself sound, or is
 * whatever failed specific to what littlefs does on top of it.
 */
void cads_explorer_flash_raw_test(void);

#endif /* CADS_EXPLORER_STORAGE_TEST_H */
