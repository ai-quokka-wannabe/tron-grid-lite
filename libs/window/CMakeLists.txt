# Copyright (C) 2026 The AiQuokkaWannabe Authors <https://github.com/ai-quokka-wannabe/tron-grid-lite>
#
# This program is free software: you can redistribute it and/or modify it under the terms of
# the GNU General Public License as published by the Free Software Foundation, either version
# 3 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
# without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with this program.
# If not, see <https://www.gnu.org/licenses/>.

if(WIN32)
    set(PLATFORM_LIBS "")
    set(PLATFORM_SOURCES src/win32_window.cpp)
elseif(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)
    set(PLATFORM_LIBS PkgConfig::XCB)
    set(PLATFORM_SOURCES src/xcb_window.cpp)
endif()

add_library(window STATIC
    src/window.cpp
    ${PLATFORM_SOURCES}
)

target_include_directories(window
    PUBLIC  include/
    PRIVATE src/
)

target_compile_features(window PUBLIC cxx_std_20)

target_link_libraries(window
    PUBLIC logging
    PRIVATE ${PLATFORM_LIBS}
)

add_subdirectory(tests)
