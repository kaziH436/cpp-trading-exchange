load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "perf_utils",
    hdrs = ["perf_utils.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "time_utils",
    hdrs = ["time_utils.h"],
    visibility = ["//visibility:public"],
    deps = [":perf_utils"],
)
