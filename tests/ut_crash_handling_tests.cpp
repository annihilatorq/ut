#include <version>

#ifndef UT_ENABLE_MODULES
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "ut/ut.hpp"
#else
import ut;
import std;
#endif

using namespace ut;

volatile int* null_pointer = nullptr;
volatile int zero = 0;

namespace
{
   std::string executable;

   [[nodiscard]] std::string output_of(const std::string_view fault)
   {
      const std::string log = "crash-" + std::string{fault} + ".log";
      std::system(('"' + executable + "\" " + std::string{fault} + " > " + log + " 2>&1").c_str());

      std::ifstream file{log};
      const std::string output{std::istreambuf_iterator<char>{file}, {}};

      file.close();
      std::remove(log.c_str());
      return output;
   }

   void fault(const std::string_view which)
   {
      "runs before the crash"_test = [] { expect(true); };

      if (which == "read") {
         test("reads through a null pointer") = [] { expect(*null_pointer == 0); };
      }
      if (which == "divide") {
         test("divides by zero") = [] { expect(1 / zero == 0); };
      }
      if (which == "abort") {
         test("calls abort") = [] { std::abort(); };
      }
   }
}

int main(int argc, char** argv)
{
   if (argc > 1) {
      fault(argv[1]);
      return 0;
   }

   executable = argv[0];

   const std::string read = output_of("read");

   "names the test the signal arrived in"_test = [&] {
      expect(read.contains("CRASHED \"reads through a null pointer\" SIGSEGV")) << read;
   };

   "counts the tests that passed before the signal"_test = [&] { expect(read.contains("(1 passed)")) << read; };

#if defined(__cpp_lib_stacktrace)
   "prints stack frames beneath the report"_test = [&] { expect(read.contains("\n  ")) << read; };
#endif

#if !defined(_MSC_VER)
   // MSVC runtime doesn't raise SIGFPE for division by zero
   "reports SIGFPE for integer division by zero"_test = [] {
      const std::string output = output_of("divide");
      expect(output.contains("CRASHED \"divides by zero\" SIGFPE")) << output;
   };

   // Debugger catches it before it reaches our handler
   "reports SIGABRT for abort"_test = [] {
      const std::string output = output_of("abort");
      expect(output.contains("CRASHED \"calls abort\" SIGABRT")) << output;
   };
#endif
}
