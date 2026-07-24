# Smart HVAC register firmware — ESP-IDF (v5.3+) project, target esp32c6.
#
#   idf.py set-target esp32c6
#   idf.py build flash monitor
#
# Host unit tests for the pure-logic modules live in test/host (plain gcc,
# no ESP-IDF needed): `make -C test/host test`.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(smart_register)
