#include "catch2/catch.hpp"

#include "shoc_unit_tests_common.hpp"
#include "physics/shoc/shoc_functions.hpp"
#include "physics/shoc/shoc_functions_f90.hpp"
#include "physics/share/physics_constants.hpp"
#include "share/scream_types.hpp"

#include "ekat/ekat_pack.hpp"
#include "ekat/util/ekat_arch.hpp"
#include "ekat/kokkos/ekat_kokkos_utils.hpp"

#include <algorithm>
#include <array>
#include <random>
#include <thread>

namespace scream {
namespace shoc {
namespace unit_test {

template <typename D>
struct UnitWrap::UnitTest<D>::TestCompShocMixLength {

  static void run_property()
  {
    static constexpr Int shcol    = 3;
    static constexpr Int nlev     = 5;

    // Tests for the SHOC function:
    //   compute_shoc_mix_shoc_length

    // Multi column test will verify that 1) mixing length increases
    // with height given brunt vaisalla frequency and
    // TKE are constant with height and 2) Columns with larger
    // TKE values produce a larger length scale value

    // Define TKE [m2/s2] that will be used for each column
    static constexpr Real tke_cons = 0.1;
    // Define the brunt vasailla frequency [s-1]
    static constexpr Real brunt_cons = 0.001;
    // Define the assymptoic length [m]
    static constexpr Real l_inf = 100;
    // Define the overturning timescale [s]
    static constexpr Real tscale = 300;
    // Define the heights on the zt grid [m]
    static constexpr Real zt_grid[nlev] = {5000, 3000, 2000, 1000, 500};

    // Initialize data structure for bridging to F90
    SHOCMixlengthData SDS(shcol, nlev);

    // Test that the inputs are reasonable.
    // For this test shcol MUST be at least 2
    REQUIRE( (SDS.shcol() == shcol && SDS.nlev() == nlev) );
    REQUIRE(shcol > 1);

    // Fill in test data on zt_grid.
    for(Int s = 0; s < shcol; ++s) {
      SDS.l_inf[s] = l_inf;
      SDS.tscale[s] = tscale;
      for(Int n = 0; n < nlev; ++n) {
        const auto offset = n + s * nlev;

        // for the subsequent columns, increase
        //  the amount of TKE
        SDS.tke[offset] = (1.0+s)*tke_cons;
        SDS.brunt[offset] = brunt_cons;
        SDS.zt_grid[offset] = zt_grid[n];
      }
    }

    // Check that the inputs make sense

    // Be sure that relevant variables are greater than zero
    for(Int s = 0; s < shcol; ++s) {
      REQUIRE(SDS.l_inf[s] > 0);
      REQUIRE(SDS.tscale[s] > 0);
      for(Int n = 0; n < nlev; ++n) {
        const auto offset = n + s * nlev;
        REQUIRE(SDS.tke[offset] > 0);
        REQUIRE(SDS.zt_grid[offset] > 0);
        if (s < shcol-1){
          // Verify that tke is larger column by column
          const auto offsets = n + (s+1) * nlev;
          REQUIRE(SDS.tke[offset] < SDS.tke[offsets]);
        }
      }

      // Check that zt increases upward
      for(Int n = 0; n < nlev - 1; ++n) {
        const auto offset = n + s * nlev;
        REQUIRE(SDS.zt_grid[offset + 1] - SDS.zt_grid[offset] < 0);
      }
    }

    // Call the fortran implementation
    compute_shoc_mix_shoc_length(SDS);

    // Check the results
    for(Int s = 0; s < shcol; ++s) {
      for(Int n = 0; n < nlev; ++n) {
        const auto offset = n + s * nlev;
        // Validate shoc_mix greater than zero everywhere
        REQUIRE(SDS.shoc_mix[offset] > 0);
        if (s < shcol-1){
          // Verify that mixing length increases column by column
          const auto offsets = n + (s+1) * nlev;
          REQUIRE(SDS.shoc_mix[offset] < SDS.shoc_mix[offsets]);
        }
      }

      // Check that mixing length increases upward
      for(Int n = 0; n < nlev - 1; ++n) {
        const auto offset = n + s * nlev;
        REQUIRE(SDS.shoc_mix[offset + 1] - SDS.shoc_mix[offset] < 0);
      }
    }
  }

  static void run_bfb()
  {
    SHOCMixlengthData SDS_f90[] = {
      //               shcol, nlev
      SHOCMixlengthData(10, 71),
      SHOCMixlengthData(10, 12),
      SHOCMixlengthData(7,  16),
      SHOCMixlengthData(2, 7)
    };

    static constexpr Int num_runs = sizeof(SDS_f90) / sizeof(SHOCMixlengthData);

    // Generate random input data
    for (auto& d : SDS_f90) {
      d.randomize();
    }

    // Create copies of data for use by cxx. Needs to happen before fortran calls so that
    // inout data is in original state
    SHOCMixlengthData SDS_cxx[] = {
      SHOCMixlengthData(SDS_f90[0]),
      SHOCMixlengthData(SDS_f90[1]),
      SHOCMixlengthData(SDS_f90[2]),
      SHOCMixlengthData(SDS_f90[3]),
    };

    // Assume all data is in C layout

    // Get data from fortran
    for (auto& d : SDS_f90) {
      // expects data in C layout
      compute_shoc_mix_shoc_length(d);
    }

    // Get data from cxx
    for (auto& d : SDS_cxx) {
      d.transpose<ekat::TransposeDirection::c2f>();
      // expects data in fortran layout
      compute_shoc_mix_shoc_length_f(d.nlev(), d.shcol(),
                                     d.tke, d.brunt,
                                     d.tscale, d.zt_grid,
                                     d.l_inf, d.shoc_mix);
      d.transpose<ekat::TransposeDirection::f2c>();
    }

    // Verify BFB results, all data should be in C layout
    for (Int i = 0; i < num_runs; ++i) {
      SHOCMixlengthData& d_f90 = SDS_f90[i];
      SHOCMixlengthData& d_cxx = SDS_cxx[i];
      for (Int k = 0; k < d_f90.total1x2(); ++k) {
        REQUIRE(d_f90.shoc_mix[k] == d_cxx.shoc_mix[k]);
      }
    }
  }
};

}  // namespace unit_test
}  // namespace shoc
}  // namespace scream

namespace {

TEST_CASE("shoc_mix_length_property", "shoc")
{
  using TestStruct = scream::shoc::unit_test::UnitWrap::UnitTest<scream::DefaultDevice>::TestCompShocMixLength;

  TestStruct::run_property();
}

TEST_CASE("shoc_mix_length_bfb", "shoc")
{
  using TestStruct = scream::shoc::unit_test::UnitWrap::UnitTest<scream::DefaultDevice>::TestCompShocMixLength;

  TestStruct::run_bfb();
}

} // namespace