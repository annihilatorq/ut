#ifndef UT_ENABLE_MODULES
#include <memory_resource>
#include <optional>
#include <stdexcept>
#include <system_error>

#include "ut/ut.hpp"
#else
import ut;
import std;
#endif

using namespace ut;

struct custom_error final : std::exception
{
   const char* what() const noexcept override { return "custom"; }
};

suite exception_tests = [] {
   // Proves that the runner keeps running tests instead of terminating the
   // process, which is what an unguarded throw used to do
   "before"_test = [] { expect(true); };

   "out of range"_test = [] { throw std::out_of_range{"out of range"}; };

   "system error"_test = [] { throw std::system_error{std::make_error_code(std::errc::io_error)}; };

   "runtime error"_test = [] { throw std::runtime_error{"runtime error"}; };

   "bad optional access"_test = [] { (void)std::optional<int>{}.value(); };

   "custom"_test = [] { throw custom_error{}; };

   "not an exception"_test = [] { throw 42; };

   "after"_test = [] { expect(true); };
};

int main() {}
