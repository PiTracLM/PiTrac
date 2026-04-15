/*
 * Camera Bounded Context - Test Main Entry Point
 *
 * Boost Test framework main for Camera bounded context unit tests.
 */

#define BOOST_TEST_MODULE CameraBoundedContextTests

// Support both header-only and linked Boost Test modes
#ifdef BOOST_TEST_HEADER_ONLY
    #include <boost/test/included/unit_test.hpp>
#else
    #include <boost/test/unit_test.hpp>
#endif

// Global test setup and teardown can be added here if needed
struct GlobalTestFixture {
    GlobalTestFixture() {
        // Global test initialization
        BOOST_TEST_MESSAGE("Starting Camera Bounded Context Tests");
    }

    ~GlobalTestFixture() {
        // Global test cleanup
        BOOST_TEST_MESSAGE("Completed Camera Bounded Context Tests");
    }
};

BOOST_GLOBAL_FIXTURE(GlobalTestFixture);
