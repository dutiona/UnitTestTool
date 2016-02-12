/*
 *
 *  (C) Copyright 2016 Michaël Roynard
 *
 *  Distributed under the MIT License, Version 1.0. (See accompanying
 *  file LICENSE or copy at https://opensource.org/licenses/MIT)
 *
 *  See https://github.com/dutiona/H2OFastTests for documentation.
 */

#pragma once

#ifndef H2OFASTTESTS_H
#define H2OFASTTESTS_H

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>


namespace H2OFastTests {

	// Implementation details
	namespace detail {

		// Line info struct
		// Holds line number, file name and function name if relevant
		struct LineInfo {
			const std::string file_;
			const std::string func_;
			const int line_;
			LineInfo(const char* file, const char* func, int line)
				: file_(file), func_(func), line_(line)
			{}
		};

		using line_info_t = LineInfo;

		// Basic display
		std::ostream& operator<<(std::ostream& os, const line_info_t& lineInfo) {
			os << lineInfo.file_ << ":" << lineInfo.line_ << " " << lineInfo.func_;
			return os;
		}

		// Internal exception raised when a test failed
		// Used by internal test runner and assert tool to communicate over the test
		class TestFailure : public std::exception {
		public:
			TestFailure(const std::string& message)
				: message_(message) {}
			virtual const char * what() const { return message_.c_str(); }
		private:
			const std::string message_;
		};

		using test_failure_t = TestFailure;

		// Internal impl for processing an assert and raise the TestFailure Exception
		void FailOnCondition(bool condition, const char* message = nullptr, const line_info_t* lineInfo = nullptr) {
			std::ostringstream oss;
			if (!condition) {
				if (lineInfo != nullptr) {
					oss << ((message != nullptr) ? message : "") << "\t(" << *lineInfo << ")";
				}
				else {
					oss << ((message != nullptr) ? message : "");
				}
				throw test_failure_t(oss.str());
			}
		}

		using test_func_t = std::function<void(void)>;
		using duration_t = std::chrono::duration<double, std::milli>; // ms

		// Standard class discribing a test
		class Test {
		public:

			// States of the test
			enum Status {
				PASSED,	// test successfuly passed
				FAILED,	// test failed to pass (an assert failed)
				ERROR,	// an error occured during the test :
				// any exception was catched like bad_alloc
				SKIPPED,// test was skipped and not run
				NONE	// the run_scenario function wasn't run yet
				// for the scenario holding the test
			};

			// All available constructors
			Test()
				: Test({}, []() {}) {}
			Test(const test_func_t&& test)
				: Test("", std::move(test)) {}
			Test(const std::string& label)
				: Test(label, []() {}) {}
			Test(const std::string& label, const test_func_t&& test)
				: label_(label), status_(NONE), test_holder_(std::make_unique<test_func_t>(std::move(test)))
			{}

			// Copy forbidden
			Test(const Test&) = delete;
			Test& operator=(const Test&) = delete;

			// Default move impl (for VC2013)
			Test(Test&& test)
				: test_holder_(std::move(test.test_holder_)),
				label_(test.label_), status_(test.status_), exec_time_ms_(test.exec_time_ms_),
				failure_reason_(test.failure_reason_), skipped_reason_(test.skipped_reason_), error_(test.error_)
			{}
			Test&& operator=(Test&& test) {
				test_holder_ = std::move(test.test_holder_);
				label_ = test.label_;
				status_ = test.status_;
				exec_time_ms_ = test.exec_time_ms_;
				failure_reason_ = test.failure_reason_;
				skipped_reason_ = test.skipped_reason_;
				error_ = test.error_;
				return std::move(*this);
			}

			// Virtual destructor impl
			virtual ~Test() {}

			// Information getters
			const std::string& getLabel(bool verbose) const { return getLabel_private(verbose); }
			const std::string& getFailureReason() const { return getFailureReason_private(); }
			const std::string& getSkippedReason() const { return getSkippedReason_private(); }
			const std::string& getError() const { return getError_private(); }
			duration_t getExecTimeMs() const { return getExecTimeMs_private(); }
			Status getStatus() const { return getStatus_private(); }

		protected:

			void run() { run_private(); } // Called by RegistryManager

			// Run the test and capture and set the state
			virtual void run_private() {
				auto start = std::chrono::high_resolution_clock::now();
				try {
					(*test_holder_)(); /* /!\ Here is the test call /!\ */
					status_ = PASSED;
				}
				catch (const test_failure_t& failure) {
					status_ = FAILED;
					failure_reason_ = failure.what();
				}
				catch (const std::exception& e) {
					status_ = ERROR;
					error_ = e.what();
				}
				exec_time_ms_ = std::chrono::high_resolution_clock::now() - start;
			}

			// Informations getters impl
			virtual const std::string& getLabel_private(bool /*verbose*/) const { return label_; }
			virtual const std::string& getFailureReason_private() const { return failure_reason_; }
			virtual const std::string& getSkippedReason_private() const { return skipped_reason_; }
			virtual const std::string& getError_private() const { return error_; }
			virtual duration_t getExecTimeMs_private() const { return exec_time_ms_; }
			virtual Status getStatus_private() const { return status_; }

		protected:

			duration_t exec_time_ms_;
			std::unique_ptr<test_func_t> test_holder_;
			std::string label_;
			std::string failure_reason_;
			std::string skipped_reason_;
			std::string error_;
			Status status_;

			friend class RegistryManager;
		};

		using test_t = detail::Test;

		std::ostream& operator<<(std::ostream& os, test_t::Status status) {
			switch (status)	{
			case test_t::PASSED:
				os << "PASSED";
				break;
			case test_t::FAILED:
				os << "FAILED";
				break;
			case test_t::ERROR:
				os << "ERROR";
				break;
			case test_t::SKIPPED:
				os << "SKIPPED";
				break;
			case test_t::NONE:
			default:
				os << "NOT RUN YET";
				break;
			}
			return os;
		}

		// This class wrap a test and make it so it's skipped (never run)
		class SkippedTest : public Test {
		public:

			SkippedTest(Test&& test)
				: Test{ std::move(test) } {}
			SkippedTest(const std::string& reason, Test&& test)
				: Test{ std::move(test) } {
				skipped_reason_ = reason;
			}

		protected:

			// Put state to skipped and don't run the test
			virtual void run_private() { status_ = SKIPPED; }

		};

		using skipped_test_t = detail::SkippedTest;

		// Helper functions to build/skip a test case
		test_t make_test(test_func_t&& func) { return test_t{ std::move(func) }; }
		test_t make_test(const std::string& label, test_func_t&& func) { return test_t{ label, std::move(func) }; }
		test_t make_skipped_test(test_t&& test) { return skipped_test_t{ std::move(test) }; }
		test_t make_skipped_test(const std::string& reason, test_t&& test) { return skipped_test_t{ reason, std::move(test) }; }
		test_t make_skipped_test(test_func_t&& func) { return skipped_test_t{ std::move(make_test(std::move(func))) }; }
		test_t make_skipped_test(const std::string& reason, test_func_t&& func) { return skipped_test_t{ reason, std::move(make_test(std::move(func))) }; }

		using registry_storage_t = std::vector<test_t>;
		using storage_func_t = std::function<registry_storage_t&(void)>;

		// Registry main impl
		class Registry {
		public:
			// Build a registry with a pointer on the static storage as 2nd arg
			Registry(const std::string& label, storage_func_t storage_func)
				: label_(label), storage_func_(storage_func) {}
			//Getters
			registry_storage_t& get() { return storage_func_(); };
			const registry_storage_t& get() const { return const_cast<Registry*>(this)->get(); };
			const std::string& getLabel() const { return label_; }
		private:
			std::string label_;
			storage_func_t storage_func_;
		};

		using registry_t = Registry;

		// POD containing informations about a test
		struct TestInfos {
			const test_t& test;
			using status_t = test_t::Status;
		};

		using tests_infos_t = TestInfos;

		// Interface for making an observer
		class IRegistryObserver {
		public:
			virtual void update(const tests_infos_t& infos) const = 0;
		};

		using registry_observer_t = IRegistryObserver;

		// Implementation of the observable part of the DP observer
		class IRegistryObservable {
		public:
			void notify(const tests_infos_t& infos) const {
				for (auto& observer : list_observers_) {
					observer->update(infos);
				}
			}
			void addObserver(const std::shared_ptr<registry_observer_t>& observer) { list_observers_.insert(observer); }
			void removeObserver(const std::shared_ptr<registry_observer_t>& observer) { list_observers_.erase(observer); }
		private:
			std::set<std::shared_ptr<registry_observer_t>> list_observers_;
		};

		using registry_observable_t = IRegistryObservable;

		// Manage a registry in a static context
		class RegistryManager : public registry_observable_t{
		public:

			// Build a registry and fill it in a static context
			template<class ... Args>
			RegistryManager(const std::string& label, storage_func_t storage_func, Args&& ... tests_or_funcs)
				: run_(false), registry_(label, storage_func), exec_time_ms_accumulator_(duration_t{ 0 }) {
				push_back(std::forward<Args>(tests_or_funcs)...);
			}

			//Recursive variadic to iterate over the test pack
			void push_back(test_t&& test) {
				registry_.get().push_back(std::move(test));
			}

			template<class ... Args>
			void push_back(test_t&& test, Args&& ... tests_or_funcs) {
				push_back(std::move(test));
				push_back(std::forward<Args>(tests_or_funcs)...);
			}

			void push_back(test_func_t&& func) {
				registry_.get().push_back(std::move(make_test(std::move(func))));
			}

			template<class ... Args>
			void push_back(test_func_t&& func, Args&& ... tests_or_funcs) {
				push_back(std::move(func));
				push_back(std::forward<Args>(tests_or_funcs)...);
			}

			template<class BadArgument, class ... Args>
			void push_back(BadArgument&& unknown_typed_arg, Args&& ... tests_or_funcs) {
				// TODO : decide to eigther crach at compile time
				static_assert(false, "Unknown type passed to initialize test registry.");
				// or print a warning
				//std::cerr << "Ignoring bad type passed to initialize test registry." << std::endl;
				// or silently ignore
				//push_back(std::forward<Args>(tests_or_funcs)...);
			}

			// Run all the tests
			void run_tests() {
				auto& tests = registry_.get();
				for (auto& test : tests) {
					test.run();
					exec_time_ms_accumulator_ += test.getExecTimeMs();
					notify(tests_infos_t{ test });
					switch (test.getStatus()) {
					case test_t::PASSED:
						tests_passed_.push_back(&test);
						break;
					case test_t::FAILED:
						tests_failed_.push_back(&test);
						break;
					case test_t::SKIPPED:
						tests_skipped_.push_back(&test);
						break;
					case test_t::ERROR:
						tests_with_error_.push_back(&test);
						break;
					default: break;
					}
				}
				run_ = true;
			}

			// Get informations
			const std::string& getLabel() const { return get()->getLabel(); }

			size_t getPassedCount() const { return run_ ? tests_passed_.size() : 0; }
			const std::vector<const test_t*>& getPassedTests() const { return tests_passed_; }

			size_t getFailedCount() const { return run_ ? tests_failed_.size() : 0; }
			const std::vector<const test_t*>& getFailedTests() const { return tests_failed_; }

			size_t getSkippedCount() const { return run_ ? tests_skipped_.size() : 0; }
			const std::vector<const test_t*>& getSkippedTests() const { return tests_skipped_; }

			size_t getWithErrorCount() const { return run_ ? tests_with_error_.size() : 0; }
			const std::vector<const test_t*>& getWithErrorTests() const { return tests_with_error_; }

			size_t getAllTestsCount() const { return run_ ? get()->get().size() : 0; }
			const registry_storage_t& getAllTests() const { return get()->get(); }
			duration_t getAllTestsExecTimeMs() const { return run_ ? exec_time_ms_accumulator_ : duration_t{ 0 }; }

		private:

			const registry_t* get() const { return &registry_; }

			bool run_;
			registry_t registry_;
			duration_t exec_time_ms_accumulator_;
			std::vector<const test_t*> tests_passed_;
			std::vector<const test_t*> tests_failed_;
			std::vector<const test_t*> tests_skipped_;
			std::vector<const test_t*> tests_with_error_;

		};

		using registry_manager_t = RegistryManager;

		// Assert test class to help verbosing test logic into lambda's impl
		// Heavily inspired by the eponym MSVC's builtin assert class
		class Assert {
		public:

			// Verify that two objects are equal.
			template<class T>
			static void AreEqual(const T& expected, const T& actual,
				const char* message = nullptr, const line_info_t* lineInfo = nullptr) {
				FailOnCondition(expected == actual, message, lineInfo);
			}

			// double equality comparison:
			static void AreEqual(double expected, double actual, double tolerance,
				const char* message = nullptr, const line_info_t* lineInfo = nullptr) {
				double diff = expected - actual;
				FailOnCondition(fabs(diff) <= fabs(tolerance), message, lineInfo);
			}

			// float equality comparison:
			static void AreEqual(float expected, float actual, float tolerance,
				const char* message = nullptr, const line_info_t* lineInfo = nullptr) {
				float diff = expected - actual;
				FailOnCondition(fabs(diff) <= fabs(tolerance), message, lineInfo);
			}

			// char* string equality comparison:
			static void AreEqual(const char* expected, const char* actual, bool ignoreCase = false,
				const char* message = nullptr, const line_info_t* lineInfo = nullptr) {
				AreEqual(std::string{ expected }, std::string{ actual }, ignoreCase, message, lineInfo);
			}

			// char* string equality comparison:
			static void AreEqual(std::string expected, std::string actual, bool ignoreCase = false,
				const char* message = nullptr, const line_info_t* lineInfo = nullptr) {
				if (ignoreCase) {
					std::transform(expected.begin(), expected.end(), expected.begin(), ::tolower);
					std::transform(actual.begin(), actual.end(), actual.begin(), ::tolower);
				}
				FailOnCondition(expected == actual, message, lineInfo);
			}

			// Verify that two references refer to the same object instance (identity):
			template<class T>
			static void AreSame(const T& expected, const T& actual,
				const char* message = nullptr, const line_info_t* lineInfo = nullptr) {
				FailOnCondition(&expected == &actual, message, lineInfo);
			}

			// Generic AreNotEqual comparison:
			template<class T>
			static void AreNotEqual(const T& notExpected, const T& actual,
				const char* message = nullptr, const line_info_t* lineInfo = nullptr) {
				FailOnCondition(!(notExpected == actual), message, lineInfo);
			}

			// double AreNotEqual comparison:
			static void AreNotEqual(double notExpected, double actual, double tolerance,
				const char* message = nullptr, const line_info_t* lineInfo = nullptr) {
				double diff = notExpected - actual;
				FailOnCondition(fabs(diff) > fabs(tolerance), message, lineInfo);
			}

			// float AreNotEqual comparison:
			static void AreNotEqual(float notExpected, float actual, float tolerance,
				const char* message = nullptr, const line_info_t* lineInfo = nullptr) {
				float diff = notExpected - actual;
				FailOnCondition(fabs(diff) > fabs(tolerance), message, lineInfo);
			}

			// char* string AreNotEqual comparison:
			static void AreNotEqual(const char* notExpected, const char* actual, bool ignoreCase = false,
				const char* message = nullptr, const line_info_t* lineInfo = nullptr) {
				AreNotEqual(std::string{ notExpected }, std::string{ actual }, ignoreCase, message, lineInfo);
			}

			// wchar_t* string AreNotEqual comparison with char* message:
			static void AreNotEqual(std::string notExpected, std::string actual, bool ignoreCase = false,
				const char* message = nullptr, const line_info_t* lineInfo = nullptr) {
				if (ignoreCase) {
					std::transform(notExpected.begin(), notExpected.end(), notExpected.begin(), ::tolower);
					std::transform(actual.begin(), actual.end(), actual.begin(), ::tolower);
				}
				FailOnCondition(notExpected != actual, message, lineInfo);
			}

			// Verify that two references do not refer to the same object instance (identity):
			template<class T>
			static void AreNotSame(const T& notExpected, const T& actual,
				const char* message = nullptr, const line_info_t* lineInfo = nullptr) {
				FailOnCondition(!(&notExpected == &actual), message, lineInfo);
			}

			// Verify that a pointer is NULL:
			template<class T>
			static void IsNull(const T* actual,
				const char* message = nullptr, const line_info_t* lineInfo = nullptr) {
				FailOnCondition(actual == nullptr, message, lineInfo);
			}

			// Verify that a pointer is not NULL:
			template<class T>
			static void IsNotNull(const T* actual,
				const char* message = nullptr, const line_info_t* lineInfo = nullptr) {
				FailOnCondition(actual != nullptr, message, lineInfo);
			}

			// Verify that a condition is true:
			static void IsTrue(bool condition,
				const char* message = nullptr, const line_info_t* lineInfo = nullptr) {
				FailOnCondition(condition, message, lineInfo);
			}

			// Verify that a conditon is false:
			static void IsFalse(bool condition,
				const char* message = nullptr, const line_info_t* lineInfo = nullptr) {
				FailOnCondition(!condition, message, lineInfo);
			}

			// Force the test case result to be Failed:
			static void Fail(const char* message = nullptr, const line_info_t* lineInfo = nullptr) {
				FailOnCondition(false, message, lineInfo);
			}

			// Verify that a function raises an exception:
			template<class ExpectedException, class Functor>
			static void ExpectException(Functor functor,
				const char* message = nullptr, const line_info_t* lineInfo = nullptr) {
				try {
					functor();
				}
				catch (ExpectedException) {
					return;
				}
				catch (...) {
					Fail(message, lineInfo);
				}
			}

			template<class ExpectedException, class ReturnType>
			static void ExpectException(ReturnType(*func)(),
				const char* message = nullptr, const line_info_t* lineInfo = nullptr) {
				IsNotNull(func, message, lineInfo);

				try {
					func();
				}
				catch (ExpectedException) {
					return;
				}
				catch (...) {
					Fail(message, lineInfo);
				}
			}
		};
	}

	using detail::Assert; // Temporary

	/*
	*
	* Public interface
	*
	*/

	// Public accessible types
	using line_info_t = detail::line_info_t;
	using test_infos_t = detail::tests_infos_t;
	using registry_storage_t = detail::registry_storage_t;
	using registry_manager_t = detail::RegistryManager;
	using registry_observer_t = detail::IRegistryObserver;

	// Interface to Implement to access access a registry information
	// Possibility to export services into a DLL for forther customization
	class IRegistryTraversal {
	public:
		IRegistryTraversal(const registry_manager_t& registry) : registry_(registry) {}
		virtual ~IRegistryTraversal() {}
	protected:
		const registry_manager_t& getRegistryManager() const { return registry_; }
	protected:
		registry_manager_t registry_;
	};

	// Trivial impl for console display results
	class RegistryTraversal_ConsoleIO : private IRegistryTraversal {
	public:
		RegistryTraversal_ConsoleIO(const registry_manager_t& registry) : IRegistryTraversal{ registry } {}
		std::ostream& print(std::ostream& os, bool verbose) const {
			auto& registry_manager = getRegistryManager();
			os << "UNIT TEST SUMMARY [" << registry_.getLabel() << "] [" << registry_.getAllTestsExecTimeMs().count() << "ms]:" << std::endl;

			os << "\tPASSED:" << registry_.getPassedCount() << "/" << registry_.getAllTestsCount() << std::endl;
			if (verbose) {
				for (const auto test : registry_.getPassedTests()) {
					os << "\t\t[" << test->getLabel(verbose) << "] [" << test->getExecTimeMs().count() << "ms] " << test->getStatus() << std::endl;
				}
			}

			os << "\tFAILED:" << registry_.getFailedCount() << "/" << registry_.getAllTestsCount() << std::endl;
			if (verbose) {
				for (const auto test : registry_.getFailedTests()) {
					os << "\t\t[" << test->getLabel(verbose) << "] [" << test->getExecTimeMs().count() << "ms] " << test->getStatus() << std::endl
						<< "\t\tMessage: " << test->getFailureReason() << std::endl;
				}
			}

			os << "\tSKIPPED:" << registry_.getSkippedCount() << "/" << registry_.getAllTestsCount() << std::endl;
			if (verbose) {
				for (const auto test : registry_.getSkippedTests()) {
					os << "\t\t[" << test->getLabel(verbose) << "] [" << test->getExecTimeMs().count() << "ms] " << test->getStatus() << std::endl;
				}
			}

			os << "\tERRORS:" << registry_.getWithErrorCount() << "/" << registry_.getAllTestsCount() << std::endl;
			if (verbose) {
				for (const auto test : registry_.getWithErrorTests()) {
					os << "\t\t[" << test->getLabel(verbose) << "] [" << test->getExecTimeMs().count() << "ms] " << test->getStatus() << std::endl
						<< "\t\tMessage: " << test->getError() << std::endl;
				}
			}

			return os;
		}
	};

	// Observer impl example
	class ConsoleIO_Observer : public registry_observer_t {
		virtual void update(const test_infos_t& infos) const {
			std::cout << (infos.test.getStatus() == test_infos_t::status_t::SKIPPED ? "SKIPPING TEST [" : "RUNNING TEST [")
				<< infos.test.getLabel(false) << "] [" << infos.test.getExecTimeMs().count() << "ms]:" << std::endl
				<< "Status: " << infos.test.getStatus() << std::endl;
		}
	};

	// Asserter engine wrapper above MSVC's builtin
	template<class Expr>
	class Expression {
	public:

		using empry_expression_t = Expression<nullptr_t>;

		Expression()
			: expr_(nullptr) {}
		Expression(Expr&& expr)
			: expr_(std::forward<Expr>(expr))
		{}

		template<class NewExpr>
		Expression<NewExpr> andThat(NewExpr&& expr) {
			return{ std::forward<NewExpr>(expr) };
		}

		template<class ... Args>
		empry_expression_t isTrue(Args&& ... args) {
			Assert::IsTrue(expr_, std::forward<Args>(args)...);
			return{};
		}

		template<class ... Args>
		empry_expression_t isFalse(Args&& ... args) {
			Assert::IsFalse(expr_, std::forward<Args>(args)...);
			return{};
		}

		template<class ... Args>
		empry_expression_t isEqualTo(Args&& ... args) {
			Assert::AreEqual(expr_, std::forward<Args>(args)...);
			return{};
		}

		template<class ... Args>
		empry_expression_t isNotEqualTo(Args&& ... args) {
			Assert::AreNotEqual(expr_, std::forward<Args>(args)...);
			return{};
		}

		template<class ... Args>
		empry_expression_t isSameAs(Args&& ... args) {
			Assert::AreSame(expr_, std::forward<Args>(args)...);
			return{};
		}

		template<class ... Args>
		empry_expression_t isNotSameAs(Args&& ... args) {
			Assert::AreNotSame(expr_, std::forward<Args>(args)...);
			return{};
		}

		template<class ... Args>
		empry_expression_t isNull(Args&& ... args) {
			Assert::IsNull(expr_, std::forward<Args>(args)...);
			return{};
		}

		template<class ... Args>
		empry_expression_t isNotNull(Args&& ... args) {
			Assert::IsNotNull(expr_, std::forward<Args>(args)...);
			return{};
		}

		template<class ... Args>
		empry_expression_t fail(Args&& ... args) {
			Assert::Fail(std::forward<Args>(args)...);
			return{};
		}

		template<class ... Args>
		empry_expression_t ExpectException(Args&& ... args) {
			Assert::ExpectException(expr_, std::forward<Args>(args)...);
			return{};
		}

	private:
		Expr expr_;
	};

	// Functor to build an Asserter with template deduction
	template<class Expr>
	Expression<Expr> AssertThat(Expr&& expr) {
		return{ std::forward<Expr>(expr) };
	}

}

//Helper macros to use the unit test suit
#define register_scenario(name, description, ...) \
	namespace H2OFastTests { \
		registry_storage_t& get_registry_storage_ ## name() { \
			static registry_storage_t registry_storage; \
			return registry_storage; \
		} \
		static registry_manager_t registry_manager_ ## name {description, &get_registry_storage_ ## name, __VA_ARGS__ }; \
	}
#define run_scenario(name) \
	H2OFastTests::registry_manager_ ## name.run_tests();

#define describe_test(label, test) \
	H2OFastTests::detail::make_test(label, (test))

#define skip_test(test) \
	H2OFastTests::detail::make_skipped_test((test))
#define skip_test_reason(reason, test) \
	H2OFastTests::detail::make_skipped_test(reason, (test))

#define add_test_to_scenario(name, made_test) \
	H2OFastTests::registry_manager_ ## name.push_back(made_test);

#define register_observer(name, class_name, instance_ptr) \
	H2OFastTests::registry_manager_ ## name.addObserver(std::shared_ptr<class_name>(instance_ptr));

#define print_result(name, verbose) \
	H2OFastTests::RegistryTraversal_ConsoleIO(H2OFastTests::registry_manager_ ## name).print(std::cout, (verbose))

#define line_info() \
	&H2OFastTests::line_info_t(__FILE__, "", __LINE__)
#define line_info_f() \
	&H2OFastTests::line_info_t(__FILE__, __FUNCTION__, __LINE__)

#endif
