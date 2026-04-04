load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "spsc_queue_test",
    srcs = ["spsc_queue_test.cc"],
    deps = ["//include/common:spsc_queue", "@googletest//:gtest", "@googletest//:gtest_main"]
)
