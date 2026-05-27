#include "unity.h"
#include "collections.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

void setUp(void) {}
void tearDown(void) {}

/* -------- Lifecycle -------- */

static void test_cookie_jar_create_destroy(void) {
    CookieJar *jar = cookie_jar_create();
    TEST_ASSERT_NOT_NULL(jar);
    TEST_ASSERT_EQUAL_INT(0, jar->count);
    TEST_ASSERT_TRUE(jar->capacity > 0);
    TEST_ASSERT_NOT_NULL(jar->cookies);
    cookie_jar_destroy(jar);
    cookie_jar_destroy(NULL); /* safe */
}

/* -------- Add / find / remove -------- */

static void test_cookie_jar_add_and_find(void) {
    CookieJar jar;
    cookie_jar_init(&jar);

    int idx = cookie_jar_add_cookie(&jar, "session", "abc",
        "example.com", "/", 0, -1, false, false, false, false);
    TEST_ASSERT_EQUAL_INT(0, idx);
    TEST_ASSERT_EQUAL_INT(1, jar.count);

    TEST_ASSERT_EQUAL_INT(0, cookie_jar_find_cookie(&jar, "session", "example.com", "/"));
    TEST_ASSERT_EQUAL_INT(-1, cookie_jar_find_cookie(&jar, "missing", "example.com", "/"));
    TEST_ASSERT_EQUAL_INT(-1, cookie_jar_find_cookie(&jar, "session", "other.com", "/"));

    /* invalid args */
    TEST_ASSERT_EQUAL_INT(-1, cookie_jar_add_cookie(NULL, "a", "b", "d", "/", 0, -1, false, false, false, false));
    TEST_ASSERT_EQUAL_INT(-1, cookie_jar_add_cookie(&jar, NULL, "b", "d", "/", 0, -1, false, false, false, false));
    TEST_ASSERT_EQUAL_INT(-1, cookie_jar_add_cookie(&jar, "a", NULL, "d", "/", 0, -1, false, false, false, false));

    cookie_jar_cleanup(&jar);
}

static void test_cookie_jar_add_updates_existing(void) {
    CookieJar jar;
    cookie_jar_init(&jar);

    cookie_jar_add_cookie(&jar, "session", "v1",
        "example.com", "/", 0, -1, false, false, false, false);
    /* same name+domain+path => update, not duplicate */
    int idx = cookie_jar_add_cookie(&jar, "session", "v2",
        "example.com", "/", 0, -1, false, false, false, false);
    TEST_ASSERT_EQUAL_INT(0, idx);
    TEST_ASSERT_EQUAL_INT(1, jar.count);
    TEST_ASSERT_EQUAL_STRING("v2", jar.cookies[0].value);

    cookie_jar_cleanup(&jar);
}

static void test_cookie_jar_remove(void) {
    CookieJar jar;
    cookie_jar_init(&jar);

    cookie_jar_add_cookie(&jar, "a", "1", "example.com", "/", 0, -1, false, false, false, false);
    cookie_jar_add_cookie(&jar, "b", "2", "example.com", "/", 0, -1, false, false, false, false);
    cookie_jar_add_cookie(&jar, "c", "3", "example.com", "/", 0, -1, false, false, false, false);

    TEST_ASSERT_EQUAL_INT(0, cookie_jar_remove_cookie(&jar, 1));
    TEST_ASSERT_EQUAL_INT(2, jar.count);
    TEST_ASSERT_EQUAL_STRING("a", jar.cookies[0].name);
    TEST_ASSERT_EQUAL_STRING("c", jar.cookies[1].name);

    TEST_ASSERT_EQUAL_INT(-1, cookie_jar_remove_cookie(&jar, 99));
    TEST_ASSERT_EQUAL_INT(-1, cookie_jar_remove_cookie(NULL, 0));

    cookie_jar_cleanup(&jar);
}

static void test_cookie_jar_clear_all(void) {
    CookieJar jar;
    cookie_jar_init(&jar);
    cookie_jar_add_cookie(&jar, "a", "1", "example.com", "/", 0, -1, false, false, false, false);
    cookie_jar_add_cookie(&jar, "b", "2", "example.com", "/", 0, -1, false, false, false, false);

    cookie_jar_clear_all(&jar);
    TEST_ASSERT_EQUAL_INT(0, jar.count);

    cookie_jar_cleanup(&jar);
}

/* -------- Expiration -------- */

static void test_cookie_expiration_max_age(void) {
    StoredCookie c;
    memset(&c, 0, sizeof(c));
    strcpy(c.name, "x");
    strcpy(c.value, "y");
    c.created_at = time(NULL) - 1000;
    c.max_age = 100; /* expired 900s ago */
    TEST_ASSERT_TRUE(cookie_jar_is_cookie_expired(&c));

    c.max_age = 100000; /* still valid */
    TEST_ASSERT_FALSE(cookie_jar_is_cookie_expired(&c));
}

static void test_cookie_expiration_expires(void) {
    StoredCookie c;
    memset(&c, 0, sizeof(c));
    c.max_age = -1; /* not using max-age */
    c.expires = time(NULL) - 100;
    TEST_ASSERT_TRUE(cookie_jar_is_cookie_expired(&c));

    c.expires = time(NULL) + 3600;
    TEST_ASSERT_FALSE(cookie_jar_is_cookie_expired(&c));

    /* No expiry info at all => session cookie, never expires by time */
    c.expires = 0;
    TEST_ASSERT_FALSE(cookie_jar_is_cookie_expired(&c));

    TEST_ASSERT_TRUE(cookie_jar_is_cookie_expired(NULL));
}

static void test_cookie_jar_cleanup_expired(void) {
    CookieJar jar;
    cookie_jar_init(&jar);

    /* Note: cookie_jar_add_cookie sets created_at to time(NULL); we need to
       reach in and back-date it for the expired ones. */
    cookie_jar_add_cookie(&jar, "fresh", "v", "example.com", "/", 0, 3600, false, false, false, false);
    cookie_jar_add_cookie(&jar, "old1", "v", "example.com", "/", 0, 1, false, false, false, false);
    cookie_jar_add_cookie(&jar, "old2", "v", "example.com", "/", 0, 1, false, false, false, false);
    jar.cookies[1].created_at = time(NULL) - 1000;
    jar.cookies[2].created_at = time(NULL) - 1000;

    int removed = cookie_jar_cleanup_expired(&jar);
    TEST_ASSERT_EQUAL_INT(2, removed);
    TEST_ASSERT_EQUAL_INT(1, jar.count);
    TEST_ASSERT_EQUAL_STRING("fresh", jar.cookies[0].name);

    cookie_jar_cleanup(&jar);
}

/* -------- Matching -------- */

static void test_cookie_matches_request_basic(void) {
    StoredCookie c;
    memset(&c, 0, sizeof(c));
    strcpy(c.name, "s");
    strcpy(c.value, "v");
    strcpy(c.domain, "example.com");
    strcpy(c.path, "/");
    c.max_age = -1;
    c.expires = 0;

    TEST_ASSERT_TRUE(cookie_jar_matches_request(&c, "https://example.com/foo", true));
    TEST_ASSERT_FALSE(cookie_jar_matches_request(&c, "https://other.com/foo", true));
    TEST_ASSERT_FALSE(cookie_jar_matches_request(NULL, "https://example.com/", true));
    TEST_ASSERT_FALSE(cookie_jar_matches_request(&c, NULL, true));
}

static void test_cookie_matches_request_secure(void) {
    StoredCookie c;
    memset(&c, 0, sizeof(c));
    strcpy(c.name, "s");
    strcpy(c.value, "v");
    strcpy(c.domain, "example.com");
    strcpy(c.path, "/");
    c.max_age = -1;
    c.secure = true;

    /* secure cookie + non-secure connection on non-localhost => no match */
    TEST_ASSERT_FALSE(cookie_jar_matches_request(&c, "http://example.com/", false));
    /* secure cookie + secure connection => match */
    TEST_ASSERT_TRUE(cookie_jar_matches_request(&c, "https://example.com/", true));
}

static void test_cookie_matches_request_path_prefix(void) {
    StoredCookie c;
    memset(&c, 0, sizeof(c));
    strcpy(c.name, "s");
    strcpy(c.value, "v");
    strcpy(c.domain, "example.com");
    strcpy(c.path, "/api");
    c.max_age = -1;

    TEST_ASSERT_TRUE(cookie_jar_matches_request(&c, "https://example.com/api/users", true));
    TEST_ASSERT_TRUE(cookie_jar_matches_request(&c, "https://example.com/api", true));
    /* /apirequest is NOT a prefix match per RFC 6265 §5.1.4 */
    TEST_ASSERT_FALSE(cookie_jar_matches_request(&c, "https://example.com/apirequest", true));
    TEST_ASSERT_FALSE(cookie_jar_matches_request(&c, "https://example.com/other", true));
}

static void test_cookie_matches_request_domain_dot_prefix(void) {
    StoredCookie c;
    memset(&c, 0, sizeof(c));
    strcpy(c.name, "s");
    strcpy(c.value, "v");
    strcpy(c.domain, ".example.com");
    strcpy(c.path, "/");
    c.max_age = -1;

    /* leading dot allows subdomain matches */
    TEST_ASSERT_TRUE(cookie_jar_matches_request(&c, "https://api.example.com/x", true));
    TEST_ASSERT_TRUE(cookie_jar_matches_request(&c, "https://example.com/x", true));
    TEST_ASSERT_FALSE(cookie_jar_matches_request(&c, "https://notexample.com/x", true));
}

/* -------- Building Cookie header -------- */

static void test_cookie_jar_build_cookie_header(void) {
    CookieJar jar;
    cookie_jar_init(&jar);

    cookie_jar_add_cookie(&jar, "a", "1", "example.com", "/", 0, -1, false, false, false, false);
    cookie_jar_add_cookie(&jar, "b", "2", "example.com", "/", 0, -1, false, false, false, false);
    /* Mismatched domain => should be excluded */
    cookie_jar_add_cookie(&jar, "c", "3", "other.com", "/", 0, -1, false, false, false, false);

    char *header = cookie_jar_build_cookie_header(&jar, "https://example.com/", true);
    TEST_ASSERT_NOT_NULL(header);
    /* Order is insertion order */
    TEST_ASSERT_EQUAL_STRING("a=1; b=2", header);
    free(header);

    /* No matching cookies => NULL */
    char *empty = cookie_jar_build_cookie_header(&jar, "https://nomatch.com/", true);
    TEST_ASSERT_NULL(empty);

    cookie_jar_cleanup(&jar);
}

/* -------- Set-Cookie parsing -------- */

static void test_parse_set_cookie_basic(void) {
    CookieJar jar;
    cookie_jar_init(&jar);

    int idx = cookie_jar_parse_set_cookie(&jar,
        "session=abcdef; Path=/; Domain=example.com",
        "https://example.com/login");
    TEST_ASSERT_TRUE(idx >= 0);
    TEST_ASSERT_EQUAL_INT(1, jar.count);
    TEST_ASSERT_EQUAL_STRING("session", jar.cookies[0].name);
    TEST_ASSERT_EQUAL_STRING("abcdef", jar.cookies[0].value);
    TEST_ASSERT_EQUAL_STRING("example.com", jar.cookies[0].domain);
    TEST_ASSERT_EQUAL_STRING("/", jar.cookies[0].path);

    cookie_jar_cleanup(&jar);
}

static void test_parse_set_cookie_attributes(void) {
    CookieJar jar;
    cookie_jar_init(&jar);

    int idx = cookie_jar_parse_set_cookie(&jar,
        "id=42; Max-Age=600; Secure; HttpOnly; SameSite=Strict",
        "https://api.example.com/foo");
    TEST_ASSERT_TRUE(idx >= 0);
    StoredCookie *c = &jar.cookies[0];
    TEST_ASSERT_EQUAL_STRING("id", c->name);
    TEST_ASSERT_EQUAL_STRING("42", c->value);
    TEST_ASSERT_EQUAL_INT(600, c->max_age);
    TEST_ASSERT_TRUE(c->secure);
    TEST_ASSERT_TRUE(c->http_only);
    TEST_ASSERT_TRUE(c->same_site_strict);
    TEST_ASSERT_FALSE(c->same_site_lax);
    /* No Domain in header => extracted from request url */
    TEST_ASSERT_EQUAL_STRING("api.example.com", c->domain);
    /* No Path => default "/" */
    TEST_ASSERT_EQUAL_STRING("/", c->path);

    cookie_jar_cleanup(&jar);
}

static void test_parse_set_cookie_invalid(void) {
    CookieJar jar;
    cookie_jar_init(&jar);

    /* Missing '=' in name=value */
    TEST_ASSERT_EQUAL_INT(-1, cookie_jar_parse_set_cookie(&jar,
        "no-equals-here", "https://example.com/"));
    TEST_ASSERT_EQUAL_INT(-1, cookie_jar_parse_set_cookie(NULL,
        "a=b", "https://example.com/"));
    TEST_ASSERT_EQUAL_INT(-1, cookie_jar_parse_set_cookie(&jar,
        NULL, "https://example.com/"));
    TEST_ASSERT_EQUAL_INT(-1, cookie_jar_parse_set_cookie(&jar,
        "a=b", NULL));

    cookie_jar_cleanup(&jar);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_cookie_jar_create_destroy);
    RUN_TEST(test_cookie_jar_add_and_find);
    RUN_TEST(test_cookie_jar_add_updates_existing);
    RUN_TEST(test_cookie_jar_remove);
    RUN_TEST(test_cookie_jar_clear_all);
    RUN_TEST(test_cookie_expiration_max_age);
    RUN_TEST(test_cookie_expiration_expires);
    RUN_TEST(test_cookie_jar_cleanup_expired);
    RUN_TEST(test_cookie_matches_request_basic);
    RUN_TEST(test_cookie_matches_request_secure);
    RUN_TEST(test_cookie_matches_request_path_prefix);
    RUN_TEST(test_cookie_matches_request_domain_dot_prefix);
    RUN_TEST(test_cookie_jar_build_cookie_header);
    RUN_TEST(test_parse_set_cookie_basic);
    RUN_TEST(test_parse_set_cookie_attributes);
    RUN_TEST(test_parse_set_cookie_invalid);
    return UNITY_END();
}
