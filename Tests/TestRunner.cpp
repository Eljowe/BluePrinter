#include "TestRunner.h"

#include <cstdio>

#ifdef _WIN32
 #include <windows.h>
 #include <cstdlib>
#endif

namespace bptest
{
namespace
{
int failureCount = 0;
}

std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

Registrar::Registrar (const char* name, void (*fn)())
{
    registry().push_back ({ name, fn });
}

void reportFailure (const char* file, int line, const juce::String& message)
{
    ++failureCount;
    std::printf ("    FAIL %s:%d\n         %s\n", file, line, message.toRawUTF8());
}

int runAll()
{
    int passed = 0;

    for (const auto& test : registry())
    {
        const int before = failureCount;
        test.fn();

        if (failureCount == before)
        {
            ++passed;
            std::printf ("    ok   %s\n", test.name);
        }
        else
        {
            std::printf ("  FAILED %s\n", test.name);
        }
    }

    std::printf ("\n%d/%d tests passed, %d assertion failure(s)\n",
                 passed, static_cast<int> (registry().size()), failureCount);

    return failureCount == 0 ? 0 : 1;
}
} // namespace bptest

int main()
{
#ifdef _WIN32
    // A crashing test must not pop a Windows Error Reporting dialog on the
    // developer's desktop, and abort() must not call the WER report path.
    SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    // Unbuffered so a crash never hides which test ran last.
    std::setvbuf (stdout, nullptr, _IONBF, 0);

    return ::bptest::runAll();
}
