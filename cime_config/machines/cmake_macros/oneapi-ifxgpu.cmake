if (compile_threaded)
  string(APPEND CFLAGS   " -qopenmp")
  string(APPEND FFLAGS   " -qopenmp")
  string(APPEND CXXFLAGS " -qopenmp")
  string(APPEND LDFLAGS  " -qopenmp")
endif()
if (NOT DEBUG)
  string(APPEND CFLAGS   " -O2")
  string(APPEND FFLAGS   " -O2")
  string(APPEND CXXFLAGS " -O2")
endif()
if (DEBUG)
  string(APPEND FFLAGS   " -O0 -g -check uninit -check bounds -check pointers -fpe0 -check noarg_temp_created")
  string(APPEND CFLAGS   " -O0 -g")
  string(APPEND CXXFLAGS " -O0 -g")
endif()
string(APPEND CFLAGS   " -traceback -fp-model precise -std=gnu99")
string(APPEND CXXFLAGS " -traceback -fp-model precise")
string(APPEND FFLAGS   " -traceback -convert big_endian -assume byterecl -assume realloc_lhs -fp-model precise")
set(SUPPORTS_CXX "TRUE")
string(APPEND CPPDEFS " -DFORTRANUNDERSCORE -DNO_R16 -DCPRINTEL -DHAVE_SLASHPROC -DHIDE_MPI")
string(APPEND FC_AUTO_R8 " -r8")
string(APPEND FFLAGS_NOOPT " -O0")
string(APPEND FIXEDFLAGS " -fixed -132")
string(APPEND FREEFLAGS " -free")
set(HAS_F2008_CONTIGUOUS "TRUE")
set(MPIFC "mpifort")
set(MPICC "mpicc")
set(MPICXX "mpicxx")
set(SCC "icx")
set(SCXX "icpx")
set(SFC "ifx")
