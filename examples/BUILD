load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "container_benchmark",
    srcs = ["container_benchmark.cpp"],
    copts = [
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wpedantic",
    ],
    deps = [
        "//common:perf_utils",
    ],
)

cc_binary(
    name = "thread_example",
    srcs = ["thread_example.cpp"],
    copts = [
        "-Wall",
        "-Wextra",
        "-Werror",
        "-Wpedantic",
    ],
    linkopts = ["-pthread"],
)
