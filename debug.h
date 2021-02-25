#ifndef _DEBUG_H
#define _DEBUG_H

#if defined(WINDOWS) /* need to implement */
    #define debugPrintf(fmt, ...) ((void)0)
#else
    #ifdef NDEBUG
    #define debugPrintf(fmt, ...) ((void)0)
    #else
    #define debugPrintf(fmt, ...) \
        do { fprintf(stderr, "%s:%u->%s(): " fmt "\n", \
            __FILE__, __LINE__, __func__, ##__VA_ARGS__); } while (0)

    #endif
#endif

#endif /*_DEBUG_H */

