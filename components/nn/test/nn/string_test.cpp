#include <boost/test/unit_test.hpp>

// This test is C++, so it is also what keeps nn/string.h usable from C++:
// without an extern "C" guard in the header the calls below are declared
// with C++ linkage and the test does not link.
#include <nn/string.h>
#include <nn/allocator.h>

#include <stdlib.h>
#include <string.h>

static struct nn_allocator defaultAllocator = {
    .calloc = &calloc,
    .free = &free
};

BOOST_AUTO_TEST_SUITE(string)

BOOST_AUTO_TEST_CASE(strdup_copies_the_string)
{
    char* s = nn_strdup("hello", &defaultAllocator);
    BOOST_TEST_REQUIRE(s != nullptr);
    BOOST_TEST(strcmp(s, "hello") == 0);
    defaultAllocator.free(s);
}

BOOST_AUTO_TEST_CASE(strdup_of_the_empty_string)
{
    char* s = nn_strdup("", &defaultAllocator);
    BOOST_TEST_REQUIRE(s != nullptr);
    BOOST_TEST(s[0] == '\0');
    defaultAllocator.free(s);
}

BOOST_AUTO_TEST_CASE(strcat_appends)
{
    char buffer[16];
    memset(buffer, 0, sizeof(buffer));
    BOOST_TEST(nn_strcat(buffer, sizeof(buffer), "foo"));
    BOOST_TEST(nn_strcat(buffer, sizeof(buffer), "bar"));
    BOOST_TEST(strcmp(buffer, "foobar") == 0);
}

BOOST_AUTO_TEST_CASE(strcat_at_the_bound)
{
    // dstLen has to hold both strings and the trailing NULL, so a dst of
    // exactly strlen(dst)+strlen(src)+1 is the smallest which succeeds.
    char exact[7] = "foo";
    BOOST_TEST(nn_strcat(exact, sizeof(exact), "bar"));
    BOOST_TEST(strcmp(exact, "foobar") == 0);

    char oneShort[6] = "foo";
    BOOST_TEST(!nn_strcat(oneShort, sizeof(oneShort), "bar"));
    BOOST_TEST(strcmp(oneShort, "foo") == 0);
}

BOOST_AUTO_TEST_CASE(strcat_leaves_dst_untouched_when_it_does_not_fit)
{
    char buffer[8] = "abc";
    BOOST_TEST(!nn_strcat(buffer, sizeof(buffer), "defghij"));
    BOOST_TEST(strcmp(buffer, "abc") == 0);
}

BOOST_AUTO_TEST_SUITE_END()
