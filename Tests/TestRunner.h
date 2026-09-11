#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <type_traits>
#include <vector>

// Minimal, dependency-free test harness for the pure-logic suite. Tests
// register themselves at static-init time and TestRunner.cpp runs them
// all and returns a non-zero exit code on failure, which is what CTest
// checks.
namespace bptest
{
struct TestCase
{
    const char* name;
    void (*fn)();
};

std::vector<TestCase>& registry();

struct Registrar
{
    Registrar (const char* name, void (*fn)());
};

void reportFailure (const char* file, int line, const juce::String& message);
int runAll();

inline bool approxEqual (double a, double b, double tolerance)
{
    return std::abs (a - b) <= tolerance;
}

inline juce::String toString (const juce::String& s) { return s; }
inline juce::String toString (const char* s)         { return juce::String (s); }
inline juce::String toString (bool v)                { return v ? "true" : "false"; }

template <typename T>
inline std::enable_if_t<std::is_integral<T>::value, juce::String> toString (T v)
{
    return juce::String (static_cast<juce::int64> (v));
}

template <typename T>
inline std::enable_if_t<std::is_floating_point<T>::value, juce::String> toString (T v)
{
    return juce::String (static_cast<double> (v), 6);
}
} // namespace bptest

#define BP_TEST(name)                                                        \
    static void name();                                                      \
    static ::bptest::Registrar bp_registrar_##name (#name, &name);           \
    static void name()

#define BP_CHECK(cond)                                                       \
    do {                                                                     \
        if (! (cond))                                                        \
            ::bptest::reportFailure (__FILE__, __LINE__, "CHECK failed: " #cond); \
    } while (false)

#define BP_CHECK_EQ(actual, expected)                                        \
    do {                                                                     \
        const auto bp_a = (actual);                                          \
        const auto bp_e = (expected);                                        \
        if (! (bp_a == bp_e))                                                \
            ::bptest::reportFailure (__FILE__, __LINE__,                     \
                juce::String ("CHECK_EQ failed: " #actual " == " #expected   \
                              " (got ") + ::bptest::toString (bp_a)          \
                + ", expected " + ::bptest::toString (bp_e) + ")");          \
    } while (false)

#define BP_CHECK_NEAR(actual, expected, tolerance)                           \
    do {                                                                     \
        const double bp_a = static_cast<double> (actual);                    \
        const double bp_e = static_cast<double> (expected);                  \
        if (! ::bptest::approxEqual (bp_a, bp_e, (tolerance)))               \
            ::bptest::reportFailure (__FILE__, __LINE__,                     \
                juce::String ("CHECK_NEAR failed: " #actual " ~= " #expected \
                              " (got ") + juce::String (bp_a, 6)            \
                + ", expected " + juce::String (bp_e, 6) + ")");             \
    } while (false)
