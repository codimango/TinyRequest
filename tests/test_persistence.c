/* Tests for the persistence module.
 *
 * The persistence module writes to $HOME/.config/tinyrequest. To keep tests
 * hermetic, setUp() points $HOME at a per-process temporary directory and
 * tearDown() removes it. Run order is independent.
 */

#include "unity.h"
#include "persistence.h"
#include "request_response.h"
#include "collections.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <unistd.h>

static char g_tmp_home[1024];
static char *g_saved_home = NULL;

static int rmrf(const char *path) {
    DIR *d = opendir(path);
    if (!d) return remove(path);
    struct dirent *e;
    char child[2048];
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        rmrf(child);
    }
    closedir(d);
    return rmdir(path);
}

void setUp(void) {
    /* Per-test isolated $HOME so config dir is throwaway. */
    snprintf(g_tmp_home, sizeof(g_tmp_home),
        "/tmp/tinyrequest_test_%d_%ld", getpid(), (long)random());
    mkdir(g_tmp_home, 0700);

    /* persistence_create_config_dir() expects $HOME/.config to already exist
       (it only mkdir's the leaf "tinyrequest" directory). */
    char parent[1200];
    snprintf(parent, sizeof(parent), "%s/.config", g_tmp_home);
    mkdir(parent, 0700);

    const char *prev = getenv("HOME");
    g_saved_home = prev ? strdup(prev) : NULL;
    setenv("HOME", g_tmp_home, 1);
}

void tearDown(void) {
    rmrf(g_tmp_home);
    if (g_saved_home) {
        setenv("HOME", g_saved_home, 1);
        free(g_saved_home);
        g_saved_home = NULL;
    } else {
        unsetenv("HOME");
    }
}

/* -------- config path helpers -------- */

static void test_create_config_dir(void) {
    TEST_ASSERT_EQUAL_INT(0, persistence_create_config_dir());

    /* Calling again is idempotent (returns 0 because dir already exists and is
       a directory). */
    TEST_ASSERT_EQUAL_INT(0, persistence_create_config_dir());

    char expected[1100];
    snprintf(expected, sizeof(expected), "%s/.config/tinyrequest", g_tmp_home);
    struct stat st;
    TEST_ASSERT_EQUAL_INT(0, stat(expected, &st));
    TEST_ASSERT_TRUE(S_ISDIR(st.st_mode));
}

static void test_get_config_path(void) {
    char *p = persistence_get_config_path("foo.json");
    TEST_ASSERT_NOT_NULL(p);
    /* Must end with .config/tinyrequest/foo.json */
    const char *suffix = "/.config/tinyrequest/foo.json";
    size_t plen = strlen(p);
    size_t slen = strlen(suffix);
    TEST_ASSERT_TRUE(plen >= slen);
    TEST_ASSERT_EQUAL_STRING(suffix, p + plen - slen);
    free(p);

    TEST_ASSERT_NULL(persistence_get_config_path(NULL));
}

static void test_file_exists(void) {
    TEST_ASSERT_FALSE(persistence_file_exists("/nonexistent/path/123"));
    TEST_ASSERT_FALSE(persistence_file_exists(NULL));

    /* Create a real file and check */
    persistence_create_config_dir();
    char *p = persistence_get_config_path("exists.txt");
    FILE *f = fopen(p, "w");
    TEST_ASSERT_NOT_NULL(f);
    fputs("hi", f);
    fclose(f);
    TEST_ASSERT_TRUE(persistence_file_exists(p));
    free(p);
}

/* -------- request save/load round-trip -------- */

static void test_save_and_load_request_roundtrip(void) {
    Request original;
    request_init(&original);
    strcpy(original.method, "POST");
    strcpy(original.url, "https://example.com/v1/users");
    header_list_add(&original.headers, "Content-Type", "application/json");
    header_list_add(&original.headers, "X-Trace", "abc-123");
    const char *body = "{\"username\":\"alice\"}";
    request_set_body(&original, body, strlen(body));

    TEST_ASSERT_EQUAL_INT(0, persistence_save_request(&original, "My Request", "myreq.json"));

    Request loaded;
    request_init(&loaded);
    TEST_ASSERT_EQUAL_INT(0, persistence_load_request(&loaded, "myreq.json"));

    TEST_ASSERT_EQUAL_STRING("POST", loaded.method);
    TEST_ASSERT_EQUAL_STRING("https://example.com/v1/users", loaded.url);
    TEST_ASSERT_EQUAL_INT(2, loaded.headers.count);
    TEST_ASSERT_EQUAL_INT(0, header_list_find(&loaded.headers, "Content-Type"));
    TEST_ASSERT_EQUAL_INT(1, header_list_find(&loaded.headers, "X-Trace"));
    TEST_ASSERT_NOT_NULL(loaded.body);
    TEST_ASSERT_EQUAL_STRING(body, loaded.body);

    request_cleanup(&original);
    request_cleanup(&loaded);
}

static void test_load_request_missing_file(void) {
    Request r;
    request_init(&r);
    TEST_ASSERT_EQUAL_INT(-1, persistence_load_request(&r, "no_such.json"));
    request_cleanup(&r);
}

static void test_save_load_rejects_null(void) {
    Request r;
    request_init(&r);
    TEST_ASSERT_EQUAL_INT(-1, persistence_save_request(NULL, "n", "f"));
    TEST_ASSERT_EQUAL_INT(-1, persistence_save_request(&r, NULL, "f"));
    TEST_ASSERT_EQUAL_INT(-1, persistence_save_request(&r, "n", NULL));
    TEST_ASSERT_EQUAL_INT(-1, persistence_load_request(NULL, "f"));
    TEST_ASSERT_EQUAL_INT(-1, persistence_load_request(&r, NULL));
    request_cleanup(&r);
}

/* -------- settings round-trip -------- */

static void test_save_and_load_settings(void) {
    /* save_settings does not create the config dir itself. */
    TEST_ASSERT_EQUAL_INT(0, persistence_create_config_dir());

    CollectionManager *m = collection_manager_create();
    TEST_ASSERT_EQUAL_INT(0, persistence_save_settings(m, true, 120));

    bool auto_save = false;
    int interval = 0;
    TEST_ASSERT_EQUAL_INT(0, persistence_load_settings(&auto_save, &interval));
    TEST_ASSERT_TRUE(auto_save);
    TEST_ASSERT_EQUAL_INT(120, interval);

    /* round-trip with the other boolean */
    TEST_ASSERT_EQUAL_INT(0, persistence_save_settings(m, false, 30));
    TEST_ASSERT_EQUAL_INT(0, persistence_load_settings(&auto_save, &interval));
    TEST_ASSERT_FALSE(auto_save);
    TEST_ASSERT_EQUAL_INT(30, interval);

    collection_manager_destroy(m);
}

/* -------- error string helpers -------- */

static void test_persistence_error_string(void) {
    TEST_ASSERT_EQUAL_STRING("Success",
        persistence_error_string(PERSISTENCE_SUCCESS));
    TEST_ASSERT_NOT_NULL(persistence_error_string(PERSISTENCE_ERROR_FILE_NOT_FOUND));
    TEST_ASSERT_NOT_NULL(persistence_error_string(PERSISTENCE_ERROR_INVALID_JSON));
    /* unknown enum still returns something */
    TEST_ASSERT_NOT_NULL(persistence_error_string((PersistenceError)9999));
}

static void test_persistence_user_friendly_error(void) {
    const char *msg = persistence_get_user_friendly_error(
        PERSISTENCE_ERROR_PERMISSION_DENIED, "save collection");
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_TRUE(strstr(msg, "save collection") != NULL);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_create_config_dir);
    RUN_TEST(test_get_config_path);
    RUN_TEST(test_file_exists);
    RUN_TEST(test_save_and_load_request_roundtrip);
    RUN_TEST(test_load_request_missing_file);
    RUN_TEST(test_save_load_rejects_null);
    RUN_TEST(test_save_and_load_settings);
    RUN_TEST(test_persistence_error_string);
    RUN_TEST(test_persistence_user_friendly_error);
    return UNITY_END();
}
