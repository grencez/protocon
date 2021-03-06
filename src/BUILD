load("@rules_foreign_cc//tools/build_defs:make.bzl", "make")
load("@rules_foreign_cc//tools/build_defs:cmake.bzl", "cmake_external")

exports_files([
    "namespace.hh",
])

cmake_external(
    name = "mdd_glu_lib",
    lib_source = "@mdd_glu//:all",
    static_libraries = [
        "libcu.a",
        "libglu.a",
    ],
)

make(
    name = "peg",
    lib_source = "@peg//:all",
    binaries = [
        "peg",
        "leg",
    ],
)
filegroup(
    name = "peg_leg",
    srcs = [":peg"],
    output_group = "leg",
)

genrule(
    name = "prot_xfile_leg",
    srcs = ["prot-xfile.leg"],
    tools = [":peg_leg"],
    cmd = "$(location peg_leg) -o \"$@\" $(location prot-xfile.leg)",
    outs = ["prot-xfile.leg.cc"],
)


cc_binary(
    name = "biring",
    srcs = [
        "biring.cc",
    ],
    deps = ["//src/cx:cx_lib"],
    copts = [
        "-Iexternal/lace/src",
    ],
)

cc_test(
    name = "conflictfamily_test",
    srcs = [
        "conflictfamily.cc",
        "conflictfamily.hh",
        "conflictfamily_test.cc",
        "namespace.hh",
    ],
    deps = ["//src/cx:cx_lib"],
    copts = [
        "-Iexternal/lace/src",
    ],
)


cc_library(
    name = "pfmla_glu_lib",
    srcs = [
        "pfmla-glu.c",
        "pfmla-glu.h",
        "pfmla.h",
    ],
    deps = [
        ":mdd_glu_lib",
        "//src/cx:cx_lib",
    ],
    copts = [
        "-Iexternal/lace/src",
        "-Iexternal/mdd_glu/include",
    ],
)

cc_library(
    name = "pfmla_lib",
    srcs = [
        "pfmla.c",
        "pfmla.cc",
        "pfmla.h",
        "pfmla.hh",
    ],
    deps = [
        ":pfmla_glu_lib",
    ],
    copts = [
        "-Iexternal/lace/src",
    ],
    visibility = [
        "//src/uni:__pkg__",
    ],
)

cc_test(
    name = "pfmla_test",
    srcs = [
        "namespace.hh",
        "pfmla_test.cc",
    ],
    deps = [
        ":pfmla_lib",
    ],
    copts = [
        "-Iexternal/lace/src",
    ],
)


cc_library(
    name = "xnsys_lib",
    srcs = [
        "namespace.hh",
        "xfmlae.cc",
        "xfmlae.hh",
        "xnsys.cc",
        "xnsys.hh",
        "xnpc.cc",
        "xnpc.hh",
        "xnspec.cc",
        "xnspec.hh",
    ],
    deps = [
        ":pfmla_lib",
    ],
    copts = [
        "-Iexternal/lace/src",
    ],
)

cc_test(
    name = "xnsys_test",
    srcs = [
        "inst.cc",
        "inst.hh",
        "namespace.hh",
        "xnpc.hh",
        "xnsys.hh",
        "xfmlae.hh",
        "xnspec.hh",
        "xnsys_test.cc",
    ],
    deps = [
        ":xnsys_lib",
    ],
    copts = [
        "-Iexternal/lace/src",
    ],
)

cc_test(
    name = "stabilization_test",
    srcs = [
        "inst.cc",
        "inst.hh",
        "stabilization.cc",
        "stabilization.hh",
        "stabilization_test.cc",
        "namespace.hh",
    ],
    deps = [":xnsys_lib"],
    copts = [
        "-Iexternal/lace/src",
    ],
)


cc_library(
    name = "prot_file_lib",
    srcs = [
        "namespace.hh",
        "pla.cc",
        "pla.hh",
        "prot-ofile.cc",
        "prot-ofile.hh",
        "prot-xfile.cc",
        "prot-xfile.hh",
        "prot-xfile.leg.cc",
    ],
    deps = [
        ":xnsys_lib",
    ],
    copts = [
        "-Isrc",
        "-Iexternal/lace/src",
    ],
    visibility = [
        "//src/uni:__pkg__",
    ],
)

cc_binary(
    name = "deadfree",
    srcs = [
        "deadfree.cc",
        "namespace.hh",
    ],
    deps = [":prot_file_lib"],
    copts = [
        "-Iexternal/lace/src",
    ],
)

cc_library(
    name = "protocon_lib",
    srcs = [
        "conflictfamily.cc",
        "conflictfamily.hh",
        "graphviz.cc",
        "graphviz.hh",
        "inst.cc",
        "inst.hh",
        "interactive.cc",
        "interactive.hh",
        "namespace.hh",
        "opt.cc",
        "opt.hh",
        "pfmla-bittable.c",
        "pfmla-bittable.h",
        "promela.cc",
        "promela.hh",
        "search.hh",
        "stabilization.cc",
        "stabilization.hh",
        "synthesis.cc",
        "synthesis.hh",
        "udp-ofile.cc",
        "udp-ofile.hh",
        "//src/udp-impl:act.embed.h",
    ],
    deps = [
        ":prot_file_lib",
    ],
    copts = [
        "-Iexternal/lace/src",
    ],
    # This lets udp-ofile.cc include the generated "udp-impl/act.embed.h" file.
    # Ideally we'd use copts to avoid adding this include path to everything
    # that depends on protocon_lib.
    includes = [
        ".",
    ],
)

cc_binary(
    name = "protocon",
    srcs = [
        "main.cc",
        "main-all.hh",
        "namespace.hh",
        "search.cc",
        "search.hh",
    ],
    deps = [
        ":protocon_lib",
    ],
    copts = [
        "-Iexternal/lace/src",
    ],
)

genrule(
    name = "test_embed_h",
    srcs = [
        "testlist.txt",
    ],
    tools = ["@lace//tool:cswitch"],
    cmd = "$(location @lace//tool:cswitch) testname -case-pfx \"fn = Test\" -array AllTests -x $(location testlist.txt) -o \"$@\"",
    outs = ["test.embed.h"],
)


cc_test(
    name = "test",
    srcs = [
        "kautz.cc",
        "kautz.hh",
        "search.cc",
        "search.hh",
        "test.cc",
        "test-pcxn.cc",
        "test-pcxn.hh",
        "test.embed.h",
    ],
    deps = [
        ":protocon_lib",
    ],
    data = [
        "//examplesoln:ColorUniRing.prot",
        "//examplespec:LeaderRingHuang.prot",
        "//examplespec:SumNotTwo.prot",
    ],
    copts = [
        "-Iexternal/lace/src",
    ],
)
