#include "unity.h"
#include "http_client.h"
#include "request_response.h"

#include <curl/curl.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* -------- URL validation -------- */

static void test_validate_url_accepts_valid(void) {
    TEST_ASSERT_EQUAL_INT(0, http_client_validate_url("http://example.com"));
    TEST_ASSERT_EQUAL_INT(0, http_client_validate_url("https://example.com/path?q=1"));
    TEST_ASSERT_EQUAL_INT(0, http_client_validate_url("https://api.example.com:8443/v1"));
    TEST_ASSERT_EQUAL_INT(0, http_client_validate_url("http://localhost:8080"));
    TEST_ASSERT_EQUAL_INT(0, http_client_validate_url("http://127.0.0.1/index"));
}

static void test_validate_url_rejects_invalid(void) {
    /* NULL / empty */
    TEST_ASSERT_TRUE(http_client_validate_url(NULL) != 0);
    TEST_ASSERT_TRUE(http_client_validate_url("") != 0);
    /* Missing protocol */
    TEST_ASSERT_TRUE(http_client_validate_url("example.com") != 0);
    /* Wrong protocol */
    TEST_ASSERT_TRUE(http_client_validate_url("ftp://example.com") != 0);
    /* No domain after protocol */
    TEST_ASSERT_TRUE(http_client_validate_url("http://") != 0);
    /* Whitespace */
    TEST_ASSERT_TRUE(http_client_validate_url("http://exam ple.com") != 0);
    TEST_ASSERT_TRUE(http_client_validate_url("http://example.com/\nfoo") != 0);
    /* Domain with no dot and not localhost */
    TEST_ASSERT_TRUE(http_client_validate_url("http://nodot") != 0);
}

static void test_validate_url_rejects_too_long(void) {
    char buf[2100];
    memcpy(buf, "https://", 8);
    memset(buf + 8, 'a', sizeof(buf) - 9);
    buf[sizeof(buf) - 1] = '\0';
    TEST_ASSERT_TRUE(http_client_validate_url(buf) != 0);
}

/* -------- headers_to_curl_list -------- */

static int slist_count(const struct curl_slist *list) {
    int n = 0;
    for (; list; list = list->next) n++;
    return n;
}

static int slist_contains(const struct curl_slist *list, const char *needle) {
    for (; list; list = list->next) {
        if (list->data && strcmp(list->data, needle) == 0) return 1;
    }
    return 0;
}

static void test_headers_to_curl_list_empty_and_null(void) {
    TEST_ASSERT_NULL(headers_to_curl_list(NULL));

    HeaderList empty;
    header_list_init(&empty);
    TEST_ASSERT_NULL(headers_to_curl_list(&empty));
    header_list_cleanup(&empty);
}

static void test_headers_to_curl_list_formats_entries(void) {
    HeaderList list;
    header_list_init(&list);
    header_list_add(&list, "Content-Type", "application/json");
    header_list_add(&list, "Accept", "*/*");
    header_list_add(&list, "X-Token", "abc");

    struct curl_slist *out = headers_to_curl_list(&list);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL_INT(3, slist_count(out));
    TEST_ASSERT_TRUE(slist_contains(out, "Content-Type: application/json"));
    TEST_ASSERT_TRUE(slist_contains(out, "Accept: */*"));
    TEST_ASSERT_TRUE(slist_contains(out, "X-Token: abc"));

    curl_list_free(out);
    /* freeing NULL must be a no-op */
    curl_list_free(NULL);
    header_list_cleanup(&list);
}

/* -------- HttpClient lifecycle -------- */

static void test_http_client_create_destroy(void) {
    /* curl_global_init is needed before any handle is created. */
    curl_global_init(CURL_GLOBAL_DEFAULT);

    HttpClient *c = http_client_create();
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_NOT_NULL(c->curl_handle);

    /* SSL settings can be tweaked safely. */
    http_client_set_ssl_verification(c, 0, 0);
    TEST_ASSERT_EQUAL_INT(0, c->ssl_verify_peer);
    TEST_ASSERT_EQUAL_INT(0, c->ssl_verify_host);

    http_client_set_max_response_size(c, 2048);
    TEST_ASSERT_EQUAL_size_t(2048, c->max_response_size);

    http_client_destroy(c);
    http_client_destroy(NULL); /* safe */

    curl_global_cleanup();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_validate_url_accepts_valid);
    RUN_TEST(test_validate_url_rejects_invalid);
    RUN_TEST(test_validate_url_rejects_too_long);
    RUN_TEST(test_headers_to_curl_list_empty_and_null);
    RUN_TEST(test_headers_to_curl_list_formats_entries);
    RUN_TEST(test_http_client_create_destroy);
    return UNITY_END();
}
