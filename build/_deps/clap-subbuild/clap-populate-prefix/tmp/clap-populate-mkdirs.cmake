# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/Users/Yusuf/Documents/Antigravity Workspaces/MidiCapture/build/_deps/clap-src")
  file(MAKE_DIRECTORY "C:/Users/Yusuf/Documents/Antigravity Workspaces/MidiCapture/build/_deps/clap-src")
endif()
file(MAKE_DIRECTORY
  "C:/Users/Yusuf/Documents/Antigravity Workspaces/MidiCapture/build/_deps/clap-build"
  "C:/Users/Yusuf/Documents/Antigravity Workspaces/MidiCapture/build/_deps/clap-subbuild/clap-populate-prefix"
  "C:/Users/Yusuf/Documents/Antigravity Workspaces/MidiCapture/build/_deps/clap-subbuild/clap-populate-prefix/tmp"
  "C:/Users/Yusuf/Documents/Antigravity Workspaces/MidiCapture/build/_deps/clap-subbuild/clap-populate-prefix/src/clap-populate-stamp"
  "C:/Users/Yusuf/Documents/Antigravity Workspaces/MidiCapture/build/_deps/clap-subbuild/clap-populate-prefix/src"
  "C:/Users/Yusuf/Documents/Antigravity Workspaces/MidiCapture/build/_deps/clap-subbuild/clap-populate-prefix/src/clap-populate-stamp"
)

set(configSubDirs Debug)
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/Yusuf/Documents/Antigravity Workspaces/MidiCapture/build/_deps/clap-subbuild/clap-populate-prefix/src/clap-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/Yusuf/Documents/Antigravity Workspaces/MidiCapture/build/_deps/clap-subbuild/clap-populate-prefix/src/clap-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
