// Refactored from:
// Copyright (c) 2024 Kris Jusiak (kris at jusiak dot net)
// Distributed under the Boost Software License, Version 1.0.
// (See http://www.boost.org/LICENSE_1_0.txt)
//
// UT: A simple C++23 unit testing library with compile-time and run-time support.
//
// Running specific tests:
//   Set the UT_RUN environment variable to run only specific tests by name.
//   Single test:    UT_RUN="my test" ./my_tests
//   Multiple tests: UT_RUN="[test1,test2,test3]" ./my_tests
//   If UT_RUN is not set, all tests run (default behavior).
module;

#include <typeinfo>
#include <version>

#if !defined(UT_NO_CRASH_HANDLER) && !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
#if !defined(__has_feature)
#define UT_CRASH_HANDLER
#elif !__has_feature(address_sanitizer) && !__has_feature(thread_sanitizer) && !__has_feature(memory_sanitizer)
// Sanitizer installs its own handler and reports way more info
#define UT_CRASH_HANDLER
#endif
#endif

#if defined(UT_CRASH_HANDLER)
#include <csignal>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif
#endif

export module ut;

import std;

namespace ut
{
   namespace detail
   {
      constexpr bool fatal = true;

      template <class>
      constexpr auto is_mutable_lambda_v = false;
      template <class R, class B, class... Ts>
      constexpr auto is_mutable_lambda_v<R (B::*)(Ts...)> = true;
      template <class R, class B, class... Ts>
      constexpr auto is_mutable_lambda_v<R (B::*)(Ts...) const> = false;
      template <class Fn>
      constexpr auto has_capture_lambda_v = sizeof(Fn) > 1ul;

      template <class T, class...>
      struct identity
      {
         using type = T;
      };

      [[nodiscard]] inline std::string_view get_runnable_tests_list()
      {
         static const std::string filter = [] {
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(suppress : 4996)
            const char* const env = std::getenv("UT_RUN");
#else
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            const char* const env = std::getenv("UT_RUN");
#pragma GCC diagnostic pop
#endif

            return env != nullptr ? std::string{env} : std::string{};
         }();

         return filter;
      }
   }

   export template <std::size_t Size>
   struct fixed_string
   {
      constexpr fixed_string(const char (&str)[Size])
      {
         for (std::size_t i = 0; i < Size; ++i) {
            storage[i] = str[i];
         }
      }
      [[nodiscard]] constexpr auto operator[](const auto i) const { return storage[i]; }
      [[nodiscard]] constexpr auto data() const { return storage; }
      [[nodiscard]] static constexpr auto size() { return Size - 1; }
      [[nodiscard]] constexpr operator std::string_view() const { return {storage, Size - 1}; }
      constexpr friend auto operator<<(auto& os, const fixed_string& fs) -> decltype(auto)
      {
         return os << std::string_view{fs.storage, fs.size()};
      }
      char storage[Size]{};
   };

   namespace events
   {
      enum class mode { run_time, compile_time };
      template <mode Mode>
      struct test_begin
      {
         std::string_view file_name{};
         std::uint_least32_t line{};
         std::string_view name{};
      };
      template <mode Mode>
      struct test_end
      {
         std::string_view file_name{};
         std::uint_least32_t line{};
         std::string_view name{};
         enum { FAILED, PASSED, COMPILE_TIME } result{};
      };
      struct assertion
      {
         bool passed{};
         std::string_view file_name{};
         std::uint_least32_t line{};
      };
      struct fatal
      {};
      struct exception
      {
         std::string_view type{};
         std::string_view what{};
         std::string_view info{};
      };
      template <class Msg>
      struct log
      {
         const Msg& msg;
         bool result{};
      };
      struct summary
      {
         enum { FAILED, PASSED, COMPILE_TIME };
         std::size_t asserts[2]{}; /* FAILED, PASSED */
         std::size_t tests[3]{}; /* FAILED, PASSED, COMPILE_TIME */
      };
   } // namespace events

   template <class OStream>
   struct outputter
   {
      template <events::mode Mode>
      constexpr auto on(const events::test_begin<Mode>&)
      {}
      constexpr auto on(const events::test_begin<events::mode::run_time>& event) { current_test = event; }
      template <events::mode Mode>
      constexpr auto on(const events::test_end<Mode>&)
      {}
      constexpr auto on(const events::assertion& event)
      {
         if (not event.passed && not std::is_constant_evaluated()) {
            begin_failure_line();
            os << "FAILED \"" << current_test.name << "\" ";
            const auto n = event.file_name.size();
            const auto start = n <= 32 ? 0 : n - 32;
            if (start > 0) {
               os << "...";
            }
            os << event.file_name.substr(start, n) << ":" << event.line << '\n';
         }
      }
      constexpr auto on(const events::exception& event)
      {
         if (not std::is_constant_evaluated()) {
            begin_failure_line();
            os << "FAILED \"" << current_test.name << "\" threw " << event.type;
            if (not event.what.empty()) {
               os << ": " << event.what;
            }
            if (not event.info.empty()) {
               os << " [" << event.info << ']';
            }
            os << '\n';
         }
      }
      constexpr auto on(const events::fatal&) {}
      template <class Msg>
      constexpr auto on(const events::log<Msg>& event)
      {
         if (!std::is_constant_evaluated() && !event.result) {
            os << ' ' << event.msg;
         }
      }
      constexpr auto on(const events::summary& event)
      {
         using namespace events;
         if (!std::is_constant_evaluated()) {
            if (event.asserts[summary::FAILED] || event.tests[summary::FAILED]) {
               os << "\nFAILED\n";
            }
            else {
               os << "\nPASSED\n";
            }
            os << "tests: " << (event.tests[summary::PASSED] + event.tests[summary::FAILED]) << " ("
               << event.tests[summary::PASSED] << " passed, " << event.tests[summary::FAILED] << " failed, "
               << event.tests[summary::COMPILE_TIME] << " compile-time)\n"
               << "asserts: " << (event.asserts[summary::PASSED] + event.asserts[summary::FAILED]) << " ("
               << event.asserts[summary::PASSED] << " passed, " << event.asserts[summary::FAILED] << " failed)\n";
         }
      }
      constexpr auto begin_failure_line()
      {
         if (initial_new_line == '\n') {
            os << initial_new_line;
         }
         else {
            initial_new_line = '\n';
         }
      }

      OStream& os;
      events::test_begin<events::mode::run_time> current_test{};
      char initial_new_line{};
   };

   template <class Outputter, std::uint32_t MaxDepth = 16>
   struct reporter
   {
      constexpr auto on(const events::test_begin<events::mode::run_time>& event)
      {
         asserts_failed[current++] = summary.asserts[events::summary::FAILED];
         outputter.on(event);
      }
      constexpr auto on(const events::test_end<events::mode::run_time>& event)
      {
         const auto result = summary.asserts[events::summary::FAILED] == asserts_failed[--current];
         ++summary.tests[result];
         events::test_end<events::mode::run_time> te{event};
         te.result = static_cast<decltype(te.result)>(result);
         outputter.on(te);
      }
      constexpr auto on(const events::test_begin<events::mode::compile_time>&)
      {
         ++summary.tests[events::summary::COMPILE_TIME];
      }
      constexpr auto on(const events::test_end<events::mode::compile_time>&) {}
      constexpr auto on(const events::assertion& event)
      {
         if (event.passed) {
            ++summary.asserts[events::summary::PASSED];
         }
         else {
            ++summary.asserts[events::summary::FAILED];
         }
         outputter.on(event);
      }
      constexpr auto on(const events::exception& event)
      {
         ++summary.asserts[events::summary::FAILED];
         outputter.on(event);
      }
      constexpr auto on(const events::fatal& event)
      {
         ++summary.tests[events::summary::FAILED];
         outputter.on(event);
         outputter.on(summary);
         std::exit(1);
      }

      ~reporter()
      { // non constexpr
         outputter.on(summary);
         if (summary.asserts[events::summary::FAILED]) {
            std::exit(1);
         }
      }

      Outputter& outputter;
      events::summary summary{};
      std::size_t asserts_failed[MaxDepth]{};
      std::size_t current{};
   };

   namespace detail
   {
#if defined(UT_CRASH_HANDLER)
      void install_crash_handler();
#endif

#if __cpp_exceptions
      [[nodiscard]] inline std::string describe(const std::error_code& ec)
      {
         return std::string{ec.category().name()} + ':' + std::to_string(ec.value()) + " (" + ec.message() + ')';
      }

      // Kept out of the templated .run_guarded() since GCC checks typeid
      // where a template is instantiated
      [[nodiscard]] inline std::string_view exception_type_name(const std::exception& e) { return typeid(e).name(); }

      // Runs a test body and turns any exception escaping it into a reported
      // failure instead of letting it reach std::terminate
      template <class Reporter, class Test>
      void run_guarded(Reporter& reporter, Test& test)
      {
         const auto report = [&](std::string_view type, std::string_view what, std::string_view info = {}) {
            reporter.on(events::exception{type, what, info});
         };

         try {
            test();
         }
         // std::logic_error-s
         catch (const std::domain_error& e) {
            report("std::domain_error", e.what());
         }
         catch (const std::invalid_argument& e) {
            report("std::invalid_argument", e.what());
         }
         catch (const std::length_error& e) {
            report("std::length_error", e.what());
         }
         catch (const std::out_of_range& e) {
            report("std::out_of_range", e.what());
         }
         catch (const std::logic_error& e) {
            report("std::logic_error", e.what());
         }
         // std::runtime_error-s
         catch (const std::ios_base::failure& e) {
            report("std::ios_base::failure", e.what(), describe(e.code()));
         }
         catch (const std::system_error& e) {
            report("std::system_error", e.what(), describe(e.code()));
         }
         catch (const std::range_error& e) {
            report("std::range_error", e.what());
         }
         catch (const std::overflow_error& e) {
            report("std::overflow_error", e.what());
         }
         catch (const std::underflow_error& e) {
            report("std::underflow_error", e.what());
         }
         catch (const std::runtime_error& e) {
            report("std::runtime_error", e.what());
         }
         // Allocation, RTTI and bad-access
         catch (const std::bad_array_new_length& e) {
            report("std::bad_array_new_length", e.what());
         }
         catch (const std::bad_alloc& e) {
            report("std::bad_alloc", e.what());
         }
         catch (const std::bad_any_cast& e) {
            report("std::bad_any_cast", e.what());
         }
         catch (const std::bad_cast& e) {
            report("std::bad_cast", e.what());
         }
         catch (const std::bad_typeid& e) {
            report("std::bad_typeid", e.what());
         }
         catch (const std::bad_optional_access& e) {
            report("std::bad_optional_access", e.what());
         }
         catch (const std::bad_variant_access& e) {
            report("std::bad_variant_access", e.what());
         }
#if defined(__cpp_lib_expected)
         catch (const std::bad_expected_access<void>& e) {
            report("std::bad_expected_access", e.what());
         }
#endif
         catch (const std::bad_weak_ptr& e) {
            report("std::bad_weak_ptr", e.what());
         }
         catch (const std::bad_function_call& e) {
            report("std::bad_function_call", e.what());
         }
         catch (const std::bad_exception& e) {
            report("std::bad_exception", e.what());
         }
         // Fallbacks
         catch (const std::exception& e) {
            // Didn't match any specific exception, so the dynamic type is
            // all that is left for identification
            report(exception_type_name(e), e.what());
         }
         catch (...) {
            report("unknown exception", {});
         }
      }
#endif
   }

   template <class Reporter>
   struct runner
   {
      template <class Test>
      constexpr auto on(Test test, const std::string_view file_name, std::uint_least32_t line,
                        const std::string_view name) -> bool
      {
         if (std::is_constant_evaluated()) {
            if constexpr (requires { requires detail::is_mutable_lambda_v<decltype(&Test::operator())>; }) {
               return false;
            }
            else {
               test();
               return true;
            }
         }
         else {
#if defined(UT_CRASH_HANDLER)
            detail::install_crash_handler();
#endif

            std::string_view filter = detail::get_runnable_tests_list();

            auto matches_filter = [](std::string_view test_name, std::string_view f) {
               if (f.empty()) return true;

               // Array format: [test1,test2,test3]
               if (f.starts_with('[') && f.ends_with(']')) {
                  auto content = f.substr(1, f.size() - 2);
                  std::size_t pos = 0;
                  while (pos < content.size()) {
                     auto comma = content.find(',', pos);
                     auto token =
                        (comma == std::string_view::npos) ? content.substr(pos) : content.substr(pos, comma - pos);
                     if (token == test_name) return true;
                     if (comma == std::string_view::npos) break;
                     pos = comma + 1;
                  }
                  return false;
               }

               // Single test name
               return test_name == f;
            };

            if (!matches_filter(name, filter)) {
               return false;
            }

#if defined(UT_COMPILE_TIME)
            if constexpr (!requires { requires detail::is_mutable_lambda_v<decltype(&Test::operator())>; } &&
                          !detail::has_capture_lambda_v<Test>) {
               reporter.on(events::test_begin<events::mode::compile_time>{file_name, line, name});
               static_assert((test(), "[FAILED]"));
               reporter.on(events::test_end<events::mode::compile_time>{file_name, line, name});
            }
#endif

            reporter.on(events::test_begin<events::mode::run_time>{file_name, line, name});
#if __cpp_exceptions
            detail::run_guarded(reporter, test);
#else
            test();
#endif
            reporter.on(events::test_end<events::mode::run_time>{file_name, line, name});
         }
         return true;
      }

      Reporter& reporter;
   };
}

namespace ut
{
   struct cfg_t
   {
      struct stream_t
      {
         friend constexpr decltype(auto) operator<<([[maybe_unused]] auto& os, [[maybe_unused]] const auto& t)
         {
            static_assert(requires { std::clog << t; });
            return (std::clog << t);
         }
      } stream;
      ut::outputter<stream_t> outputter{stream};
      ut::reporter<decltype(outputter)> reporter{outputter};
      ut::runner<decltype(reporter)> runner{reporter};
   };

   export extern cfg_t cfg;
   cfg_t cfg{};

   namespace detail
   {
#if defined(UT_CRASH_HANDLER)
      inline char crash_buffer[512];
      inline std::size_t crash_length;

      inline void crash_append(const std::string_view text)
      {
         for (const char c : text) {
            if (crash_length < sizeof(crash_buffer)) {
               crash_buffer[crash_length++] = c;
            }
         }
      }

      inline void crash_append_dec(std::size_t value)
      {
         char digits[20];
         std::size_t count = 0;
         do {
            digits[count++] = static_cast<char>('0' + value % 10);
            value /= 10;
         } while (value != 0);

         while (count != 0) {
            crash_append(std::string_view{&digits[--count], 1});
         }
      }

      inline void crash_append_hex(std::uintptr_t value)
      {
         char digits[2 * sizeof(value)];
         std::size_t count = 0;
         do {
            digits[count++] = "0123456789abcdef"[value % 16];
            value /= 16;
         } while (value != 0);

         crash_append("0x");
         while (count != 0) {
            crash_append(std::string_view{&digits[--count], 1});
         }
      }

      inline void crash_begin()
      {
         crash_length = 0;
         if (cfg.outputter.initial_new_line == '\n') {
            crash_append("\n");
         }
         crash_append("CRASHED");
         if (cfg.reporter.current != 0) {
            crash_append(" \"");
            crash_append(cfg.outputter.current_test.name);
            crash_append("\"");
         }
         crash_append(" ");
      }

      inline void crash_flush()
      {
#if defined(_WIN32)
         _write(2, crash_buffer, static_cast<unsigned int>(crash_length));
#else
         const ssize_t written = write(2, crash_buffer, crash_length);
         static_cast<void>(written);
#endif
         crash_length = 0;
      }

#if defined(__cpp_lib_stacktrace)
      [[nodiscard]] inline bool crash_is_dispatch_frame(const std::stacktrace_entry& entry)
      {
         return entry.description().contains("KiUserExceptionDispatcher") ||
                entry.description().contains("__restore_rt") || entry.source_file().contains("libc_sigaction");
      }

      inline void crash_append_stacktrace()
      {
         const std::stacktrace trace = std::stacktrace::current(1);

         std::size_t first = 0;
         for (std::size_t index = 0; index != trace.size(); ++index) {
            if (crash_is_dispatch_frame(trace[index])) {
               first = index + 1;
            }
         }

         constexpr std::size_t frames_limit = 32;
         for (std::size_t index = first; index != trace.size() && index - first != frames_limit; ++index) {
            crash_append("  ");
            crash_append(trace[index].description());

            const std::string file = trace[index].source_file();
            if (not file.empty()) {
               crash_append(" (");
               crash_append(file);
               crash_append(":");
               crash_append_dec(trace[index].source_line());
               crash_append(")");
            }
            crash_append("\n");
            crash_flush();
         }
      }
#endif

      inline void crash_end()
      {
         crash_append(" (");
         crash_append_dec(cfg.reporter.summary.tests[events::summary::PASSED]);
         crash_append(" passed)\n");

         // Stack walking allocates, which is not safe inside a signal
         // handler, so it can be lost
         crash_flush();
#if defined(__cpp_lib_stacktrace)
         crash_append_stacktrace();
#endif
      }

      [[nodiscard]] inline std::string_view crash_signal_name(const int number)
      {
         switch (number) {
         case SIGSEGV:
            return "SIGSEGV";
#if defined(SIGBUS)
         case SIGBUS:
            return "SIGBUS";
#endif
         case SIGFPE:
            return "SIGFPE";
         case SIGILL:
            return "SIGILL";
         case SIGABRT:
            return "SIGABRT";
         default:
            return "signal";
         }
      }

      // Re-raise rather than exit, so that the debugger and the exit status
      // stay the same. All we do is "report" that a crash happened
#if defined(_WIN32)
      inline void crash_signal_handler(const int number)
      {
         crash_begin();
         crash_append(crash_signal_name(number));
         crash_end();

         std::signal(number, SIG_DFL);
         std::raise(number);
      }
#else
      inline void crash_signal_handler(const int number, siginfo_t* info, void*)
      {
         crash_begin();
         crash_append(crash_signal_name(number));

         if (info != nullptr && info->si_code > 0) {
            crash_append(" at ");
            crash_append_hex(reinterpret_cast<std::uintptr_t>(info->si_addr));
         }
         crash_end();

         std::signal(number, SIG_DFL);
         std::raise(number);
      }
#endif

      // Not using a static constructor here, because suites are static too,
      // and the order between TUs is unspecified
      inline void install_crash_handler()
      {
         [[maybe_unused]] static const bool installed = [] {
#if defined(_WIN32)
            const int numbers[] = {SIGSEGV, SIGFPE, SIGILL, SIGABRT};
            for (const int number : numbers) {
               std::signal(number, &crash_signal_handler);
            }
#else
            struct sigaction action = {};
            action.sa_sigaction = &crash_signal_handler;
            action.sa_flags = SA_SIGINFO;
            sigemptyset(&action.sa_mask);

            const int numbers[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
            for (const int number : numbers) {
               sigaction(number, &action, nullptr);
            }
#endif
            return true;
         }();
      }
#endif
   }

   struct expect_fn final
   {
      template <bool Fatal>
      struct eval final
      {
         template <class T>
            requires std::convertible_to<T, bool>
         constexpr eval(T&& test_passed, auto&& loc) : passed(static_cast<bool>(test_passed))
         {
            if (std::is_constant_evaluated()) {
               if (not passed) {
                  std::abort();
               }
            }
            else {
               cfg.reporter.on(events::assertion{passed, loc.file_name(), loc.line()});
               if (not passed) {
                  if constexpr (Fatal) {
                     cfg.reporter.on(events::fatal{});
                  }
               }
            }
         }
         bool passed{};
      };

      template <class T>
         requires std::convertible_to<T, bool>
      constexpr auto operator()(T&& test_passed,
                                const std::source_location& loc = std::source_location::current()) const
      {
         return log{eval<not detail::fatal>{test_passed, loc}.passed};
      }

      template <class T>
         requires std::convertible_to<T, bool>
      constexpr auto operator[](T&& test_passed,
                                const std::source_location& loc = std::source_location::current()) const
      {
         return log{eval<detail::fatal>{test_passed, loc}.passed};
      }

     private:
      struct log final
      {
         bool passed{};

         template <class Msg>
         constexpr const auto& operator<<(const Msg& msg) const
         {
            cfg.outputter.on(events::log<Msg>{msg, passed});
            return *this;
         }
      };
   };

   export inline constexpr expect_fn expect{};

   export struct suite final
   {
      suite(auto&& tests) { tests(); }
   };

   namespace detail
   {
      export template <fixed_string Name>
      struct test final
      {
         constexpr auto operator=(auto test) const
         {
            const auto& loc = std::source_location::current();
            return cfg.runner.on(test, loc.file_name(), loc.line(), Name);
         }
      };

      export struct runtime_test final
      {
         std::string_view name{};

         constexpr auto operator=(auto test) const
         {
            const auto& loc = std::source_location::current();
            return cfg.runner.on(test, loc.file_name(), loc.line(), name);
         }
      };
   }

   export constexpr auto test(const std::string_view name) { return detail::runtime_test{name}; }

   export template <fixed_string Str>
   [[nodiscard]] constexpr auto operator""_test()
   {
      return detail::test<Str>{};
   }

#if __cpp_exceptions
   export template <class Callable, class... Args>
   constexpr auto throws(Callable&& c, Args&&... args)
   {
      try {
         std::forward<Callable>(c)(std::forward<Args>(args)...);
      }
      catch (...) {
         return true;
      }
      return false;
   }
#endif
}

export using ut::operator""_test;
