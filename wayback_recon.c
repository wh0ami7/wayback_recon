/*
 * wayback_recon.c
 * Bug Bounty Recon Tool using Internet Archive Wayback Machine CDX Server
 *
 * COMPILE:
 *   gcc -std=c23 -O3 -march=native -mtune=native -flto -pipe \
 *       -fomit-frame-pointer -falign-functions=32 -fno-plt -ffast-math \
 *       -static-libgcc -static-libstdc++ \
 *       -Wl,-O1 -Wl,--as-needed -Wl,--strip-all \
 *       -o waybackrecon wayback_recon.c -lcurl -ljansson
 *
 * Source: https://archive.org/developers/wayback-cdx-server.html
 * Version: 1.9.12 | Author: Izzy
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <jansson.h>

_Static_assert(sizeof(char) == 1, "Platform must have 8-bit char");
_Static_assert(__STDC_VERSION__ >= 202311L, "C23 or later required");

#define MAX_URL_LEN 2048
#define MAX_PARAM_LEN 512
#define INITIAL_CAPACITY 1024
#define MAX_LINE_LEN 4096
#define MAX_DOMAIN_LEN 253  // RFC 1035

static inline int max(int a, int b) { return a > b ? a : b; }

typedef struct {
    char *data;
    size_t size;
} MemoryChunk;

typedef struct {
    char **urls;
    int count;
    int capacity;
} URLSet;

typedef struct {
    char *url;
    char *method;
    char **params;
    int param_count;
} Endpoint;

[[nodiscard]] const char *infer_method(const char *url, const char *mimetype);
[[nodiscard]] int add_url(URLSet *set, const char *url);
void print_help(const char *prog_name);
int compare_endpoints_asc(const void *a, const void *b);
int compare_endpoints_desc(const void *a, const void *b);
void free_endpoint(Endpoint *e);
[[nodiscard]] int process_domain(const char *domain, const char *output_file, long limit, long timeout, int verbose, int sort_desc);
static void safe_strcpy(char *dest, const char *src, size_t dest_size);
static size_t safe_strncpy(char *dest, const char *src, size_t dest_size);
static char *safe_strtok(char *str, const char *delim, char **saveptr);

void print_help(const char *prog_name) {
    printf(
"Usage: %s [OPTIONS] [domain]\n"
"       cat domains.txt | %s [OPTIONS]\n"
"\n"
"Recon tool that queries the Internet Archive CDX Server and outputs\n"
"endpoints in **JSON format** (machine-readable).\n"
"\n"
"Input:\n"
"  domain                Target domain (e.g., example.com)\n"
"  -                     Read domains from stdin (pipe)\n"
"\n"
"Options:\n"
"  -h, --help            Show this help message and exit\n"
"  -o, --output FILE     Output JSON file (default: endpoints.json)\n"
"  -l, --limit N         Max results per query (1150000, default: 100000)\n"
"  -t, --timeout SEC     Curl timeout in seconds (default: 60)\n"
"  -v, --verbose         Show query URLs\n"
"  -s, --sort ORDER      Sort order: asc (default), desc\n"
"\n"
"Examples:\n"
"  %s example.com\n"
"  echo \"google.com\" | %s\n"
"  cat domains.txt | %s -o all.json\n"
"  %s -s desc target.com\n"
"\n"
"Output (endpoints.json):\n"
"  [\n"
"    {\"url\": \"https://example.com/login\", \"method\": \"POST\", \"parameters\": [\"username\", \"password\"]},\n"
"    ...\n"
"  ]\n"
"\n"
"Source: https://archive.org/developers/wayback-cdx-server.html\n"
"Version: 1.9.12 (C23)\n",
        prog_name, prog_name, prog_name, prog_name, prog_name, prog_name
    );
}

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    const size_t realsize = size * nmemb;
    MemoryChunk *mem = (MemoryChunk *)userp;

    char *ptr = realloc(mem->data, mem->size + realsize + 1);
    if (!ptr) {
        fprintf(stderr, "realloc() failed\n");
        return 0;
    }

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = '\0';

    return realsize;
}

[[nodiscard]] int add_url(URLSet *set, const char *url) {
    if (!url || url[0] == '\0') return 0;

    for (int i = 0; i < set->count; ++i) {
        if (strcmp(set->urls[i], url) == 0) return 0;
    }

    if (set->count == set->capacity) {
        set->capacity = max(set->capacity * 2, INITIAL_CAPACITY);
        char **new_urls = realloc(set->urls, set->capacity * sizeof *new_urls);
        if (!new_urls) { perror("realloc"); exit(EXIT_FAILURE); }
        set->urls = new_urls;
    }

    set->urls[set->count] = strdup(url);
    if (!set->urls[set->count]) { perror("strdup"); exit(EXIT_FAILURE); }
    ++set->count;
    return 1;
}

[[nodiscard]] const char *infer_method(const char *url, const char *mimetype) {
    if (!url) return "GET";
    const char *m = mimetype ? mimetype : "";

    int has_post = strstr(url, "login") || strstr(url, "submit") || strstr(url, "upload") ||
                   strstr(url, "create") || strstr(url, "update") || strstr(url, "delete") ||
                   strstr(url, "api") || strstr(url, "json") || strstr(url, "graphql") ||
                   strstr(m, "json") || strstr(m, "xml") || strstr(m, "form");

    if (has_post) {
        if (strstr(url, "update") || strstr(url, "patch")) return "PUT";
        if (strstr(url, "delete") || strstr(url, "remove")) return "DELETE";
        return "POST";
    }
    return "GET";
}

int compare_endpoints_asc(const void *a, const void *b) {
    const Endpoint *ea = (const Endpoint *)a;
    const Endpoint *eb = (const Endpoint *)b;
    return strcmp(ea->url, eb->url);
}

int compare_endpoints_desc(const void *a, const void *b) {
    const Endpoint *ea = (const Endpoint *)a;
    const Endpoint *eb = (const Endpoint *)b;
    return strcmp(eb->url, ea->url);
}

void free_endpoint(Endpoint *e) {
    free(e->url);
    free(e->method);
    for (int i = 0; i < e->param_count; ++i) free(e->params[i]);
    if (e->params) free(e->params);
}

static void safe_strcpy(char *dest, const char *src, size_t dest_size) {
    size_t i = 0;
    while (i < dest_size - 1 && src[i]) {
        dest[i] = src[i];
        ++i;
    }
    dest[i] = '\0';
}

static size_t safe_strncpy(char *dest, const char *src, size_t dest_size) {
    size_t i = 0;
    while (i < dest_size - 1 && src[i]) {
        dest[i] = src[i];
        ++i;
    }
    dest[i] = '\0';
    return i;
}

static char *safe_strtok(char *str, const char *delim, char **saveptr) {
    if (!str) str = *saveptr;
    if (!str || *str == '\0') return NULL;
    str += strspn(str, delim);
    if (*str == '\0') return NULL;
    char *end = str + strcspn(str, delim);
    if (*end != '\0') *end++ = '\0';
    *saveptr = end;
    return str;
}

[[nodiscard]] int process_domain(const char *domain, const char *output_file, long limit, long timeout, int verbose, int sort_desc) {
    if (!domain || domain[0] == '\0' || strlen(domain) > MAX_DOMAIN_LEN) {
        fprintf(stderr, "Invalid domain: empty or too long\n");
        return 1;
    }

    char full_domain[MAX_DOMAIN_LEN + 8];
    if (strstr(domain, "://") == NULL) {
        snprintf(full_domain, sizeof(full_domain), "http://%s", domain);
    } else {
        safe_strcpy(full_domain, domain, sizeof(full_domain));
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "curl_easy_init() failed\n");
        return 1;
    }

    char *resume_key = NULL;
    MemoryChunk chunk = {0};
    URLSet seen = {0};

    Endpoint *endpoints = NULL;
    int endpoint_count = 0;
    int endpoint_capacity = 0;

    do {
        char url[MAX_URL_LEN];
        if (resume_key) {
            snprintf(url, sizeof(url),
                     "http://web.archive.org/cdx/search/cdx?"
                     "url=%s&matchType=domain&fl=original,timestamp,statuscode,mimetype&"
                     "collapse=urlkey&output=json&limit=%ld&resumeKey=%s",
                     full_domain, limit, resume_key);
            free(resume_key);
            resume_key = NULL;
        } else {
            snprintf(url, sizeof(url),
                     "http://web.archive.org/cdx/search/cdx?"
                     "url=%s&matchType=domain&fl=original,timestamp,statuscode,mimetype&"
                     "collapse=urlkey&output=json&limit=%ld&showResumeKey=true",
                     full_domain, limit);
        }

        if (verbose) {
            printf("Querying: %s\n", url);
            fflush(stdout);
        }

        chunk.data = NULL; chunk.size = 0;

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "WaybackRecon/1.9.12");
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "curl error for %s: %s\n", domain, curl_easy_strerror(res));
            break;
        }
        if (!chunk.data || chunk.size == 0) break;

        json_error_t error;
        json_t *root = json_loads(chunk.data, 0, &error);
        free(chunk.data); chunk.data = NULL;

        if (!root) {
            fprintf(stderr, "JSON parse error for %s: %s\n", domain, error.text);
            break;
        }
        if (!json_is_array(root) || json_array_size(root) < 2) {
            json_decref(root);
            break;
        }

        for (size_t i = 1; i < json_array_size(root); ++i) {
            json_t *row = json_array_get(root, i);
            if (!json_is_array(row) || json_array_size(row) < 4) continue;

            const char *original = json_string_value(json_array_get(row, 0));
            const char *mimetype = json_string_value(json_array_get(row, 3));
            if (!original) continue;

            if (add_url(&seen, original)) {
                const char *method = infer_method(original, mimetype);

                char **params = NULL;
                int param_count = 0;
                const char *qmark = strchr(original, '?');
                if (qmark) {
                    char query[MAX_PARAM_LEN];
                    safe_strncpy(query, qmark + 1, sizeof(query));

                    char *saveptr = NULL;
                    char *token = safe_strtok(query, "&", &saveptr);
                    while (token) {
                        char *eq = strchr(token, '=');
                        if (eq) *eq = '\0';
                        if (token[0] != '\0') {
                            params = realloc(params, (param_count + 1) * sizeof *params);
                            if (!params) { perror("realloc"); exit(1); }
                            params[param_count++] = strdup(token);
                            if (!params[param_count - 1]) { perror("strdup"); exit(1); }
                        }
                        token = safe_strtok(NULL, "&", &saveptr);
                    }
                }

                printf("%s | %s | ", original, method);
                if (param_count == 0) printf("none\n");
                else {
                    for (int j = 0; j < param_count; ++j) {
                        printf("%s%s", params[j], j < param_count - 1 ? ", " : "\n");
                    }
                }
                fflush(stdout);

                if (endpoint_count == endpoint_capacity) {
                    endpoint_capacity = max(endpoint_capacity * 2, INITIAL_CAPACITY);
                    endpoints = realloc(endpoints, endpoint_capacity * sizeof *endpoints);
                    if (!endpoints) { perror("realloc"); exit(1); }
                }

                endpoints[endpoint_count].url = strdup(original);
                if (!endpoints[endpoint_count].url) { perror("strdup"); exit(1); }
                endpoints[endpoint_count].method = strdup(method);
                if (!endpoints[endpoint_count].method) { perror("strdup"); exit(1); }
                endpoints[endpoint_count].params = params;
                endpoints[endpoint_count].param_count = param_count;
                ++endpoint_count;
            }
        }

        json_t *last_row = json_array_get(root, json_array_size(root) - 1);
        if (json_is_array(last_row) && json_array_size(last_row) > 0) {
            json_t *last_cell = json_array_get(last_row, json_array_size(last_row) - 1);
            const char *key = json_string_value(last_cell);
            if (key && key[0] != '\0' && strcmp(key, "null") != 0) {
                resume_key = strdup(key);
                if (!resume_key) { perror("strdup"); exit(1); }
            }
        }

        json_decref(root);

    } while (resume_key);

    curl_easy_cleanup(curl);
    for (int i = 0; i < seen.count; ++i) free(seen.urls[i]);
    if (seen.urls) free(seen.urls);

    if (endpoint_count > 0) {
        qsort(endpoints, endpoint_count, sizeof *endpoints,
              sort_desc ? compare_endpoints_desc : compare_endpoints_asc);
    }

    FILE *fp = fopen(output_file, "w");
    if (!fp) { perror("fopen"); exit(1); }

    fprintf(fp, "[\n");
    for (int i = 0; i < endpoint_count; ++i) {
        json_t *obj = json_object();
        json_object_set_new(obj, "url", json_string(endpoints[i].url));
        json_object_set_new(obj, "method", json_string(endpoints[i].method));

        json_t *params_array = json_array();
        for (int j = 0; j < endpoints[i].param_count; ++j) {
            json_array_append_new(params_array, json_string(endpoints[i].params[j]));
        }
        json_object_set_new(obj, "parameters", params_array);

        char *json_str = json_dumps(obj, JSON_INDENT(2) | JSON_ENSURE_ASCII);
        if (json_str) {
            if (i > 0) fprintf(fp, ",\n");
            fprintf(fp, "%s", json_str);
            free(json_str);
        }
        json_decref(obj);
    }
    fprintf(fp, "\n]\n");
    fclose(fp);

    for (int i = 0; i < endpoint_count; ++i) free_endpoint(&endpoints[i]);
    free(endpoints);

    if (!verbose) {
        printf("\nRecon complete for %s. JSON output saved to %s\n", domain, output_file);
    }
    return 0;
}

int main(int argc, char *argv[]) {
    const char *output_file = "endpoints.json";
    long limit = 100000;
    long timeout = 60;
    int verbose = 0;
    int sort_desc = 0;
    const char *domain = NULL;

    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (++i >= argc) { fprintf(stderr, "Error: --output requires a filename\n"); return 1; }
            output_file = argv[i];
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--limit") == 0) {
            if (++i >= argc) { fprintf(stderr, "Error: --limit is required\n"); return 1; }
            limit = atol(argv[i]);
            if (limit <= 0 || limit > 150000) {
                fprintf(stderr, "Error: limit must be 1150000\n"); return 1;
            }
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--timeout") == 0) {
            if (++i >= argc) { fprintf(stderr, "Error: --timeout requires seconds\n"); return 1; }
            timeout = atol(argv[i]);
            if (timeout <= 0) { fprintf(stderr, "Error: timeout must be > 0\n"); return 1; }
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--sort") == 0) {
            if (++i >= argc) { fprintf(stderr, "Error: --sort requires asc/desc\n"); return 1; }
            if (strcmp(argv[i], "desc") == 0) sort_desc = 1;
            else if (strcmp(argv[i], "asc") == 0) sort_desc = 0;
            else {
                fprintf(stderr, "Error: --sort must be asc or desc\n"); return 1;
            }
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_help(argv[0]);
            return 1;
        } else {
            if (domain != NULL) {
                fprintf(stderr, "Error: Only one domain allowed\n");
                return 1;
            }
            domain = argv[i];
        }
        ++i;
    }

    if (argc == 1 || (argc > 1 && strcmp(argv[1], "-") == 0)) {
        char line[MAX_LINE_LEN];
        while (fgets(line, sizeof(line), stdin)) {
            size_t len = strcspn(line, "\r\n");
            if (len == 0) continue;
            char domain_buf[MAX_LINE_LEN];
            safe_strncpy(domain_buf, line, len + 1);

            if (strlen(domain_buf) == 0 || strlen(domain_buf) > MAX_DOMAIN_LEN) {
                fprintf(stderr, "Invalid domain: empty or too long\n");
                continue;
            }

            printf("\n=== Processing: %s ===\n", domain_buf);
            if (process_domain(domain_buf, output_file, limit, timeout, verbose, sort_desc) != 0) {
                fprintf(stderr, "Failed to process %s\n", domain_buf);
            }
        }
        return 0;
    }

    if (domain == NULL) {
        fprintf(stderr, "Error: Domain is required (or use pipe input)\n");
        print_help(argv[0]);
        return 1;
    }

    if (strlen(domain) == 0 || strlen(domain) > MAX_DOMAIN_LEN) {
        fprintf(stderr, "Invalid domain: empty or too long\n");
        return 1;
    }

    return process_domain(domain, output_file, limit, timeout, verbose, sort_desc);
}
