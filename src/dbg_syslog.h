/*----------------------------------------------------------------------------
 * Zed Shaw's cosmic macros
 * copied from Learn C the Hard Way
 * http://c.learncodethehardway.org/book/
 * with non debug logging going to syslog
 *--------------------------------------------------------------------------*/
#if !defined(__DBG_SYSLOG_H__)
#define __DBG_SYSLOG_H__

#if defined(NDEBUG)
#include <syslog.h>
#else
#include <stdio.h>
#endif // NDEBUG

#include <errno.h>
#include <string.h>

#define clean_errno() (errno == 0 ? "None" : strerror(errno))

#if defined(NDEBUG)
#define debug(M, ...)

#define log_err(M, ...) syslog(LOG_ERR, \
                              "[ERROR] %s:%d: errno: %s " M "\n", \
                              __FILE__, \
                              __LINE__, \
                              clean_errno(), \
                              ##__VA_ARGS__)

#define log_warn(M, ...) syslog(LOG_WARNING \
                                 "[WARN] %s:%d: errno: %s " M "\n", \
                                 __FILE__, \
                                 __LINE__, \
                                 clean_errno(), \
                                 ##__VA_ARGS__)

#define log_info(M, ...) syslog(LOG_INFO, \
                                 "[INFO] %s:%d: " M "\n", \
                                 __FILE__, \
                                 __LINE__, \
                                 ##__VA_ARGS__)

#else
#define debug(M, ...) fprintf(stderr, \
                              "DEBUG %s:%d: " M "\n", \
                              __FILE__, \
                              __LINE__, \
                              ##__VA_ARGS__)

#define log_err(M, ...) fprintf(stderr, \
                              "[ERROR] %s:%d: errno: %s " M "\n", \
                              __FILE__, \
                              __LINE__, \
                              clean_errno(), \
                              ##__VA_ARGS__)

#define log_warn(M, ...) fprintf(stderr, \
                                 "[WARN] %s:%d: errno: %s " M "\n", \
                                 __FILE__, \
                                 __LINE__, \
                                 clean_errno(), \
                                 ##__VA_ARGS__)

#define log_info(M, ...) fprintf(stderr, \
                                 "[INFO] %s:%d: " M "\n", \
                                 __FILE__, \
                                 __LINE__, \
                                 ##__VA_ARGS__)

#endif // defined(NDEBUG)

#define check(A, M, ...) if (!(A)) { log_err(M, ##__VA_ARGS__); \
                                     errno = 0; \
                                     goto error; }

#define sentinel(M, ...) { log_err(M, ##__VA_ARGS__); \
                           errno = 0; \
                           goto error; }

#define check_mem(A) check((A), "out of memory.") 

#define check_debug(A, M, ...) if (!(A)) { debug(M, ##__VA_ARGS__); \
                                           errno = 0; \
                                           goto error; }
#endif // !defined(__DBG_SYSLOG_H__)
