#include <boost/test/unit_test.hpp>
#include <nn/log.h>

#include <string.h>

struct log_message {
    enum nn_log_severity severity;
    const char* module;
    const char* file;
    int line;
};

struct log_message lastMessage;

#ifdef __cplusplus
extern "C" {
#endif

void log_function(void* userData, enum nn_log_severity severity, const char* module, const char* file, int line, const char* fmt, va_list args)
{
    //printf("%s %s:%d ", module, file, line);
    //vprintf(fmt, args);
    //printf("\n");

    lastMessage.severity = severity;
    lastMessage.module = module;
    lastMessage.file = file;
    lastMessage.line = line;

}

#ifdef __cplusplus
} //extern "C"
#endif


const char* logModule = "LOG";

BOOST_AUTO_TEST_SUITE(logging)

BOOST_AUTO_TEST_CASE(test)
{
    struct nn_log logger;
    nn_log_init(&logger, log_function, NULL);

    NN_LOG_ERROR(&logger, logModule, "host %s port %d", "127.0.0.1", 4242);
    BOOST_TEST(lastMessage.severity == NN_LOG_SEVERITY_ERROR);
    BOOST_TEST(lastMessage.file == __FILE__);
    BOOST_TEST(lastMessage.line == (__LINE__ - 3));

    NN_LOG_TRACE(&logger, logModule, "host %s port %d", "127.0.0.1", 4242);
    BOOST_TEST(lastMessage.severity == NN_LOG_SEVERITY_TRACE);
    BOOST_TEST(lastMessage.file == __FILE__);
    BOOST_TEST(lastMessage.line == (__LINE__ - 3));
}

BOOST_AUTO_TEST_CASE(null_logger)
{
    NN_LOG_ERROR(NULL, logModule, "host %s port %d", "127.0.0.1", 4242);
}

BOOST_AUTO_TEST_CASE(logger_without_a_print_function)
{
    // A logger which has not had a print function installed yet has to be
    // safe to log through: on an embedded target the integrator's context
    // struct is commonly zeroed long before the log handler is set up.
    struct nn_log logger;
    memset(&logger, 0, sizeof(logger));
    NN_LOG_ERROR(&logger, logModule, "host %s port %d", "127.0.0.1", 4242);

    // ... and so does one initialised without a print function.
    nn_log_init(&logger, NULL, NULL);
    NN_LOG_TRACE(&logger, logModule, "no print function installed");
}

BOOST_AUTO_TEST_SUITE_END()
