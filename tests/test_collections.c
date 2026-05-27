#include "unity.h"
#include "collections.h"
#include "request_response.h"

#include <stdlib.h>
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* -------- Collection lifecycle -------- */

static void test_collection_create_defaults(void) {
    Collection *c = collection_create("My Collection", "A description");
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_STRING("My Collection", c->name);
    TEST_ASSERT_EQUAL_STRING("A description", c->description);
    TEST_ASSERT_EQUAL_INT(0, c->request_count);
    TEST_ASSERT_TRUE(c->request_capacity > 0);
    TEST_ASSERT_NOT_NULL(c->requests);
    TEST_ASSERT_NOT_NULL(c->request_names);
    /* Generated id has the expected prefix */
    TEST_ASSERT_EQUAL_INT(0, strncmp(c->id, "col_", 4));
    TEST_ASSERT_TRUE(c->created_at > 0);
    TEST_ASSERT_EQUAL_INT64(c->created_at, c->modified_at);
    collection_destroy(c);
    collection_destroy(NULL); /* must be safe */
}

static void test_collection_create_null_name_uses_default(void) {
    Collection *c = collection_create(NULL, NULL);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_STRING("Untitled Collection", c->name);
    TEST_ASSERT_EQUAL_STRING("", c->description);
    collection_destroy(c);
}

/* -------- Request management -------- */

static void test_collection_add_get_remove_request(void) {
    Collection *c = collection_create("col", "");

    Request r;
    request_init(&r);
    strcpy(r.method, "POST");
    strcpy(r.url, "https://example.com/api");
    header_list_add(&r.headers, "Content-Type", "application/json");
    request_set_body(&r, "{\"a\":1}", 7);
    r.selected_auth_type = 2;
    strcpy(r.auth_bearer_token, "tok");

    int idx = collection_add_request(c, &r, "Create User");
    TEST_ASSERT_EQUAL_INT(0, idx);
    TEST_ASSERT_EQUAL_INT(1, c->request_count);

    Request *stored = collection_get_request(c, 0);
    TEST_ASSERT_NOT_NULL(stored);
    TEST_ASSERT_EQUAL_STRING("POST", stored->method);
    TEST_ASSERT_EQUAL_STRING("https://example.com/api", stored->url);
    TEST_ASSERT_EQUAL_INT(1, stored->headers.count);
    TEST_ASSERT_EQUAL_STRING("Content-Type", stored->headers.headers[0].name);
    TEST_ASSERT_EQUAL_STRING("{\"a\":1}", stored->body);
    TEST_ASSERT_EQUAL_INT(2, stored->selected_auth_type);
    TEST_ASSERT_EQUAL_STRING("tok", stored->auth_bearer_token);

    TEST_ASSERT_EQUAL_STRING("Create User", collection_get_request_name(c, 0));

    /* invalid args */
    TEST_ASSERT_EQUAL_INT(-1, collection_add_request(NULL, &r, "x"));
    TEST_ASSERT_EQUAL_INT(-1, collection_add_request(c, NULL, "x"));
    TEST_ASSERT_EQUAL_INT(-1, collection_add_request(c, &r, NULL));
    TEST_ASSERT_EQUAL_INT(-1, collection_add_request(c, &r, ""));

    /* out-of-range get */
    TEST_ASSERT_NULL(collection_get_request(c, 5));
    TEST_ASSERT_NULL(collection_get_request_name(c, -1));

    /* remove */
    TEST_ASSERT_EQUAL_INT(0, collection_remove_request(c, 0));
    TEST_ASSERT_EQUAL_INT(0, c->request_count);
    TEST_ASSERT_EQUAL_INT(-1, collection_remove_request(c, 0));

    request_cleanup(&r);
    collection_destroy(c);
}

static void test_collection_duplicate_request(void) {
    Collection *c = collection_create("col", "");
    Request r;
    request_init(&r);
    strcpy(r.url, "https://a/b");
    collection_add_request(c, &r, "Original");

    int new_idx = collection_duplicate_request(c, 0);
    TEST_ASSERT_EQUAL_INT(1, new_idx);
    TEST_ASSERT_EQUAL_INT(2, c->request_count);
    TEST_ASSERT_EQUAL_STRING("Original (Copy)", collection_get_request_name(c, 1));

    request_cleanup(&r);
    collection_destroy(c);
}

static void test_collection_rename_request(void) {
    Collection *c = collection_create("col", "");
    Request r;
    request_init(&r);
    collection_add_request(c, &r, "Old");

    TEST_ASSERT_EQUAL_INT(0, collection_rename_request(c, 0, "Brand New"));
    TEST_ASSERT_EQUAL_STRING("Brand New", collection_get_request_name(c, 0));

    TEST_ASSERT_EQUAL_INT(-1, collection_rename_request(c, 5, "x"));
    TEST_ASSERT_EQUAL_INT(-1, collection_rename_request(c, 0, NULL));

    request_cleanup(&r);
    collection_destroy(c);
}

static void test_collection_set_name_and_description(void) {
    Collection *c = collection_create("col", "");
    time_t initial = c->modified_at;

    /* sleep alternative — directly bump time to verify ordering only via API */
    TEST_ASSERT_EQUAL_INT(0, collection_set_name(c, "New Name"));
    TEST_ASSERT_EQUAL_STRING("New Name", c->name);
    TEST_ASSERT_TRUE(c->modified_at >= initial);

    TEST_ASSERT_EQUAL_INT(0, collection_set_description(c, "New desc"));
    TEST_ASSERT_EQUAL_STRING("New desc", c->description);

    /* description may be NULL (treated as empty) */
    TEST_ASSERT_EQUAL_INT(0, collection_set_description(c, NULL));
    TEST_ASSERT_EQUAL_STRING("", c->description);

    /* invalid name (empty) */
    TEST_ASSERT_EQUAL_INT(-1, collection_set_name(c, ""));
    TEST_ASSERT_EQUAL_INT(-1, collection_set_name(c, NULL));
    TEST_ASSERT_EQUAL_INT(-1, collection_set_name(NULL, "x"));

    collection_destroy(c);
}

/* -------- Validation helpers -------- */

static void test_collection_validate_name(void) {
    TEST_ASSERT_TRUE(collection_validate_name("ok"));
    TEST_ASSERT_TRUE(collection_validate_name("A name with spaces"));
    TEST_ASSERT_FALSE(collection_validate_name(""));
    TEST_ASSERT_FALSE(collection_validate_name(NULL));
    TEST_ASSERT_FALSE(collection_validate_name("with\nnewline"));
    TEST_ASSERT_FALSE(collection_validate_name("with\ttab"));

    char too_long[260];
    memset(too_long, 'a', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';
    TEST_ASSERT_FALSE(collection_validate_name(too_long));
}

static void test_collection_validate_description(void) {
    TEST_ASSERT_TRUE(collection_validate_description(""));
    TEST_ASSERT_TRUE(collection_validate_description(NULL));
    TEST_ASSERT_TRUE(collection_validate_description("Some text"));

    char too_long[520];
    memset(too_long, 'a', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';
    TEST_ASSERT_FALSE(collection_validate_description(too_long));
}

static void test_collection_is_valid(void) {
    Collection *c = collection_create("ok", "desc");
    TEST_ASSERT_TRUE(collection_is_valid(c));
    TEST_ASSERT_FALSE(collection_is_valid(NULL));
    collection_destroy(c);
}

/* -------- CollectionManager -------- */

static void test_collection_manager_lifecycle(void) {
    CollectionManager *m = collection_manager_create();
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_INT(0, m->count);
    TEST_ASSERT_EQUAL_INT(-1, m->active_collection_index);
    TEST_ASSERT_EQUAL_INT(-1, m->active_request_index);
    TEST_ASSERT_FALSE(collection_manager_has_collections(m));
    collection_manager_destroy(m);
    collection_manager_destroy(NULL);
}

static void test_collection_manager_add_and_get(void) {
    CollectionManager *m = collection_manager_create();

    Collection *c1 = collection_create("First", "");
    Collection *c2 = collection_create("Second", "");

    int i1 = collection_manager_add_collection(m, c1);
    int i2 = collection_manager_add_collection(m, c2);
    TEST_ASSERT_EQUAL_INT(0, i1);
    TEST_ASSERT_EQUAL_INT(1, i2);
    TEST_ASSERT_EQUAL_INT(2, m->count);
    TEST_ASSERT_TRUE(collection_manager_has_collections(m));

    /* first added becomes active */
    TEST_ASSERT_EQUAL_INT(0, m->active_collection_index);

    /* find by name */
    TEST_ASSERT_EQUAL_INT(1, collection_manager_find_collection_by_name(m, "Second"));
    TEST_ASSERT_EQUAL_INT(-1, collection_manager_find_collection_by_name(m, "Missing"));

    /* get out-of-range returns NULL */
    TEST_ASSERT_NULL(collection_manager_get_collection(m, 99));

    /* originals can be freed because manager makes its own copy */
    collection_destroy(c1);
    collection_destroy(c2);

    /* manager copies remain valid */
    Collection *stored = collection_manager_get_collection(m, 0);
    TEST_ASSERT_NOT_NULL(stored);
    TEST_ASSERT_EQUAL_STRING("First", stored->name);

    collection_manager_destroy(m);
}

static void test_collection_manager_active_selection(void) {
    CollectionManager *m = collection_manager_create();
    Collection *c = collection_create("a", "");

    Request r;
    request_init(&r);
    strcpy(r.url, "https://x/y");
    collection_add_request(c, &r, "Req1");
    collection_add_request(c, &r, "Req2");

    collection_manager_add_collection(m, c);
    collection_destroy(c);

    /* Active request defaults to 0 when the collection has requests. */
    TEST_ASSERT_EQUAL_INT(0, m->active_collection_index);
    TEST_ASSERT_EQUAL_INT(0, m->active_request_index);

    TEST_ASSERT_EQUAL_INT(0, collection_manager_set_active_request(m, 1));
    TEST_ASSERT_EQUAL_INT(1, m->active_request_index);

    TEST_ASSERT_EQUAL_INT(-1, collection_manager_set_active_request(m, 99));
    TEST_ASSERT_EQUAL_INT(-1, collection_manager_set_active_collection(m, 99));

    /* setting active to -1 is allowed */
    TEST_ASSERT_EQUAL_INT(0, collection_manager_set_active_collection(m, -1));
    TEST_ASSERT_EQUAL_INT(-1, m->active_request_index);

    request_cleanup(&r);
    collection_manager_destroy(m);
}

static void test_collection_manager_remove_collection(void) {
    CollectionManager *m = collection_manager_create();
    Collection *a = collection_create("A", "");
    Collection *b = collection_create("B", "");
    Collection *c = collection_create("C", "");
    collection_manager_add_collection(m, a);
    collection_manager_add_collection(m, b);
    collection_manager_add_collection(m, c);
    collection_destroy(a);
    collection_destroy(b);
    collection_destroy(c);

    TEST_ASSERT_EQUAL_INT(0, collection_manager_remove_collection(m, 1));
    TEST_ASSERT_EQUAL_INT(2, m->count);
    TEST_ASSERT_EQUAL_STRING("A", collection_manager_get_collection(m, 0)->name);
    TEST_ASSERT_EQUAL_STRING("C", collection_manager_get_collection(m, 1)->name);

    TEST_ASSERT_EQUAL_INT(-1, collection_manager_remove_collection(m, 99));

    collection_manager_destroy(m);
}

static void test_collection_manager_total_requests(void) {
    CollectionManager *m = collection_manager_create();
    Collection *c1 = collection_create("A", "");
    Collection *c2 = collection_create("B", "");
    Request r;
    request_init(&r);
    strcpy(r.url, "https://x/y");

    collection_add_request(c1, &r, "r1");
    collection_add_request(c1, &r, "r2");
    collection_add_request(c2, &r, "r3");

    collection_manager_add_collection(m, c1);
    collection_manager_add_collection(m, c2);

    TEST_ASSERT_EQUAL_INT(3, collection_manager_get_total_requests(m));
    TEST_ASSERT_EQUAL_INT(0, collection_manager_get_total_requests(NULL));

    collection_destroy(c1);
    collection_destroy(c2);
    request_cleanup(&r);
    collection_manager_destroy(m);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_collection_create_defaults);
    RUN_TEST(test_collection_create_null_name_uses_default);
    RUN_TEST(test_collection_add_get_remove_request);
    RUN_TEST(test_collection_duplicate_request);
    RUN_TEST(test_collection_rename_request);
    RUN_TEST(test_collection_set_name_and_description);
    RUN_TEST(test_collection_validate_name);
    RUN_TEST(test_collection_validate_description);
    RUN_TEST(test_collection_is_valid);
    RUN_TEST(test_collection_manager_lifecycle);
    RUN_TEST(test_collection_manager_add_and_get);
    RUN_TEST(test_collection_manager_active_selection);
    RUN_TEST(test_collection_manager_remove_collection);
    RUN_TEST(test_collection_manager_total_requests);
    return UNITY_END();
}
