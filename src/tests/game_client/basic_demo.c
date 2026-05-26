/*
 * basic_demo.c — Minimal example demonstrating the client C API.
 *
 * Build (from src/client/):
 *   mkdir build && cd build && cmake .. -DCLIENT_BUILD_EXAMPLES=ON && cmake --build .
 *
 * Run:
 *   ./client_example
 */

#include "client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Timer callback ──────────────────────────────────────────────────── */

static int g_ticks = 0;

static void on_timer(int timer_id, void* userdata) {
    (void)timer_id;
    const char* label = (const char*)userdata;
    printf("[timer] %s (tick %d)\n", label, g_ticks);
}

/* ── HTTP response callback ──────────────────────────────────────────── */

static void on_http_response(int http_code, const char* body, int body_len,
                             void* userdata) {
    (void)userdata;
    printf("[http] response: HTTP %d, body=%d bytes\n", http_code, body_len);
    if (body_len > 0 && body_len < 512) {
        printf("  body: %.*s\n", body_len, body);
    }
}

/* ── TCP client callbacks ─────────────────────────────────────────────── */

static void on_connect(game_net_client_t* cli, void* userdata) {
    (void)userdata;
    printf("[tcp] connected\n");
    game_tcp_send(cli, "hello from C API", 17);
}

static void on_message(game_net_client_t* cli,
                       const char* data, int data_len, void* userdata) {
    (void)cli;
    (void)userdata;
    printf("[tcp] recv: %.*s\n", data_len, data);
}

static void on_close(game_net_client_t* cli, void* userdata) {
    (void)cli;
    (void)userdata;
    printf("[tcp] connection closed\n");
}

/* ── Main ─────────────────────────────────────────────────────────────── */

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    printf("game_client version: %s\n", game_version());

    /* 1. Create and initialise the engine */
    game_client_t* cli = NULL;
    game_error_t err = game_client_create(&cli);
    if (err != GAME_OK) {
        fprintf(stderr, "game_client_create failed: %d\n", err);
        return 1;
    }

    err = game_client_init(cli, NULL);  /* NULL = use default config */
    if (err != GAME_OK) {
        char ebuf[256];
        game_client_last_error(cli, ebuf, sizeof(ebuf));
        fprintf(stderr, "game_client_init failed: %s\n", ebuf);
        game_client_destroy(&cli);
        return 1;
    }

    printf("engine initialised. running=%d\n", game_client_is_running(cli));

    /* 2. Execute a Lua script */
    err = game_client_do_string(cli,
        "print('[lua] Hello from C API!')\n"
        "log_info('[lua] This goes to the Quill logger')\n",
        NULL, 0);
    if (err != GAME_OK) {
        char ebuf[256];
        game_client_last_error(cli, ebuf, sizeof(ebuf));
        fprintf(stderr, "script error: %s\n", ebuf);
    }

    /* 3. Create a periodic timer */
    static char timer_tag[] = "periodic";
    int timer_id = 0;
    err = game_timer_interval(cli, 1000, on_timer, timer_tag, &timer_id);
    if (err == GAME_OK) {
        printf("timer created, id=%d\n", timer_id);
    }

    /* 4. Fire an HTTP GET */
    game_http_get(cli, "https://httpbin.org/get", on_http_response, NULL);

    /* 5. Simulate a game loop for a few ticks.
     *    In a real game engine, you'd call game_client_tick() each frame. */
    printf("\n--- starting game loop (5 ticks) ---\n");
    for (int i = 0; i < 5; i++) {
        g_ticks = i;
        game_client_tick(cli);
        /* In a real app, you'd use a high-precision sleep/timer here */
    }

    /* 6. Cancel the timer */
    game_timer_cancel(cli, timer_id);

    /* 7. Shutdown */
    printf("\n--- shutting down ---\n");
    game_client_destroy(&cli);
    printf("done.\n");

    return 0;
}
