load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "spsc_queue_bench",
    srcs = ["spsc_queue_bench.cc"],
    deps = ["//include/common:spsc_queue", "//src/common:utils", "@google_benchmark//:benchmark"],
)