load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "time_utils_test",
    srcs = ["time_utils_test.cpp"],
    copts = [
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wpedantic",
    ],
    deps = [
        "//common:time_utils",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "perf_utils_test",
    srcs = ["perf_utils_test.cpp"],
    copts = [
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wpedantic",
    ],
    deps = [
        "//common:perf_utils",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
