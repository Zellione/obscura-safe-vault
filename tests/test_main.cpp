#include "test_framework.h"

#include <print>

int main()
{
    std::println("Running osv crypto tests...\n");
    return ::testing::run_all_tests();
}
