#include "catch2/catch.hpp"

#include "share/scream_types.hpp"
#include "share/util/scream_utils.hpp"
#include "share/scream_kokkos.hpp"
#include "share/scream_pack.hpp"
#include "physics/p3/p3_functions.hpp"
#include "physics/p3/p3_functions_f90.hpp"
#include "share/util/scream_kokkos_utils.hpp"

#include "p3_unit_tests_common.hpp"

#include <thread>
#include <array>
#include <algorithm>
#include <random>

namespace scream {
namespace p3 {
namespace unit_test {

template <typename D>
struct UnitWrap::UnitTest<D>::TestIceSed {

static void run_phys()
{
  // TODO
}

static void run_bfb_calc_bulk_rhime()
{
  // TODO
}

static void run_bfb_ice_sed()
{
  // TODO
}

static void run_bfb()
{
  run_bfb_calc_bulk_rhime();
  run_bfb_ice_sed();
}

};

}
}
}

namespace {

TEST_CASE("p3_ice_sed", "[p3_functions]")
{
  using TCS = scream::p3::unit_test::UnitWrap::UnitTest<scream::DefaultDevice>::TestIceSed;

  TCS::run_phys();
  TCS::run_bfb();

  scream::p3::P3GlobalForFortran::deinit();
}

} // namespace
