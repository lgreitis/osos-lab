/* SPDX-License-Identifier: GPL-3.0-only */

/* Checks eapp_format against expected texts: every conversion, the flags,
 * widths and precision, truncation that returns the full length and cuts
 * before a UTF-8 character, and eapp_logf reaching eapp_log. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "eapp.h"

static char logged[EAPP_LOG_SIZE + 8];

void eapp_log(const char *text)
{
    strcpy(logged, text);
}

static void expect(const char *expected, const char *format, ...)
{
    char out[64];
    memset(out, 'x', sizeof out);
    va_list args;
    va_start(args, format);
    int length = eapp_format_list(out, sizeof out, format, args);
    va_end(args);
    if (strcmp(out, expected) || length != (int)strlen(expected)) {
        printf("format %s: got \"%s\" (%d), expected \"%s\"\n", format, out, length, expected);
        assert(0);
    }
}

int main(void)
{
    expect("", "");
    expect("plain", "plain");
    expect("0 -1 2147483647 -2147483648", "%d %d %d %d", 0, -1, 2147483647, (int)-2147483648);
    expect("4294967295 255 FF ff", "%u %u %X %x", 4294967295u, 255u, 255u, 255u);
    expect("[  42][42  ][00042][-0042][-42  ]", "[%4d][%-4d][%05d][%05d][%-5d]", 42, 42, 42,
           -42, -42);
    expect("[ab][  ab][ab  ][a][ab]", "[%s][%4s][%-4s][%.1s][%.5s]", "ab", "ab", "ab", "ab",
           "ab");
    expect("(null) % c  d", "%s %% %c %2c", (const char *)NULL, 'c', 'd');
    expect("%q 7", "%q %ld", 7); /* l is skipped: long is 32 bits on the target. */
    expect("5", "%d%", 5);
    /* Truncation: the full length comes back and the text is cut at a
     * character boundary. */
    char small[6];
    assert(eapp_format(small, sizeof small, "%s", "abcdefgh") == 8 && !strcmp(small, "abcde"));
    assert(eapp_format(small, sizeof small, "ab\xc3\xa4\xc3\xb6\xc3\xbc") == 8 &&
           !strcmp(small, "ab\xc3\xa4"));
    assert(eapp_format(small, 1, "abc") == 3 && small[0] == 0);
    assert(eapp_format(NULL, 0, "%d", 12345) == 5);
    assert(eapp_format(small, sizeof small, NULL) == 0 && small[0] == 0);
    eapp_logf("run %d of %s", 3, "five");
    assert(!strcmp(logged, "run 3 of five"));
    char longest[200];
    memset(longest, 'a', sizeof longest - 1);
    longest[sizeof longest - 1] = 0;
    eapp_logf("%s", longest);
    assert(strlen(logged) == EAPP_LOG_SIZE - 1);
    puts("format_test passed");
    return 0;
}
