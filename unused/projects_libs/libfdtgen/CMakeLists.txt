# Copyright 2019, Data61
# Commonwealth Scientific and Industrial Research Organisation (CSIRO)
# ABN 41 687 119 230.
#
# This software may be distributed and modified according to the terms of
# the BSD 2-Clause license. Note that NO WARRANTY is provided.
# See "LICENSE_BSD2.txt" for details.
#
# @TAG(DATA61_BSD)
#

cmake_minimum_required(VERSION 3.7.2)

project(libfdtgen C)

add_compile_options(-O2)
target_compile_options(fdt PRIVATE -O2)
add_library(fdtgen STATIC EXCLUDE_FROM_ALL fdtgen.c)
target_include_directories(fdtgen PUBLIC include)
target_link_libraries(fdtgen muslc utils fdt)
