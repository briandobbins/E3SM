if (compile_threaded)
  string(APPEND CFLAGS " -Kopenmp")
endif()
if (DEBUG)
  string(APPEND CFLAGS " -g -Nquickdbg=subchk")
endif()
if (NOT DEBUG)
  string(APPEND CFLAGS " -O2")
endif()
if (COMP_NAME STREQUAL csm_share)
  string(APPEND CFLAGS " -std=c99")
endif()
string(APPEND CXXFLAGS " -std=c++14")
if (compile_threaded)
  string(APPEND CXXFLAGS " -Kopenmp")
endif()
if (DEBUG)
  string(APPEND CXXFLAGS " -g -Nquickdbg=subchk")
endif()
if (NOT DEBUG)
  string(APPEND CXXFLAGS " -O2")
endif()
string(APPEND CPPDEFS " -DFORTRANUNDERSCORE -DNO_R16 -DCPRFJ")
if (DEBUG)
  string(APPEND CPPDEFS "-DYAKL_DEBUG")
endif()
set(CXX_LINKER "FORTRAN")
set(FC_AUTO_R8 "-CcdRR8")
if (compile_threaded)
  string(APPEND FFLAGS " -Kopenmp")
endif()
if (DEBUG)
  string(APPEND FFLAGS " -g -Haefosux")
endif()
if (NOT DEBUG)
  string(APPEND FFLAGS " -O2")
endif()
set(FFLAGS_NOOPT " -O0")
set(FIXEDFLAGS "-Fixed")
set(FREEFLAGS "-Free")
set(HAS_F2008_CONTIGUOUS "FALSE")
if (compile_threaded)
  string(APPEND LDFLAGS " -Kopenmp")
endif()
set(MPICC "mpifcc")
set(MPICXX "mpiFCC")
set(MPIFC "mpifrt")
set(SCC "fcc")
set(SCXX "FCC")
set(SFC "frt")
set(SUPPORTS_CXX "TRUE")
set(CXX_LIBS "--linkstl=libfjc++")
set(NETCDF_C_PATH "$ENV{NETCDF_C_PATH}")
set(NETCDF_FORTRAN_PATH "$ENV{NETCDF_FORTRAN_PATH}")
set(PNETCDF_PATH "$ENV{PNETCDF_PATH}")
execute_process(COMMAND $ENV{NETCDF_FORTRAN_PATH}/bin/nf-config --flibs OUTPUT_VARIABLE SHELL_CMD_OUTPUT_BUILD_INTERNAL_IGNORE0 OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND $ENV{NETCDF_C_PATH}/bin/nc-config --libs OUTPUT_VARIABLE SHELL_CMD_OUTPUT_BUILD_INTERNAL_IGNORE1 OUTPUT_STRIP_TRAILING_WHITESPACE)
string(APPEND SLIBS " ${SHELL_CMD_OUTPUT_BUILD_INTERNAL_IGNORE0} ${SHELL_CMD_OUTPUT_BUILD_INTERNAL_IGNORE1} -lblas -llapack")
if (COMP_NAME MATCHES "^pio")
  string(APPEND CMAKE_OPTS " -DPIO_ENABLE_TOOLS:BOOL=OFF -DMPIFJ")
endif()
