/*
 * CaDS Zero - host simulator entry point.
 *
 * The mirror image of targets/itsboard/main.c: bring the "hardware" up and hand
 * over to the same portable application. Everything specific to the host is
 * argument parsing, because the firmware itself has no arguments.
 */

#include "sim.h"

#include "bringup/bringup.h"
#include "cads_hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cads_sim_usage(const char* program) {
    fprintf(
        stderr,
        "usage: %s [options]\n"
        "  --screenshot <file.bmp>   save the first quiescent panel frame and exit\n"
        "  --screenshot-idle <ms>    how long the panel must be still, default 250\n"
        "  --screenshot-timeout <ms> give up if nothing is drawn, default 10000\n"
        "  --scale <1..4>            window magnification, default 1\n"
        "  --help\n"
        "\n"
        "controls: mouse = touch, keys 1..8 = S0..S7, F1..F6 = INT0..5,\n"
        "          space = USER button, Esc or Q = quit\n",
        program);
}

static uint32_t cads_sim_argument(int argc, char** argv, int* position, const char* name) {
    if(*position + 1 >= argc) {
        fprintf(stderr, "%s needs a value\n", name);
        exit(2);
    }
    (*position)++;
    return (uint32_t)strtoul(argv[*position], NULL, 10);
}

int main(int argc, char** argv) {
    cads_sim_options_t options = {
        .screenshot_path = NULL,
        .screenshot_idle_ms = 250u,
        .screenshot_timeout_ms = 10000u,
        .scale = 1u,
    };

    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "--screenshot") == 0) {
            if(i + 1 >= argc) {
                fprintf(stderr, "--screenshot needs a file name\n");
                return 2;
            }
            options.screenshot_path = argv[++i];
        } else if(strcmp(argv[i], "--screenshot-idle") == 0) {
            options.screenshot_idle_ms = cads_sim_argument(argc, argv, &i, argv[i]);
        } else if(strcmp(argv[i], "--screenshot-timeout") == 0) {
            options.screenshot_timeout_ms = cads_sim_argument(argc, argv, &i, argv[i]);
        } else if(strcmp(argv[i], "--scale") == 0) {
            options.scale = cads_sim_argument(argc, argv, &i, argv[i]);
        } else if(strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            cads_sim_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            cads_sim_usage(argv[0]);
            return 2;
        }
    }

    if(options.screenshot_path) {
        size_t length = strlen(options.screenshot_path);
        if(length < 4u || strcmp(options.screenshot_path + length - 4, ".bmp") != 0) {
            /* SDL writes BMP and nothing else, and a golden image that claims
             * to be a PNG while holding a BMP is a trap for the next reader. */
            fprintf(stderr, "sim: note - the screenshot is a BMP whatever it is called\n");
        }
    }

    cads_sim_configure(&options);

    cads_hal_early_init();
    cads_hal_init();

    cads_bringup_run();

    /* cads_bringup_run() does not return today. When it grows an exit path, the
     * window still has to be serviced or the host declares it hung. */
    for(;;) {
        cads_sim_pump();
        cads_hal_delay_ms(10u);
    }
}
