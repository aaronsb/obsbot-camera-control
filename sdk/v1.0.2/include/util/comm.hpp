#ifndef OBSBOT_COMM_HPP
#define OBSBOT_COMM_HPP

#include <cstdint>
#include <cstdarg>

#define LIB_MAJOR_VER    1
#define LIB_MINOR_VER    3
#define LIB_REVISION     0

#define LIB_DEV_VER_T(ver)                      #ver
#define LIB_DEV_VER(major, minor, revision)     LIB_DEV_VER_T(major.minor.revision)


#ifdef _MSC_VER
#define DEV_EXPORT __declspec(dllexport)
#else
#define DEV_EXPORT __attribute__((visibility("default")))
#endif


template<typename T>
inline T *getPtrHelper(T *ptr)
{ return ptr; }

template<typename Ptr>
inline auto getPtrHelper(Ptr &ptr) -> decltype(ptr.operator->())
{ return ptr.operator->(); }

#define DECLARE_PRIVATE(Class)                                                  \
    inline Class##Private* d_func()                                             \
    { return reinterpret_cast<Class##Private *>(getPtrHelper(d_ptr)); }         \
    inline const Class##Private* d_func() const                                 \
    { return reinterpret_cast<const Class##Private *>(getPtrHelper(d_ptr)); }   \
    friend class Class##Private;

#define DECLARE_PUBLIC(Class)                                                           \
    inline Class* q_func() { return static_cast<Class *>(q_ptr); }                      \
    inline const Class* q_func() const { return static_cast<const Class *>(q_ptr); }    \
    friend class Class;

#define R_D(Class) Class##Private * const d = d_func()
#define R_Q(Class) Class * const q = q_func()

/**
 * @brief  Log level enumeration
 */
enum
{
    DEV_ERROR = 100,
    DEV_WARN = 200,
    DEV_INFO = 300,
    DEV_DEBUG = 400
};

/**
 * @brief  The callback function definition for log output used by the device library, if no callback function is
 *         registered, it will be output to the console.
 * @param  [in] lvl   Log level, DEV_ERROR ~ DEV_DEBUG.
 * @param  [in] msg   Format control string.
 * @param  [in] args  Variables corresponding to format control strings.
 * @param  [in] p     User-defined parameter.
 */
typedef void (*dlog_handler_t)(int32_t lvl, const char *msg, va_list args, void *p);

/**
 * @brief  Get the current log output callback function.
 * @param  [out] handler   Receive the currently used log output callback function.
 * @param  [out] param     Receive user-defined parameter.
 * @return void
 */
DEV_EXPORT void dev_get_log_handler(dlog_handler_t *handler, void **param);

/**
 * @brief  Register log output callback function.
 * @param  [in] handler   Log output callback function to be registered.
 * @param  [in] param     User-defined parameter.
 * @return void
 */
DEV_EXPORT void dev_set_log_handler(dlog_handler_t handler, void *param);

void dlogva(int32_t log_level, const char *format, va_list args);


#if !defined(_MSC_VER) && !defined(SWIG)
#define PRINTFATTR(f, a) __attribute__((__format__(__printf__, f, a)))
#else
#define PRINTFATTR(f, a)
#endif

PRINTFATTR(2, 3)

void dlog(int32_t log_level, const char *format, ...);

#undef PRINTFATTR

inline const char *get_dll_ver()
{ return LIB_DEV_VER(LIB_MAJOR_VER, LIB_MINOR_VER, LIB_REVISION); }


#endif // OBSBOT_COMM_HPP
