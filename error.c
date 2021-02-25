#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

char* errorString(int error, char* buf, const char* fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, BUFSIZ, fmt, ap);
    va_end(ap);
    assert(n > 0);
    if (error != 0) {
        char* errp = buf + n;
        strerror_r(error, errp, BUFSIZ);
        n = strlen(errp);
        snprintf(errp + n, BUFSIZ - n, ".(%d)", error);
    }
    buf[BUFSIZ - 1] = '\0';
    return buf;
}

