#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <string.h>

#include "../include/constants.h"
#include <cjson/cJSON.h>

typedef enum {
    COMPOSITOR_AUTO,
    COMPOSITOR_NIRI,
    COMPOSITOR_HYPRLAND
} compositor_type_t;

typedef struct {
    bool verbose;
    bool fork_process;
    bool do_pywal;
    bool do_matugen;
    compositor_type_t compositor;
    char* output_name;
    char* mpvpaper_socket_path;
    int mpvpaper_socket_fd;
    char* hyprland_socket_path;
    int hyprland_socket_fd;
    char* niri_socket_path;
    int niri_socket_fd;
    int socket_wait_time;
    int polling_period;
} config_t;

void print_help(const char *program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  -v, --verbose              Enables verbose output\n");
    printf("  -f, --fork                 Forks the process\n");
    printf("  -o, --output NAME          Target specific monitor output (e.g. DP-1, ALL) (default: ALL)\n");
    printf("  -p, --socket-path PATH     Path to the mpvpaper socket (default: /tmp/mpvsocket)\n");
    printf("  -w, --socket-wait-time TIME Wait time for the socket in milliseconds (default: 5000)\n");
    printf("  -t, --period TIME          Polling period in milliseconds (default: 1000)\n");
    printf("  -C, --compositor NAME      Compositor to use: niri, hyprland, or auto (default: auto)\n");
    printf("  -c <color_backend>         Chooses color backend (pywal or matugen)\n");
    printf("  --pywal                    Runs pywal on pause\n");
    printf("  --matugen                  Runs matugen on pause\n");
    printf("  -h, --help                 Shows this help message\n");
}

void log_verbose(const char* message, const config_t* config) {
    if (!config->verbose) return;
    printf("%ld: %s\n", time(NULL), message);
}

char* get_hyprctl_socket_path() {
    char* xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (!xdg_runtime_dir) return NULL;

    char* hyprland_instance_signature = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!hyprland_instance_signature) return NULL;

    char path[256];
    snprintf(path, sizeof(path), "%s/hypr/%s/.socket.sock", xdg_runtime_dir, hyprland_instance_signature);
    if (access(path, F_OK) == 0) return strdup(path);

    snprintf(path, sizeof(path), "/tmp/hypr/%s/.socket.sock", hyprland_instance_signature);
    if (access(path, F_OK) == 0) return strdup(path);

    return NULL;
}

char* get_hyprctl_event_socket_path() {
    char* xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (!xdg_runtime_dir) return NULL;

    char* hyprland_instance_signature = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!hyprland_instance_signature) return NULL;

    char path[256];
    snprintf(path, sizeof(path), "%s/hypr/%s/.socket2.sock", xdg_runtime_dir, hyprland_instance_signature);
    if (access(path, F_OK) == 0) return strdup(path);

    snprintf(path, sizeof(path), "/tmp/hypr/%s/.socket2.sock", hyprland_instance_signature);
    if (access(path, F_OK) == 0) return strdup(path);

    return NULL;
}

char* get_niri_socket_path() {
    char* niri_socket = getenv("NIRI_SOCKET");
    if (niri_socket && access(niri_socket, F_OK) == 0) {
        return strdup(niri_socket);
    }

    char* xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (xdg_runtime_dir) {
        char direct_path[256];
        snprintf(direct_path, sizeof(direct_path), "%s/niri.sock", xdg_runtime_dir);
        if (access(direct_path, F_OK) == 0) {
            return strdup(direct_path);
        }

        DIR* dir = opendir(xdg_runtime_dir);
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != NULL) {
                if (strncmp(entry->d_name, "niri", 4) == 0 && strstr(entry->d_name, ".sock")) {
                    char found_path[512];
                    snprintf(found_path, sizeof(found_path), "%s/%s", xdg_runtime_dir, entry->d_name);
                    closedir(dir);
                    return strdup(found_path);
                }
            }
            closedir(dir);
        }
    }

    DIR* tmp_dir = opendir("/tmp");
    if (tmp_dir) {
        struct dirent* entry;
        while ((entry = readdir(tmp_dir)) != NULL) {
            if (strncmp(entry->d_name, "niri", 4) == 0 && strstr(entry->d_name, ".sock")) {
                char found_path[512];
                snprintf(found_path, sizeof(found_path), "/tmp/%s", entry->d_name);
                closedir(tmp_dir);
                return strdup(found_path);
            }
        }
        closedir(tmp_dir);
    }

    return NULL;
}

void detect_compositor(config_t* config) {
    if (config->compositor != COMPOSITOR_AUTO) return;

    char* niri_sock = get_niri_socket_path();
    if (niri_sock) {
        config->compositor = COMPOSITOR_NIRI;
        config->niri_socket_path = niri_sock;
        log_verbose("Detected compositor: Niri", config);
        return;
    }

    char* hypr_sock = get_hyprctl_socket_path();
    if (hypr_sock) {
        config->compositor = COMPOSITOR_HYPRLAND;
        config->hyprland_socket_path = hypr_sock;
        log_verbose("Detected compositor: Hyprland", config);
        return;
    }

    config->compositor = COMPOSITOR_NIRI;
    log_verbose("Defaulting to compositor: Niri", config);
}

void init_config(config_t* config) {
    config->verbose = false;
    config->fork_process = false;
    config->do_pywal = false;
    config->do_matugen = false;
    config->compositor = COMPOSITOR_AUTO;
    config->output_name = NULL;
    config->mpvpaper_socket_path = DEFAULT_MPVPAPER_SOCKET_PATH;
    config->mpvpaper_socket_fd = -1;
    config->hyprland_socket_path = NULL;
    config->hyprland_socket_fd = -1;
    config->niri_socket_path = NULL;
    config->niri_socket_fd = -1;
    config->socket_wait_time = DEFAULT_MPVPAPER_SOCKET_WAIT_TIME;
    config->polling_period = DEFAULT_PERIOD;
}

void wait_for_socket(const char *socket_path, const config_t* config) {
    int elapsed = 0;
    while (elapsed < config->socket_wait_time * 1000) {
        const int interval = 100000;
        if (access(socket_path, F_OK) == 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Socket %s is available", socket_path);
            log_verbose(msg, config);
            return;
        }

        char msg[256];
        snprintf(msg, sizeof(msg), "Socket %s not available, sleeping...", socket_path);
        log_verbose(msg, config);

        usleep(interval);
        elapsed += interval;
    }

    fprintf(stderr, "error: socket %s not available after waiting %d ms\n", socket_path, config->socket_wait_time);
    exit(EXIT_FAILURE);
}

int initialize_socket(const char* socket_path) {
    if (!socket_path) return -1;

    int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("error: socket creation error");
        return -1;
    }

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(sockfd, (struct sockaddr*) &addr, sizeof(addr)) < 0) {
        close(sockfd);
        return -1;
    }

    return sockfd;
}

char* send_to_socket(const char* command, int* socket_fd, const char* socket_path, const bool reconnect) {
    if (!socket_path) return NULL;

    if (reconnect || *socket_fd < 0) {
        if (*socket_fd >= 0) close(*socket_fd);
        *socket_fd = initialize_socket(socket_path);
        if (*socket_fd < 0) return NULL;
    }

    if (write(*socket_fd, command, strlen(command)) < 0) {
        close(*socket_fd);
        *socket_fd = -1;
        // Retry once on failure
        *socket_fd = initialize_socket(socket_path);
        if (*socket_fd < 0 || write(*socket_fd, command, strlen(command)) < 0) {
            if (*socket_fd >= 0) { close(*socket_fd); *socket_fd = -1; }
            return NULL;
        }
    }

    size_t capacity = 16384;
    size_t total_read = 0;
    char* buffer = malloc(capacity);
    if (!buffer) return NULL;

    while (1) {
        ssize_t n = read(*socket_fd, buffer + total_read, capacity - total_read - 1);
        if (n <= 0) {
            if (total_read == 0) {
                close(*socket_fd);
                *socket_fd = -1;
            }
            break;
        }
        total_read += n;
        buffer[total_read] = '\0';

        if (buffer[total_read - 1] == '\n' || n < 1024) break;

        if (capacity - total_read < 2048) {
            capacity *= 2;
            char* new_buf = realloc(buffer, capacity);
            if (!new_buf) {
                free(buffer);
                return NULL;
            }
            buffer = new_buf;
        }
    }

    if (total_read == 0) {
        free(buffer);
        return NULL;
    }

    return buffer;
}

char* send_to_mpv_socket(const char* command, config_t* config) {
    return send_to_socket(command, &config->mpvpaper_socket_fd, config->mpvpaper_socket_path, false);
}

char* send_to_hyprland_socket(const char* command, config_t* config) {
    if (!config->hyprland_socket_path) config->hyprland_socket_path = get_hyprctl_socket_path();
    return send_to_socket(command, &config->hyprland_socket_fd, config->hyprland_socket_path, false);
}

char* send_to_niri_socket(const char* command, config_t* config) {
    if (!config->niri_socket_path) config->niri_socket_path = get_niri_socket_path();
    return send_to_socket(command, &config->niri_socket_fd, config->niri_socket_path, false);
}

int query_hyprland_windows(config_t* config) {
    char* json_str = send_to_hyprland_socket(QUERY_HYPRLAND_SOCKET_ACTIVE_WORKSPACE, config);
    if (!json_str) {
        log_verbose("failed to query hyprland active workspace", config);
        return -1;
    }

    cJSON* json = cJSON_Parse(json_str);
    free(json_str);

    if (!json) return -1;

    cJSON* json_windows = cJSON_GetObjectItemCaseSensitive(json, "windows");
    int windows = cJSON_IsNumber(json_windows) ? json_windows->valueint : 0;

    cJSON_Delete(json);
    return windows;
}

int query_niri_windows(config_t* config) {
    char* ws_json_str = send_to_niri_socket(QUERY_NIRI_SOCKET_WORKSPACES, config);
    if (!ws_json_str) {
        log_verbose("failed to query niri workspaces", config);
        return -1;
    }

    cJSON* ws_json = cJSON_Parse(ws_json_str);
    free(ws_json_str);
    if (!ws_json) return -1;

    cJSON* ok_obj = cJSON_GetObjectItemCaseSensitive(ws_json, "Ok");
    if (!ok_obj) {
        cJSON_Delete(ws_json);
        return -1;
    }

    cJSON* workspaces_arr = cJSON_GetObjectItemCaseSensitive(ok_obj, "Workspaces");
    if (!workspaces_arr || !cJSON_IsArray(workspaces_arr)) {
        cJSON_Delete(ws_json);
        return -1;
    }

    long long active_ws_ids[32];
    int active_ws_count = 0;
    bool has_any_active_win = false;

    int ws_count = cJSON_GetArraySize(workspaces_arr);
    for (int i = 0; i < ws_count && active_ws_count < 32; i++) {
        cJSON* ws_item = cJSON_GetArrayItem(workspaces_arr, i);
        cJSON* is_active = cJSON_GetObjectItemCaseSensitive(ws_item, "is_active");
        cJSON* is_focused = cJSON_GetObjectItemCaseSensitive(ws_item, "is_focused");
        cJSON* out_item = cJSON_GetObjectItemCaseSensitive(ws_item, "output");

        bool matches_output = true;
        if (config->output_name && strcasecmp(config->output_name, "ALL") != 0) {
            if (out_item && cJSON_IsString(out_item)) {
                matches_output = (strcasecmp(out_item->valuestring, config->output_name) == 0);
            } else {
                matches_output = false;
            }
        }

        if (matches_output) {
            bool ws_is_visible = (is_active && cJSON_IsTrue(is_active)) || (is_focused && cJSON_IsTrue(is_focused));
            if (ws_is_visible) {
                cJSON* id_item = cJSON_GetObjectItemCaseSensitive(ws_item, "id");
                if (id_item && cJSON_IsNumber(id_item)) {
                    active_ws_ids[active_ws_count++] = (long long)id_item->valuedouble;
                    cJSON* active_win = cJSON_GetObjectItemCaseSensitive(ws_item, "active_window_id");
                    if (active_win && !cJSON_IsNull(active_win)) {
                        has_any_active_win = true;
                    }
                }
            }
        }
    }

    cJSON_Delete(ws_json);

    if (active_ws_count == 0) {
        return 0;
    }

    // Fast-path: If none of the active visible workspaces have an active window, all are empty!
    if (!has_any_active_win) {
        return 0;
    }

    char* win_json_str = send_to_niri_socket(QUERY_NIRI_SOCKET_WINDOWS, config);
    if (!win_json_str) {
        return has_any_active_win ? 1 : 0;
    }

    cJSON* win_json = cJSON_Parse(win_json_str);
    free(win_json_str);
    if (!win_json) return has_any_active_win ? 1 : 0;

    cJSON* win_ok = cJSON_GetObjectItemCaseSensitive(win_json, "Ok");
    if (!win_ok) {
        cJSON_Delete(win_json);
        return has_any_active_win ? 1 : 0;
    }

    cJSON* windows_arr = cJSON_GetObjectItemCaseSensitive(win_ok, "Windows");
    if (!windows_arr || !cJSON_IsArray(windows_arr)) {
        cJSON_Delete(win_json);
        return has_any_active_win ? 1 : 0;
    }

    int visible_windows = 0;
    int total_win = cJSON_GetArraySize(windows_arr);
    for (int i = 0; i < total_win; i++) {
        cJSON* win_item = cJSON_GetArrayItem(windows_arr, i);
        cJSON* ws_id_item = cJSON_GetObjectItemCaseSensitive(win_item, "workspace_id");
        if (ws_id_item && cJSON_IsNumber(ws_id_item)) {
            long long w_ws_id = (long long)ws_id_item->valuedouble;
            for (int k = 0; k < active_ws_count; k++) {
                if (w_ws_id == active_ws_ids[k]) {
                    visible_windows++;
                    break;
                }
            }
        }
    }

    cJSON_Delete(win_json);
    return visible_windows;
}

#define MAX_TARGET_MONITORS 16

typedef struct {
    char name[64];
    long long active_workspace_id;
    int window_count;
    char socket_path[256];
    int socket_fd;
    int last_windows;
    int last_paused;
} monitor_entry_t;

static monitor_entry_t g_monitors[MAX_TARGET_MONITORS];
static int g_monitor_count = 0;

static monitor_entry_t* get_or_create_monitor(const char* name, const config_t* config) {
    for (int i = 0; i < g_monitor_count; i++) {
        if (strcmp(g_monitors[i].name, name) == 0) {
            return &g_monitors[i];
        }
    }
    if (g_monitor_count >= MAX_TARGET_MONITORS) return NULL;

    monitor_entry_t* m = &g_monitors[g_monitor_count++];
    strncpy(m->name, name, sizeof(m->name) - 1);
    m->active_workspace_id = -1;
    m->window_count = 0;
    m->socket_fd = -1;
    m->last_windows = -1;
    m->last_paused = -1;

    char try_path[256];
    snprintf(try_path, sizeof(try_path), "/tmp/mpvsocket-%s", name);
    if (access(try_path, F_OK) == 0) {
        strncpy(m->socket_path, try_path, sizeof(m->socket_path) - 1);
    } else {
        snprintf(try_path, sizeof(try_path), "/tmp/mpv-%s", name);
        if (access(try_path, F_OK) == 0) {
            strncpy(m->socket_path, try_path, sizeof(m->socket_path) - 1);
        } else {
            if (config->output_name && strcasecmp(config->output_name, "ALL") != 0) {
                strncpy(m->socket_path, config->mpvpaper_socket_path, sizeof(m->socket_path) - 1);
            } else if (access(config->mpvpaper_socket_path, F_OK) == 0) {
                strncpy(m->socket_path, config->mpvpaper_socket_path, sizeof(m->socket_path) - 1);
            } else {
                snprintf(m->socket_path, sizeof(m->socket_path), "/tmp/mpvsocket-%s", name);
            }
        }
    }
    return m;
}

bool query_socket_pause_status(int* socket_fd, const char* socket_path) {
    char* json_str = send_to_socket(QUERY_MPVPAPER_SOCKET_PAUSE_PROPERTY, socket_fd, socket_path, false);
    if (!json_str) return false;

    cJSON* json = cJSON_Parse(json_str);
    free(json_str);
    if (!json) return false;

    cJSON* json_data = cJSON_GetObjectItemCaseSensitive(json, "data");
    bool paused = cJSON_IsBool(json_data) ? cJSON_IsTrue(json_data) : false;
    cJSON_Delete(json);
    return paused;
}

void update_monitor_playback(monitor_entry_t* mon, config_t* config) {
    // Check if socket path is accessible
    if (access(mon->socket_path, F_OK) != 0) {
        // Try fallback to default socket
        if (access(config->mpvpaper_socket_path, F_OK) == 0) {
            strncpy(mon->socket_path, config->mpvpaper_socket_path, sizeof(mon->socket_path) - 1);
        } else {
            if (mon->socket_fd >= 0) {
                close(mon->socket_fd);
                mon->socket_fd = -1;
            }
            return;
        }
    }

    if (mon->socket_fd < 0) {
        mon->socket_fd = initialize_socket(mon->socket_path);
        if (mon->socket_fd < 0) return;
        char msg[256];
        snprintf(msg, sizeof(msg), "Connected to mpvpaper socket %s for output %s", mon->socket_path, mon->name);
        log_verbose(msg, config);
    }

    bool is_paused = query_socket_pause_status(&mon->socket_fd, mon->socket_path);

    if (mon->window_count == mon->last_windows && (int)is_paused == mon->last_paused) {
        return;
    }

    mon->last_windows = mon->window_count;
    mon->last_paused = is_paused;

    char msg[256];
    snprintf(msg, sizeof(msg), "Output %s [%s]: {windows: %d, paused: %d}", mon->name, mon->socket_path, mon->window_count, is_paused);
    log_verbose(msg, config);

    if (mon->window_count == 0 && is_paused) {
        char resume_msg[128];
        snprintf(resume_msg, sizeof(resume_msg), "Resuming %s (%s)", mon->name, mon->socket_path);
        log_verbose(resume_msg, config);
        char* resp = send_to_socket(SET_MPVPAPER_SOCKET_RESUME, &mon->socket_fd, mon->socket_path, false);
        if (resp) free(resp);
    } else if (mon->window_count > 0 && !is_paused) {
        char pause_msg[128];
        snprintf(pause_msg, sizeof(pause_msg), "Pausing %s (%s)", mon->name, mon->socket_path);
        log_verbose(pause_msg, config);
        char* resp = send_to_socket(SET_MPVPAPER_SOCKET_PAUSE, &mon->socket_fd, mon->socket_path, false);
        if (resp) free(resp);
    }
}

void update_mpv_state(config_t* config) {
    if (config->compositor == COMPOSITOR_NIRI) {
        char* ws_json_str = send_to_niri_socket(QUERY_NIRI_SOCKET_WORKSPACES, config);
        if (!ws_json_str) return;

        cJSON* ws_json = cJSON_Parse(ws_json_str);
        free(ws_json_str);
        if (!ws_json) return;

        cJSON* ok_obj = cJSON_GetObjectItemCaseSensitive(ws_json, "Ok");
        if (!ok_obj) { cJSON_Delete(ws_json); return; }

        cJSON* workspaces_arr = cJSON_GetObjectItemCaseSensitive(ok_obj, "Workspaces");
        if (!workspaces_arr || !cJSON_IsArray(workspaces_arr)) { cJSON_Delete(ws_json); return; }

        int ws_count = cJSON_GetArraySize(workspaces_arr);
        bool any_active_win = false;

        for (int i = 0; i < ws_count; i++) {
            cJSON* ws_item = cJSON_GetArrayItem(workspaces_arr, i);
            cJSON* is_active = cJSON_GetObjectItemCaseSensitive(ws_item, "is_active");
            cJSON* is_focused = cJSON_GetObjectItemCaseSensitive(ws_item, "is_focused");
            cJSON* out_item = cJSON_GetObjectItemCaseSensitive(ws_item, "output");

            const char* out_name = (out_item && cJSON_IsString(out_item)) ? out_item->valuestring : "default";

            if (config->output_name && strcasecmp(config->output_name, "ALL") != 0) {
                if (strcasecmp(config->output_name, out_name) != 0) continue;
            }

            bool ws_is_visible = (is_active && cJSON_IsTrue(is_active)) || (is_focused && cJSON_IsTrue(is_focused));
            if (ws_is_visible) {
                cJSON* id_item = cJSON_GetObjectItemCaseSensitive(ws_item, "id");
                if (id_item && cJSON_IsNumber(id_item)) {
                    monitor_entry_t* mon = get_or_create_monitor(out_name, config);
                    if (mon) {
                        mon->active_workspace_id = (long long)id_item->valuedouble;
                        mon->window_count = 0;
                        cJSON* active_win = cJSON_GetObjectItemCaseSensitive(ws_item, "active_window_id");
                        if (active_win && !cJSON_IsNull(active_win)) {
                            any_active_win = true;
                            mon->window_count = 1;
                        }
                    }
                }
            }
        }
        cJSON_Delete(ws_json);

        // If there are active windows, query precise counts per workspace
        if (any_active_win) {
            char* win_json_str = send_to_niri_socket(QUERY_NIRI_SOCKET_WINDOWS, config);
            if (win_json_str) {
                cJSON* win_json = cJSON_Parse(win_json_str);
                free(win_json_str);
                if (win_json) {
                    cJSON* win_ok = cJSON_GetObjectItemCaseSensitive(win_json, "Ok");
                    cJSON* windows_arr = win_ok ? cJSON_GetObjectItemCaseSensitive(win_ok, "Windows") : NULL;
                    if (windows_arr && cJSON_IsArray(windows_arr)) {
                        for (int m = 0; m < g_monitor_count; m++) {
                            g_monitors[m].window_count = 0;
                        }
                        int total_win = cJSON_GetArraySize(windows_arr);
                        for (int i = 0; i < total_win; i++) {
                            cJSON* win_item = cJSON_GetArrayItem(windows_arr, i);
                            cJSON* ws_id_item = cJSON_GetObjectItemCaseSensitive(win_item, "workspace_id");
                            if (ws_id_item && cJSON_IsNumber(ws_id_item)) {
                                long long w_ws_id = (long long)ws_id_item->valuedouble;
                                for (int m = 0; m < g_monitor_count; m++) {
                                    if (g_monitors[m].active_workspace_id == w_ws_id) {
                                        g_monitors[m].window_count++;
                                    }
                                }
                            }
                        }
                    }
                    cJSON_Delete(win_json);
                }
            }
        }

        for (int m = 0; m < g_monitor_count; m++) {
            update_monitor_playback(&g_monitors[m], config);
        }
    } else {
        // Hyprland mode
        int windows = query_hyprland_windows(config);
        if (windows >= 0) {
            monitor_entry_t* mon = get_or_create_monitor("default", config);
            if (mon) {
                mon->window_count = windows;
                update_monitor_playback(mon, config);
            }
        }
    }
}

void fork_if(const bool flag) {
    if (!flag) return;

    int pid = fork();
    if (pid < 0) {
        perror("error: fork failed");
        exit(EXIT_FAILURE);
    }

    if (pid > 0) exit(EXIT_SUCCESS);

    if (setsid() < 0) {
        perror("error: setsid failed");
        exit(EXIT_FAILURE);
    }
}

void validate_period(int period) {
    if (period <= 0) {
        fprintf(stderr, "error: period must be greater than 0\n");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char **argv) {
    int opt;
    struct option long_options[] = {
        {"help", no_argument, NULL, 'h'},
        {"period", required_argument, NULL, 't'},
        {"socket-path", required_argument, NULL, 'p'},
        {"output", required_argument, NULL, 'o'},
        {"compositor", required_argument, NULL, 'C'},
        {"fork", no_argument, NULL, 'f'},
        {"verbose", no_argument, NULL, 'v'},
        {"pywal", no_argument, NULL, 1001},
        {"matugen", no_argument, NULL, 1002},
        {"socket-wait-time", required_argument, NULL, 'w'},
        {0, 0, 0, 0}
    };

    config_t config;
    init_config(&config);

    while ((opt = getopt_long(argc, argv, "hvfp:o:c:t:w:C:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'h':
                print_help(argv[0]);
                return 0;
            case 'f':
                config.fork_process = true;
                break;
            case 'v':
                config.verbose = true;
                break;
            case 'o':
                config.output_name = optarg;
                break;
            case 'p':
                config.mpvpaper_socket_path = optarg;
                break;
            case 't':
                config.polling_period = atoi(optarg);
                break;
            case 'w':
                config.socket_wait_time = atoi(optarg);
                break;
            case 'C':
                if (strcasecmp(optarg, "niri") == 0) config.compositor = COMPOSITOR_NIRI;
                else if (strcasecmp(optarg, "hyprland") == 0) config.compositor = COMPOSITOR_HYPRLAND;
                else config.compositor = COMPOSITOR_AUTO;
                break;
            case 'c':
                if (strcmp(optarg, "pywal") == 0) config.do_pywal = true;
                if (strcmp(optarg, "matugen") == 0) config.do_matugen = true;
                break;
            case 1001: 
                config.do_pywal = true;
                break;
            case 1002: 
                config.do_matugen = true;
                break;
            default:
                print_help(argv[0]);
                exit(EXIT_SUCCESS);
        }
    }

    validate_period(config.polling_period);
    detect_compositor(&config);
    fork_if(config.fork_process);

    int event_sockfd = -1;
    char* event_socket_path = NULL;

    if (config.compositor == COMPOSITOR_HYPRLAND) {
        config.hyprland_socket_fd = initialize_socket(config.hyprland_socket_path);
        event_socket_path = get_hyprctl_event_socket_path();
        if (event_socket_path) {
            event_sockfd = initialize_socket(event_socket_path);
        }
    } else if (config.compositor == COMPOSITOR_NIRI) {
        config.niri_socket_fd = initialize_socket(config.niri_socket_path);
        event_sockfd = initialize_socket(config.niri_socket_path);
        if (event_sockfd >= 0) {
            const char* ev_cmd = "\"EventStream\"\n";
            if (write(event_sockfd, ev_cmd, strlen(ev_cmd)) < 0) {
                close(event_sockfd);
                event_sockfd = -1;
            }
        }
    }

    log_verbose("Starting monitoring loop", &config);

    fd_set readfds;
    struct timeval tv;
    char buffer[4096];

    update_mpv_state(&config);

    while (1) {
        FD_ZERO(&readfds);
        if (event_sockfd >= 0) {
            FD_SET(event_sockfd, &readfds);
        }

        tv.tv_sec = config.polling_period / 1000;
        tv.tv_usec = (config.polling_period % 1000) * 1000;

        int max_fd = event_sockfd >= 0 ? event_sockfd : 0;
        int activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);

        if (activity < 0) {
            if (errno == EINTR) continue;
            usleep(100000);
            continue;
        }

        bool should_update = false;

        if (activity == 0) {
            should_update = true;
        } else if (event_sockfd >= 0 && FD_ISSET(event_sockfd, &readfds)) {
            ssize_t valread = read(event_sockfd, buffer, sizeof(buffer) - 1);
            if (valread > 0) {
                buffer[valread] = '\0';
                should_update = true;
            } else {
                close(event_sockfd);
                event_sockfd = -1;
                if (config.compositor == COMPOSITOR_HYPRLAND && event_socket_path) {
                    event_sockfd = initialize_socket(event_socket_path);
                } else if (config.compositor == COMPOSITOR_NIRI && config.niri_socket_path) {
                    event_sockfd = initialize_socket(config.niri_socket_path);
                    if (event_sockfd >= 0) {
                        const char* ev_cmd = "\"EventStream\"\n";
                        if (write(event_sockfd, ev_cmd, strlen(ev_cmd)) < 0) {
                            close(event_sockfd);
                            event_sockfd = -1;
                        }
                    }
                }
            }
        }

        if (should_update) {
            update_mpv_state(&config);
        }
    }

    return 0;
}