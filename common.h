#ifndef __COMMON_H__
#define __COMMON_H__

#ifdef __cplusplus
extern "C"
{
#endif


#define LOG_INFO(fmt,...)       do { if (1) fprintf(stderr, "\033[0;32m[debug][info][%s:%s():%d]:" fmt, __FILE__, __func__,__LINE__, ##__VA_ARGS__); fprintf(stderr, "\n");} while (0)
#define LOG_WARNING(fmt,...)    do { if (1) fprintf(stderr, "\033[0;33m[debug][warning][%s:%s():%d]:" fmt, __FILE__,__func__, __LINE__,##__VA_ARGS__); fprintf(stderr, "\n");} while (0)
#define LOG_ERROR(fmt,...)      do { if (1) fprintf(stderr, "\033[0;31m[debug][error][%s:%s():%d]:" fmt, __FILE__,__func__, __LINE__,##__VA_ARGS__); fprintf(stderr, "\n");} while (0)


#ifdef __cplusplus
};
#endif

#endif

