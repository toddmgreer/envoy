licenses(["notice"])  # Apache 2

## Pluggable HTTP cache filter

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
    "envoy_proto_library",
)

envoy_package()

envoy_cc_library(
    name = "cache_filter_lib",
    srcs = ["cache_filter.cc"],
    hdrs = ["cache_filter.h"],
    deps = [
        ":http_cache_lib",
        "//include/envoy/registry",
        "//source/common/common:logger_lib",
        "//source/common/common:macros",
        "//source/common/config:utility_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/protobuf",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/config/filter/http/cache/v2:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream_and_upstream",
    status = "wip",
    deps = [
        ":cache_filter_lib",
        ":key_cc_proto",
        "//include/envoy/common:time_interface",
        "//include/envoy/registry",
        "//include/envoy/stats:stats_interface",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/config/filter/http/cache/v2:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "key",
    srcs = ["key.proto"],
)

envoy_cc_library(
    name = "http_cache_lib",
    srcs = ["http_cache.cc"],
    hdrs = ["http_cache.h"],
    deps = [
        ":http_cache_utils_lib",
        ":key_cc_proto",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/common:time_interface",
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/http:header_map_interface",
        "//source/common/common:assert_lib",
        "//source/common/http:headers_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/filter/http/cache/v2:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "http_cache_utils_lib",
    srcs = ["http_cache_utils.cc"],
    hdrs = ["http_cache_utils.h"],
    deps = [
        "//include/envoy/common:time_interface",
        "//include/envoy/http:header_map_interface",
    ],
)

envoy_cc_library(
    name = "simple_http_cache_lib",
    srcs = ["simple_http_cache.cc"],
    hdrs = ["simple_http_cache.h"],
    deps = [
        ":http_cache_lib",
        "//include/envoy/registry",
        "//include/envoy/runtime:runtime_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:macros",
        "//source/common/common:thread_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/protobuf",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)
