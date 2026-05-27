#include "unity.h"
#include "request_response.h"

#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* -------- HeaderList -------- */

static void test_header_list_create_destroy(void) {
    HeaderList *list = header_list_create();
    TEST_ASSERT_NOT_NULL(list);
    TEST_ASSERT_EQUAL_INT(0, list->count);
    TEST_ASSERT_EQUAL_INT(0, list->capacity);
    TEST_ASSERT_NULL(list->headers);
    header_list_destroy(list);

    /* destroying NULL is a no-op */
    header_list_destroy(NULL);
}

static void test_header_list_add_and_find(void) {
    HeaderList list;
    header_list_init(&list);

    TEST_ASSERT_EQUAL_INT(0, header_list_add(&list, "Content-Type", "application/json"));
    TEST_ASSERT_EQUAL_INT(0, header_list_add(&list, "X-Token", "abc123"));
    TEST_ASSERT_EQUAL_INT(2, list.count);
    TEST_ASSERT_TRUE(list.capacity >= 2);

    TEST_ASSERT_EQUAL_INT(0, header_list_find(&list, "Content-Type"));
    TEST_ASSERT_EQUAL_INT(1, header_list_find(&list, "X-Token"));
    TEST_ASSERT_EQUAL_INT(-1, header_list_find(&list, "Missing"));

    header_list_cleanup(&list);
}

static void test_header_list_add_rejects_invalid(void) {
    HeaderList list;
    header_list_init(&list);

    TEST_ASSERT_EQUAL_INT(-1, header_list_add(NULL, "a", "b"));
    TEST_ASSERT_EQUAL_INT(-1, header_list_add(&list, NULL, "b"));
    TEST_ASSERT_EQUAL_INT(-1, header_list_add(&list, "a", NULL));

    /* empty name */
    TEST_ASSERT_EQUAL_INT(-1, header_list_add(&list, "", "value"));
    /* names containing forbidden chars */
    TEST_ASSERT_EQUAL_INT(-1, header_list_add(&list, "Bad Name", "v"));
    TEST_ASSERT_EQUAL_INT(-1, header_list_add(&list, "bad:name", "v"));
    TEST_ASSERT_EQUAL_INT(-1, header_list_add(&list, "bad\rname", "v"));
    /* CR/LF in value */
    TEST_ASSERT_EQUAL_INT(-1, header_list_add(&list, "X-Test", "line1\r\nInjected: yes"));

    TEST_ASSERT_EQUAL_INT(0, list.count);
    header_list_cleanup(&list);
}

static void test_header_list_remove_shifts(void) {
    HeaderList list;
    header_list_init(&list);

    header_list_add(&list, "A", "1");
    header_list_add(&list, "B", "2");
    header_list_add(&list, "C", "3");

    TEST_ASSERT_EQUAL_INT(0, header_list_remove(&list, 1));
    TEST_ASSERT_EQUAL_INT(2, list.count);
    TEST_ASSERT_EQUAL_STRING("A", list.headers[0].name);
    TEST_ASSERT_EQUAL_STRING("C", list.headers[1].name);

    TEST_ASSERT_EQUAL_INT(-1, header_list_remove(&list, 5));
    TEST_ASSERT_EQUAL_INT(-1, header_list_remove(&list, -1));
    TEST_ASSERT_EQUAL_INT(-1, header_list_remove(NULL, 0));

    header_list_cleanup(&list);
}

static void test_header_list_update_existing_and_new(void) {
    HeaderList list;
    header_list_init(&list);

    /* update on missing key adds */
    TEST_ASSERT_EQUAL_INT(0, header_list_update(&list, "Accept", "*/*"));
    TEST_ASSERT_EQUAL_INT(1, list.count);

    /* update on existing key replaces value */
    TEST_ASSERT_EQUAL_INT(0, header_list_update(&list, "Accept", "application/json"));
    TEST_ASSERT_EQUAL_INT(1, list.count);
    TEST_ASSERT_EQUAL_STRING("application/json", list.headers[0].value);

    header_list_cleanup(&list);
}

static void test_header_list_clear(void) {
    HeaderList list;
    header_list_init(&list);
    header_list_add(&list, "A", "1");
    header_list_add(&list, "B", "2");

    header_list_clear(&list);
    TEST_ASSERT_EQUAL_INT(0, list.count);
    TEST_ASSERT_EQUAL_INT(0, list.capacity);
    TEST_ASSERT_NULL(list.headers);
}

static void test_header_list_grows_capacity(void) {
    HeaderList list;
    header_list_init(&list);

    char name[16];
    for (int i = 0; i < 50; i++) {
        snprintf(name, sizeof(name), "X-N-%d", i);
        TEST_ASSERT_EQUAL_INT(0, header_list_add(&list, name, "v"));
    }
    TEST_ASSERT_EQUAL_INT(50, list.count);
    TEST_ASSERT_TRUE(list.capacity >= 50);
    header_list_cleanup(&list);
}

/* -------- Header validation -------- */

static void test_header_validate_name(void) {
    TEST_ASSERT_EQUAL_INT(0, header_validate_name("Content-Type"));
    TEST_ASSERT_EQUAL_INT(0, header_validate_name("X-Trace-Id"));
    TEST_ASSERT_EQUAL_INT(-1, header_validate_name(NULL));
    TEST_ASSERT_EQUAL_INT(-1, header_validate_name(""));
    TEST_ASSERT_EQUAL_INT(-1, header_validate_name("With Space"));
    TEST_ASSERT_EQUAL_INT(-1, header_validate_name("With:Colon"));
}

static void test_header_validate_value(void) {
    TEST_ASSERT_EQUAL_INT(0, header_validate_value(""));
    TEST_ASSERT_EQUAL_INT(0, header_validate_value("normal value 123"));
    TEST_ASSERT_EQUAL_INT(-1, header_validate_value(NULL));
    TEST_ASSERT_EQUAL_INT(-1, header_validate_value("inj\r\nect"));
}

/* -------- Request -------- */

static void test_request_create_defaults(void) {
    Request *r = request_create();
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_STRING("GET", r->method);
    TEST_ASSERT_EQUAL_STRING("", r->url);
    TEST_ASSERT_NULL(r->body);
    TEST_ASSERT_EQUAL_size_t(0, r->body_size);
    TEST_ASSERT_EQUAL_INT(0, r->headers.count);
    TEST_ASSERT_EQUAL_INT(0, r->selected_auth_type);
    TEST_ASSERT_TRUE(r->auth_api_key_enabled);
    request_destroy(r);
    request_destroy(NULL); /* must be safe */
}

static void test_request_set_body_basic(void) {
    Request r;
    request_init(&r);

    const char *body = "{\"hello\":\"world\"}";
    TEST_ASSERT_EQUAL_INT(0, request_set_body(&r, body, strlen(body)));
    TEST_ASSERT_NOT_NULL(r.body);
    TEST_ASSERT_EQUAL_size_t(strlen(body), r.body_size);
    TEST_ASSERT_EQUAL_STRING(body, r.body);

    /* replacing body frees the old one (no leak if not crashing) */
    const char *body2 = "different";
    TEST_ASSERT_EQUAL_INT(0, request_set_body(&r, body2, strlen(body2)));
    TEST_ASSERT_EQUAL_STRING(body2, r.body);

    /* clearing body */
    TEST_ASSERT_EQUAL_INT(0, request_set_body(&r, NULL, 0));
    TEST_ASSERT_NULL(r.body);
    TEST_ASSERT_EQUAL_size_t(0, r.body_size);

    request_cleanup(&r);
}

static void test_request_set_body_rejects_excessive(void) {
    Request r;
    request_init(&r);
    /* 51 MB > 50 MB cap */
    TEST_ASSERT_EQUAL_INT(-1, request_set_body(&r, "x", 51u * 1024u * 1024u));
    TEST_ASSERT_EQUAL_INT(-1, request_set_body(NULL, "x", 1));
    request_cleanup(&r);
}

/* -------- Response -------- */

static void test_response_create_defaults(void) {
    Response *r = response_create();
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_INT(0, r->status_code);
    TEST_ASSERT_EQUAL_STRING("", r->status_text);
    TEST_ASSERT_NULL(r->body);
    TEST_ASSERT_EQUAL_size_t(0, r->body_size);
    TEST_ASSERT_EQUAL_INT(0, r->is_truncated);
    response_destroy(r);
}

static void test_response_set_body(void) {
    Response r;
    response_init(&r);

    const char *body = "OK";
    TEST_ASSERT_EQUAL_INT(0, response_set_body(&r, body, strlen(body)));
    TEST_ASSERT_EQUAL_STRING("OK", r.body);
    TEST_ASSERT_EQUAL_size_t(2, r.body_size);

    /* 100 MB cap */
    TEST_ASSERT_EQUAL_INT(-1, response_set_body(&r, "x", 101u * 1024u * 1024u));

    response_cleanup(&r);
}

/* -------- Error strings -------- */

static void test_error_string(void) {
    TEST_ASSERT_EQUAL_STRING("Success",
        request_response_error_string(REQUEST_RESPONSE_SUCCESS));
    TEST_ASSERT_EQUAL_STRING("Null parameter provided",
        request_response_error_string(REQUEST_RESPONSE_ERROR_NULL_PARAM));
    TEST_ASSERT_EQUAL_STRING("Memory allocation failed",
        request_response_error_string(REQUEST_RESPONSE_ERROR_MEMORY_ALLOCATION));
    /* unknown enum value */
    TEST_ASSERT_EQUAL_STRING("Unknown error",
        request_response_error_string((RequestResponseError)9999));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_header_list_create_destroy);
    RUN_TEST(test_header_list_add_and_find);
    RUN_TEST(test_header_list_add_rejects_invalid);
    RUN_TEST(test_header_list_remove_shifts);
    RUN_TEST(test_header_list_update_existing_and_new);
    RUN_TEST(test_header_list_clear);
    RUN_TEST(test_header_list_grows_capacity);
    RUN_TEST(test_header_validate_name);
    RUN_TEST(test_header_validate_value);
    RUN_TEST(test_request_create_defaults);
    RUN_TEST(test_request_set_body_basic);
    RUN_TEST(test_request_set_body_rejects_excessive);
    RUN_TEST(test_response_create_defaults);
    RUN_TEST(test_response_set_body);
    RUN_TEST(test_error_string);
    return UNITY_END();
}
