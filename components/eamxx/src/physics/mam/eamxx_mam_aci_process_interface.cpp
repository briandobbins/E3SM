#include <physics/mam/eamxx_mam_aci_process_interface.hpp>

namespace scream {

namespace {
KOKKOS_INLINE_FUNCTION
void copy_scream_array_to_mam4xx(const haero::ThreadTeam &team,
                                 const MAMAci::view_2d mam4xx_view,
                                 MAMAci::const_view_3d scream_view,
                                 const int icol, const int nlev,
                                 const int view_num) {
  Kokkos::parallel_for(
      Kokkos::TeamThreadRange(team, 0u, nlev), KOKKOS_LAMBDA(int kk) {
        mam4xx_view(icol, kk) = scream_view(icol, kk, view_num);
      });
}

void copy_scream_array_to_mam4xx(
    haero::ThreadTeamPolicy team_policy,
    MAMAci::view_2d mam4xx_views[mam4::ndrop::ncnst_tot],
    MAMAci::const_view_3d scream_view, const int nlev) {
  // Localize the input arrays.
  MAMAci::view_2d mam4xx[mam4::ndrop::ncnst_tot];
  for(int view_num = 0; view_num < mam4::ndrop::ncnst_tot; ++view_num)
    mam4xx[view_num] = mam4xx_views[view_num];
  Kokkos::parallel_for(
      team_policy, KOKKOS_LAMBDA(const haero::ThreadTeam &team) {
        const int icol = team.league_rank();
        for(int view_num = 0; view_num < mam4::ndrop::ncnst_tot; ++view_num) {
          MAMAci::view_2d mam4xx_view = mam4xx[view_num];
          copy_scream_array_to_mam4xx(team, mam4xx_view, scream_view, icol,
                                      nlev, view_num);
        }
      });
}

KOKKOS_INLINE_FUNCTION
void compute_w0_and_rho(const haero::ThreadTeam &team,
                        const MAMAci::const_view_2d omega,
                        const MAMAci::const_view_2d T_mid,
                        const MAMAci::const_view_2d p_mid, const int icol,
                        const int top_lev, const int nlev,
                        // output
                        MAMAci::view_2d w0, MAMAci::view_2d rho) {
  // Get physical constants
  using C                      = physics::Constants<Real>;
  static constexpr auto gravit = C::gravit;  // Gravity [m/s2]
  // Gas constant for dry air [J/(kg*K) or J/Kg/K]
  static constexpr auto rair = C::Rair;
  Kokkos::parallel_for(
      Kokkos::TeamThreadRange(team, 0u, top_lev), KOKKOS_LAMBDA(int kk) {
        w0(icol, kk)  = 0;
        rho(icol, kk) = -999.0;
      });
  Kokkos::parallel_for(
      Kokkos::TeamThreadRange(team, top_lev, nlev), KOKKOS_LAMBDA(int kk) {
        rho(icol, kk) = p_mid(icol, kk) / (rair * T_mid(icol, kk));
        w0(icol, kk)  = -1.0 * omega(icol, kk) / (rho(icol, kk) * gravit);
      });
}
void compute_w0_and_rho(haero::ThreadTeamPolicy team_policy,
                        const mam_coupling::DryAtmosphere &dry_atmosphere,
                        const int top_lev, const int nlev,
                        // output
                        MAMAci::view_2d w0, MAMAci::view_2d rho) {
  MAMAci::const_view_2d omega = dry_atmosphere.omega;
  MAMAci::const_view_2d T_mid = dry_atmosphere.T_mid;
  MAMAci::const_view_2d p_mid = dry_atmosphere.p_mid;
  Kokkos::parallel_for(
      team_policy, KOKKOS_LAMBDA(const haero::ThreadTeam &team) {
        const int icol = team.league_rank();
        compute_w0_and_rho(team, omega, T_mid, p_mid, icol, top_lev, nlev,
                           // output
                           w0, rho);
      });
}

KOKKOS_INLINE_FUNCTION
void compute_tke_using_w_sec(const haero::ThreadTeam &team,
                             const MAMAci::const_view_2d w_sec, const int icol,
                             const int nlev,
                             // output
                             MAMAci::view_2d tke) {
  // FIXME Is this the correct boundary condition for tke at the surface?
  // TKE seems to be at interfaces but w_sec is at cell centers so this
  // descrepensy needs to be worked out.
  Kokkos::parallel_for(
      Kokkos::TeamThreadRange(team, 0u, nlev + 1),
      KOKKOS_LAMBDA(int kk) { tke(icol, kk) = (3.0 / 2.0) * w_sec(icol, kk); });
}
void compute_tke_using_w_sec(haero::ThreadTeamPolicy team_policy,
                             const MAMAci::const_view_2d w_sec, const int nlev,
                             // output
                             MAMAci::view_2d tke) {
  Kokkos::parallel_for(
      team_policy, KOKKOS_LAMBDA(const haero::ThreadTeam &team) {
        const int icol = team.league_rank();
        compute_tke_using_w_sec(team, w_sec, icol, nlev,
                                // output
                                tke);
      });
}
KOKKOS_INLINE_FUNCTION
void compute_subgrid_scale_velocities(
    const haero::ThreadTeam &team, const MAMAci::const_view_2d tke,
    const Real wsubmin, const int icol, const int top_lev, const int nlev,
    // output
    MAMAci::view_2d wsub, MAMAci::view_2d wsubice, MAMAci::view_2d wsig) {
  // More refined computation of sub-grid vertical velocity
  // Set to be zero at the surface by initialization.
  Kokkos::parallel_for(
      Kokkos::TeamThreadRange(team, 0u, top_lev), KOKKOS_LAMBDA(int kk) {
        wsub(icol, kk)    = wsubmin;
        wsubice(icol, kk) = 0.001;
        wsig(icol, kk)    = 0.001;
      });
  Kokkos::parallel_for(
      Kokkos::TeamThreadRange(team, top_lev, nlev), KOKKOS_LAMBDA(int kk) {
        wsub(icol, kk) = haero::sqrt(0.5 * (tke(icol, kk) + tke(icol, kk + 1)) *
                                     (2.0 / 3.0));
        wsig(icol, kk) =
            mam4::utils::min_max_bound(0.001, 10.0, wsub(icol, kk));
        wsubice(icol, kk) =
            mam4::utils::min_max_bound(0.2, 10.0, wsub(icol, kk));
        wsub(icol, kk) = haero::max(wsubmin, wsub(icol, kk));
      });
}
void compute_subgrid_scale_velocities(
    haero::ThreadTeamPolicy team_policy, const MAMAci::const_view_2d tke,
    const Real wsubmin, const int top_lev, const int nlev,
    // output
    MAMAci::view_2d wsub, MAMAci::view_2d wsubice, MAMAci::view_2d wsig) {
  Kokkos::parallel_for(
      team_policy, KOKKOS_LAMBDA(const haero::ThreadTeam &team) {
        const int icol = team.league_rank();
        compute_subgrid_scale_velocities(team, tke, wsubmin, icol, top_lev,
                                         nlev,
                                         // output
                                         wsub, wsubice, wsig);
      });
}

KOKKOS_INLINE_FUNCTION
void compute_aitken_dry_diameter(const haero::ThreadTeam &team,
                                 const MAMAci::const_view_3d dgnum,
                                 const int icol, const int top_lev,
                                 const int nlev,
                                 // output
                                 MAMAci::view_2d aitken_dry_dia) {
  const int aitken_idx = static_cast<int>(mam4::ModeIndex::Aitken);
  Kokkos::parallel_for(
      Kokkos::TeamThreadRange(team, top_lev, nlev), KOKKOS_LAMBDA(int kk) {
        aitken_dry_dia(icol, kk) = dgnum(icol, aitken_idx, kk);
      });
}
void compute_aitken_dry_diameter(haero::ThreadTeamPolicy team_policy,
                                 const MAMAci::const_view_3d dgnum,
                                 const int top_lev, const int nlev,
                                 // output
                                 MAMAci::view_2d aitken_dry_dia) {
  Kokkos::parallel_for(
      team_policy, KOKKOS_LAMBDA(const haero::ThreadTeam &team) {
        const int icol = team.league_rank();
        compute_aitken_dry_diameter(team, dgnum, icol, top_lev, nlev,
                                    // output
                                    aitken_dry_dia);
      });
}

void compute_nucleate_ice_tendencies(
    const mam4::NucleateIce &nucleate_ice, haero::ThreadTeamPolicy team_policy,
    const mam_coupling::DryAtmosphere &dry_atmosphere,
    const mam_coupling::AerosolState &dry_aerosol_state,
    const MAMAci::view_2d wsubice, const MAMAci::view_2d aitken_dry_dia,
    const int nlev, const double dt,
    // output
    MAMAci::view_2d nihf, MAMAci::view_2d niim, MAMAci::view_2d nidep,
    MAMAci::view_2d nimey, MAMAci::view_2d naai_hom, MAMAci::view_2d naai) {
  //-------------------------------------------------------------
  // Get number of activated aerosol for ice nucleation (naai)
  // from ice nucleation
  //-------------------------------------------------------------

  Kokkos::parallel_for(
      team_policy, KOKKOS_LAMBDA(const haero::ThreadTeam &team) {
        const int icol = team.league_rank();
        // for(int icol = 0; icol < 5; ++icol) {
        //  std::cout<<""<<std::endl;
        //  std::cout<<"ICOL:"<<icol<<" : "<<aitken_dry_dia(icol,62)<<std::endl;
        //---------------------------------------------------------------------
        //   Set up surface, pronostics atmosphere, diagnostics, and tendencies
        //   classes.
        //---------------------------------------------------------------------

        // For compute_tendecies interface only, this structure is empty
        haero::Surface surf{};

        // Store interstitial and cld borne aerosols in "progrs" struture
        mam4::Prognostics progs =
            mam_coupling::aerosols_for_column(dry_aerosol_state, icol);

        // Store atmopsheric vars (Tmid, Pmid, cloud fraction, qv, wsubmin)
        haero::Atmosphere haero_atm =
            atmosphere_for_column(dry_atmosphere, icol);

        // Update the updraft velocity needed by nucleation to be "wsubice"
        // in the haero_atm object
        haero_atm.updraft_vel_ice_nucleation = ekat::subview(wsubice, icol);

        // All the output from this process is diagnotics; creates "diags" with
        // nlev column length
        mam4::Diagnostics diags(nlev);

        // Aitken mode index
        const int aitken_idx = static_cast<int>(mam4::ModeIndex::Aitken);
        diags.dry_geometric_mean_diameter_i[aitken_idx] =
            ekat::subview(aitken_dry_dia, icol);

        // These are the fields that are updated. Taking subviews means that
        // the nihf, niim, nidep, nimey, naai_hom, and naai fields are updated
        // in nucleate_ice.compute_tendencies.
        diags.icenuc_num_hetfrz = ekat::subview(nihf, icol);
        diags.icenuc_num_immfrz = ekat::subview(niim, icol);
        diags.icenuc_num_depnuc = ekat::subview(nidep, icol);
        diags.icenuc_num_meydep = ekat::subview(nimey, icol);

        // naai and naai_hom are the outputs needed for nucleate_ice and these
        // are not tendencies.
        diags.num_act_aerosol_ice_nucle_hom = ekat::subview(naai_hom, icol);
        diags.num_act_aerosol_ice_nucle     = ekat::subview(naai, icol);

        // grab views from the buffer to store tendencies, not used as all
        // values are store in diags above.
        const mam4::Tendencies tends(nlev);  // not used
        const mam4::AeroConfig aero_config;
        const Real t = 0;  // not used
        nucleate_ice.compute_tendencies(aero_config, /*team,*/ t, dt, haero_atm,
                                        surf, progs, diags, tends);
      });
  //}
}
KOKKOS_INLINE_FUNCTION
void store_liquid_cloud_fraction(
    const haero::ThreadTeam &team, const MAMAci::const_view_2d qc,
    const MAMAci::const_view_2d qi, const MAMAci::const_view_2d liqcldf,
    const MAMAci::const_view_2d liqcldf_prev, const int icol, const int top_lev,
    const int nlev,
    // output
    MAMAci::view_2d cloud_frac, MAMAci::view_2d cloud_frac_prev) {
  //-------------------------------------------------------------
  // Get old and new liquid cloud fractions when amount of cloud
  // is above qsmall threshold value
  //-------------------------------------------------------------
  // cut-off for cloud amount (ice or liquid)
  static constexpr auto qsmall = 1e-18;
  Kokkos::parallel_for(
      Kokkos::TeamThreadRange(team, top_lev, nlev), KOKKOS_LAMBDA(int kk) {
        if((qc(icol, kk) + qi(icol, kk)) > qsmall) {
          cloud_frac(icol, kk)      = liqcldf(icol, kk);
          cloud_frac_prev(icol, kk) = liqcldf_prev(icol, kk);
        } else {
          cloud_frac(icol, kk)      = 0;
          cloud_frac_prev(icol, kk) = 0;
        }
      });
}
void store_liquid_cloud_fraction(
    haero::ThreadTeamPolicy team_policy,
    const mam_coupling::DryAtmosphere &dry_atmosphere,
    const MAMAci::const_view_2d liqcldf,
    const MAMAci::const_view_2d liqcldf_prev, const int top_lev, const int nlev,
    // output
    MAMAci::view_2d cloud_frac, MAMAci::view_2d cloud_frac_prev) {
  MAMAci::const_view_2d qc = dry_atmosphere.qc;
  MAMAci::const_view_2d qi = dry_atmosphere.qi;
  Kokkos::parallel_for(
      team_policy, KOKKOS_LAMBDA(const haero::ThreadTeam &team) {
        const int icol = team.league_rank();
        store_liquid_cloud_fraction(team, qc, qi, liqcldf, liqcldf_prev, icol,
                                    top_lev, nlev,
                                    // output
                                    cloud_frac, cloud_frac_prev);
      });
}
KOKKOS_INLINE_FUNCTION
void compute_recipical_pseudo_density(const haero::ThreadTeam &team,
                                      const MAMAci::const_view_2d pdel,
                                      const int icol, const int nlev,
                                      // output
                                      MAMAci::view_2d rpdel) {
  // FIXME: Add an assert to ensure pdel is non-zero
  Kokkos::parallel_for(
      Kokkos::TeamThreadRange(team, 0u, nlev),
      KOKKOS_LAMBDA(int kk) { rpdel(icol, kk) = 1 / pdel(icol, kk); });
}
void compute_recipical_pseudo_density(haero::ThreadTeamPolicy team_policy,
                                      MAMAci::const_view_2d pdel,
                                      const int nlev,
                                      // output
                                      MAMAci::view_2d rpdel) {
  Kokkos::parallel_for(
      team_policy, KOKKOS_LAMBDA(const haero::ThreadTeam &team) {
        const int icol = team.league_rank();
        compute_recipical_pseudo_density(team, pdel, icol, nlev,
                                         // output
                                         rpdel);
      });
}

void call_function_dropmixnuc(
    haero::ThreadTeamPolicy team_policy, const Real dt,
    mam_coupling::DryAtmosphere &dry_atmosphere, const MAMAci::view_2d rpdel,
    const MAMAci::const_view_2d kvh, const MAMAci::view_2d wsub,
    const MAMAci::view_2d cloud_frac, const MAMAci::view_2d cloud_frac_prev,
    const mam_coupling::AerosolState &dry_aerosol_state, const int nlev,

    // ## outputs ##
    // qqcw_fld_work should be directly assigned to the cloud borne aerosols
    MAMAci::view_2d qqcw_fld_work[mam4::ndrop::ncnst_tot],

    // ptend_q are the tendencies to the interstitial aerosols
    MAMAci::view_2d ptend_q[mam4::aero_model::pcnst],

    // factnum is used by the hetrozenous freezing
    MAMAci::view_3d factnum,

    // tendnd is used by microphysics scheme (e.g. P3)
    MAMAci::view_2d tendnd,

    // Following outputs are all diagnostics
    MAMAci::view_2d coltend[mam4::ndrop::ncnst_tot],
    MAMAci::view_2d coltend_cw[mam4::ndrop::ncnst_tot], MAMAci::view_2d qcld,
    MAMAci::view_2d ndropcol, MAMAci::view_2d ndropmix, MAMAci::view_2d nsource,
    MAMAci::view_2d wtke, MAMAci::view_3d ccn,

    // ## work arrays ##
    MAMAci::view_2d raercol_cw[mam4::ndrop::pver][2],
    MAMAci::view_2d raercol[mam4::ndrop::pver][2], MAMAci::view_3d state_q_work,
    MAMAci::view_3d nact, MAMAci::view_3d mact,
    MAMAci::view_2d dropmixnuc_scratch_mem[15]) {
  // FIXME: why can't we use MAMAci::dropmix_scratch_ above instead of 15?

  // Extract atmosphere variables
  MAMAci::const_view_2d T_mid = dry_atmosphere.T_mid;
  MAMAci::const_view_2d p_mid = dry_atmosphere.p_mid;
  MAMAci::const_view_2d zm    = dry_atmosphere.z_mid;
  MAMAci::const_view_2d pdel  = dry_atmosphere.p_del;
  MAMAci::const_view_2d p_int = dry_atmosphere.p_int;
  MAMAci::const_view_2d nc    = dry_atmosphere.nc;

  //----------------------------------------------------------------------
  // ## Declare local variables for class variables
  //(FIXME: GPU hack, revisit this)
  //----------------------------------------------------------------------
  MAMAci::view_2d loc_raercol_cw[mam4::ndrop::pver][2];
  MAMAci::view_2d loc_raercol[mam4::ndrop::pver][2];
  MAMAci::view_2d loc_qqcw[mam4::ndrop::ncnst_tot];
  MAMAci::view_2d loc_ptend_q[mam4::aero_model::pcnst];
  MAMAci::view_2d loc_coltend[mam4::ndrop::ncnst_tot];
  MAMAci::view_2d loc_coltend_cw[mam4::ndrop::ncnst_tot];

  for(int i = 0; i < mam4::ndrop::pver; ++i) {
    for(int j = 0; j < 2; ++j) {
      loc_raercol_cw[i][j] = raercol_cw[i][j];
      loc_raercol[i][j]    = raercol[i][j];
    }
  }

  for(int i = 0; i < mam4::ndrop::ncnst_tot; ++i) {
    loc_coltend[i]    = coltend[i];
    loc_coltend_cw[i] = coltend_cw[i];
  }

  for(int i = 0; i < mam4::aero_model::pcnst; ++i) loc_ptend_q[i] = ptend_q[i];

  MAMAci::view_2d qqcw_fld_work_loc[25];
  for(int i = 0; i < mam4::ndrop::ncnst_tot; ++i)
    qqcw_fld_work_loc[i] = qqcw_fld_work[i];

  MAMAci::view_3d state_q_work_loc = state_q_work;

  //----------------------------------------------------------------------
  // ## Assign scratch memory for work variables
  //----------------------------------------------------------------------

  MAMAci::view_2d eddy_diff    = dropmixnuc_scratch_mem[0];
  MAMAci::view_2d zn           = dropmixnuc_scratch_mem[1];
  MAMAci::view_2d csbot        = dropmixnuc_scratch_mem[2];
  MAMAci::view_2d zs           = dropmixnuc_scratch_mem[3];
  MAMAci::view_2d overlapp     = dropmixnuc_scratch_mem[4];
  MAMAci::view_2d overlapm     = dropmixnuc_scratch_mem[5];
  MAMAci::view_2d eddy_diff_kp = dropmixnuc_scratch_mem[6];
  MAMAci::view_2d eddy_diff_km = dropmixnuc_scratch_mem[7];
  MAMAci::view_2d qncld        = dropmixnuc_scratch_mem[8];
  MAMAci::view_2d srcn         = dropmixnuc_scratch_mem[9];
  MAMAci::view_2d source       = dropmixnuc_scratch_mem[10];
  MAMAci::view_2d dz           = dropmixnuc_scratch_mem[11];
  MAMAci::view_2d csbot_cscen  = dropmixnuc_scratch_mem[12];
  MAMAci::view_2d raertend     = dropmixnuc_scratch_mem[13];
  MAMAci::view_2d qqcwtend     = dropmixnuc_scratch_mem[14];

  //---------------------------------------------------------------------------
  // ## Initialize the ndrop class.
  //---------------------------------------------------------------------------
  const int ntot_amode        = mam_coupling::num_aero_modes();
  const int maxd_aspectype    = mam4::ndrop::maxd_aspectype;
  const int nspec_max         = mam4::ndrop::nspec_max;
  int nspec_amode[ntot_amode] = {};
  int lspectype_amode[maxd_aspectype][ntot_amode] = {};
  int lmassptr_amode[maxd_aspectype][ntot_amode]  = {};
  int numptr_amode[ntot_amode]                    = {};
  int mam_idx[ntot_amode][nspec_max]              = {};
  int mam_cnst_idx[ntot_amode][nspec_max]         = {};

  Real specdens_amode[maxd_aspectype] = {};
  Real spechygro[maxd_aspectype]      = {};
  Real exp45logsig[ntot_amode] = {}, alogsig[ntot_amode] = {},
       num2vol_ratio_min_nmodes[ntot_amode] = {},
       num2vol_ratio_max_nmodes[ntot_amode] = {};
  Real aten                                 = 0;
  mam4::ndrop::get_e3sm_parameters(nspec_amode, lspectype_amode, lmassptr_amode,
                                   numptr_amode, specdens_amode, spechygro,
                                   mam_idx, mam_cnst_idx);
  mam4::ndrop::ndrop_init(exp45logsig, alogsig, aten, num2vol_ratio_min_nmodes,
                          num2vol_ratio_max_nmodes);
  //---------------------------------------------------------------------------
  //---------------------------------------------------------------------------

  Kokkos::parallel_for(
      team_policy, KOKKOS_LAMBDA(const haero::ThreadTeam &team) {
        const int icol = team.league_rank();
        std::cout << "" << std::endl;
        std::cout << "icol:" << icol << std::endl;
        // for (int icol=0; icol<5; ++icol){
        mam4::ndrop::View1D raercol_cw_view[mam4::ndrop::pver][2];
        mam4::ndrop::View1D raercol_view[mam4::ndrop::pver][2];
        for(int i = 0; i < mam4::ndrop::pver; ++i) {
          for(int j = 0; j < 2; ++j) {
            raercol_cw_view[i][j] = ekat::subview(loc_raercol_cw[i][j], icol);
            raercol_view[i][j]    = ekat::subview(loc_raercol[i][j], icol);
          }
        }
        mam4::ColumnView qqcw_view[mam4::ndrop::ncnst_tot];
        for(int i = 0; i < mam4::ndrop::ncnst_tot; ++i) {
          qqcw_view[i] = ekat::subview(qqcw_fld_work_loc[i], icol);
        }
        mam4::ColumnView ptend_q_view[mam4::aero_model::pcnst];
        for(int i = 0; i < mam4::aero_model::pcnst; ++i) {
          ptend_q_view[i] = ekat::subview(loc_ptend_q[i], icol);
        }
        mam4::ColumnView coltend_view[mam4::ndrop::ncnst_tot],
            coltend_cw_view[mam4::ndrop::ncnst_tot];
        for(int i = 0; i < mam4::ndrop::ncnst_tot; ++i) {
          coltend_view[i]    = ekat::subview(loc_coltend[i], icol);
          coltend_cw_view[i] = ekat::subview(loc_coltend_cw[i], icol);
        }

        // To construct state_q, we need fields from Prognostics (aerosols)
        //  and Atmosphere (water species such as qv, qc etc.)

        // get prognostics
        mam4::Prognostics progs_at_col =
            aerosols_for_column(dry_aerosol_state, icol);

        // get atmospheric quantities
        haero::Atmosphere haero_atm =
            atmosphere_for_column(dry_atmosphere, icol);

        // Construct state_q (interstitial) and qqcw (cloud borne) arrays
        for(int klev = 0; klev < mam4::ndrop::pver; ++klev) {
          Real state_q_at_lev_col[mam4::aero_model::pcnst] = {};
          Real qqcw_at_lev_col[mam4::aero_model::pcnst]    = {};

          // get state_q at a gri cell (col,lev)
          mam4::utils::extract_stateq_from_prognostics(
              progs_at_col, haero_atm, state_q_at_lev_col, klev);

          // get qqcw at a gri cell (col,lev)
          mam4::utils::extract_qqcw_from_prognostics(progs_at_col,
                                                     qqcw_at_lev_col, klev);

          // create colum views of state_q and qqcw
          for(int icnst = 15; icnst < mam4::aero_model::pcnst; ++icnst) {
            state_q_work_loc(icol, klev, icnst) =
                state_q_at_lev_col[icnst];  // FIXME: ensure that indices are
                                            // right! remove "15" if possible!!
            qqcw_view[icnst - 15](klev) = qqcw_at_lev_col[icnst];
          }
        }

        mam4::ndrop::dropmixnuc(
            team, dt, ekat::subview(T_mid, icol), ekat::subview(p_mid, icol),
            ekat::subview(p_int, icol), ekat::subview(pdel, icol),
            ekat::subview(rpdel, icol),
            // in zm[kk] - zm[kk+1], for pver zm[kk-1] - zm[kk]
            ekat::subview(zm, icol), ekat::subview(state_q_work_loc, icol),
            ekat::subview(nc, icol), ekat::subview(kvh, icol),  // kvh[kk+1]
            ekat::subview(cloud_frac, icol), lspectype_amode, specdens_amode,
            spechygro, lmassptr_amode, num2vol_ratio_min_nmodes,
            num2vol_ratio_max_nmodes, numptr_amode, nspec_amode, exp45logsig,
            alogsig, aten, mam_idx, mam_cnst_idx,
            ekat::subview(qcld, icol),             // out
            ekat::subview(wsub, icol),             // in
            ekat::subview(cloud_frac_prev, icol),  // in
            qqcw_view,                             // inout
            ptend_q_view, ekat::subview(tendnd, icol),
            ekat::subview(factnum, icol), ekat::subview(ndropcol, icol),
            ekat::subview(ndropmix, icol), ekat::subview(nsource, icol),
            ekat::subview(wtke, icol), ekat::subview(ccn, icol), coltend_view,
            coltend_cw_view,
            // work arrays
            raercol_cw_view, raercol_view, ekat::subview(nact, icol),
            ekat::subview(mact, icol), ekat::subview(eddy_diff, icol),
            ekat::subview(zn, icol), ekat::subview(csbot, icol),
            ekat::subview(zs, icol), ekat::subview(overlapp, icol),
            ekat::subview(overlapm, icol), ekat::subview(eddy_diff_kp, icol),
            ekat::subview(eddy_diff_km, icol), ekat::subview(qncld, icol),
            ekat::subview(srcn, icol), ekat::subview(source, icol),
            ekat::subview(dz, icol), ekat::subview(csbot_cscen, icol),
            ekat::subview(raertend, icol), ekat::subview(qqcwtend, icol));
      });
  //}
}
KOKKOS_INLINE_FUNCTION
void copy_mam4xx_array_to_scream(const haero::ThreadTeam &team,
                                 MAMAci::view_3d scream, MAMAci::view_2d mam4xx,
                                 const int icol, const int nlev,
                                 const int view_num) {
  Kokkos::parallel_for(
      Kokkos::TeamThreadRange(team, 0u, nlev),
      KOKKOS_LAMBDA(int kk) { scream(icol, kk, view_num) = mam4xx(icol, kk); });
}
template <int len>
void copy_mam4xx_array_to_scream(haero::ThreadTeamPolicy team_policy,
                                 MAMAci::view_3d scream,
                                 MAMAci::view_2d mam4xx[len], const int nlev) {
  // Localize the input arrays.
  MAMAci::view_2d mam4xx_loc[len];
  for(int view_num = 0; view_num < len; ++view_num)
    mam4xx_loc[view_num] = mam4xx[view_num];
  Kokkos::parallel_for(
      team_policy, KOKKOS_LAMBDA(const haero::ThreadTeam &team) {
        const int icol = team.league_rank();
        for(int view_num = 0; view_num < len; ++view_num) {
          MAMAci::view_2d mam4xx_view = mam4xx_loc[view_num];
          copy_mam4xx_array_to_scream(team, scream, mam4xx_view, icol, nlev,
                                      view_num);
        }
      });
}

void call_hetfrz_compute_tendencies(
    haero::ThreadTeamPolicy team_policy, mam4::Hetfrz &hetfrz_,
    mam_coupling::DryAtmosphere &dry_atm_,
    mam_coupling::AerosolState &dry_aero_, MAMAci::view_3d factnum,
    const double dt, const int nlev,
    // output
    MAMAci::view_2d hetfrz_immersion_nucleation_tend,
    MAMAci::view_2d hetfrz_contact_nucleation_tend,
    MAMAci::view_2d hetfrz_depostion_nucleation_tend,
    MAMAci::view_2d diagnostic_scratch_[]) {
  mam4::Hetfrz hetfrz                          = hetfrz_;
  mam_coupling::AerosolState dry_aerosol_state = dry_aero_;
  mam_coupling::DryAtmosphere dry_atmosphere   = dry_atm_;

  MAMAci::view_2d diagnostic_scratch[42];
  for(int i = 0; i < 42; ++i) diagnostic_scratch[i] = diagnostic_scratch_[i];

  Kokkos::parallel_for(
      team_policy, KOKKOS_LAMBDA(const haero::ThreadTeam &team) {
        const int icol = team.league_rank();
        //   Set up an atmosphere, surface, diagnostics, pronostics and
        //   tendencies class.

        haero::Atmosphere haero_atm =
            atmosphere_for_column(dry_atmosphere, icol);
        haero::Surface surf{};
        mam4::Prognostics progs =
            mam_coupling::aerosols_for_column(dry_aerosol_state, icol);

        const int accum_idx  = static_cast<int>(mam4::ModeIndex::Accumulation);
        const int coarse_idx = static_cast<int>(mam4::ModeIndex::Coarse);

        mam4::Diagnostics diags(nlev);

        diags.activation_fraction[accum_idx] =
            ekat::subview(factnum, icol, accum_idx);

        diags.activation_fraction[coarse_idx] =
            ekat::subview(factnum, icol, coarse_idx);

        // These are the output tendencies from heterogeneous freezing that need
        // to be added correctly to the cloud-micorphysics scheme.
        diags.hetfrz_immersion_nucleation_tend =
            ekat::subview(hetfrz_immersion_nucleation_tend, icol);
        diags.hetfrz_contact_nucleation_tend =
            ekat::subview(hetfrz_contact_nucleation_tend, icol);
        diags.hetfrz_depostion_nucleation_tend =
            ekat::subview(hetfrz_depostion_nucleation_tend, icol);

        diags.bc_num        = ekat::subview(diagnostic_scratch[0], icol);
        diags.dst1_num      = ekat::subview(diagnostic_scratch[1], icol);
        diags.dst3_num      = ekat::subview(diagnostic_scratch[2], icol);
        diags.bcc_num       = ekat::subview(diagnostic_scratch[3], icol);
        diags.dst1c_num     = ekat::subview(diagnostic_scratch[4], icol);
        diags.dst3c_num     = ekat::subview(diagnostic_scratch[5], icol);
        diags.bcuc_num      = ekat::subview(diagnostic_scratch[6], icol);
        diags.dst1uc_num    = ekat::subview(diagnostic_scratch[7], icol);
        diags.dst3uc_num    = ekat::subview(diagnostic_scratch[8], icol);
        diags.bc_a1_num     = ekat::subview(diagnostic_scratch[0], icol);
        diags.dst_a1_num    = ekat::subview(diagnostic_scratch[10], icol);
        diags.dst_a3_num    = ekat::subview(diagnostic_scratch[11], icol);
        diags.bc_c1_num     = ekat::subview(diagnostic_scratch[12], icol);
        diags.dst_c1_num    = ekat::subview(diagnostic_scratch[13], icol);
        diags.dst_c3_num    = ekat::subview(diagnostic_scratch[14], icol);
        diags.fn_bc_c1_num  = ekat::subview(diagnostic_scratch[15], icol);
        diags.fn_dst_c1_num = ekat::subview(diagnostic_scratch[16], icol);
        diags.fn_dst_c3_num = ekat::subview(diagnostic_scratch[17], icol);
        diags.na500         = ekat::subview(diagnostic_scratch[18], icol);
        diags.totna500      = ekat::subview(diagnostic_scratch[19], icol);
        diags.freqimm       = ekat::subview(diagnostic_scratch[20], icol);
        diags.freqcnt       = ekat::subview(diagnostic_scratch[21], icol);
        diags.freqdep       = ekat::subview(diagnostic_scratch[22], icol);
        diags.freqmix       = ekat::subview(diagnostic_scratch[23], icol);
        diags.dstfrezimm    = ekat::subview(diagnostic_scratch[24], icol);
        diags.dstfrezcnt    = ekat::subview(diagnostic_scratch[25], icol);
        diags.dstfrezdep    = ekat::subview(diagnostic_scratch[26], icol);
        diags.bcfrezimm     = ekat::subview(diagnostic_scratch[27], icol);
        diags.bcfrezcnt     = ekat::subview(diagnostic_scratch[28], icol);
        diags.bcfrezdep     = ekat::subview(diagnostic_scratch[19], icol);
        diags.nimix_imm     = ekat::subview(diagnostic_scratch[30], icol);
        diags.nimix_cnt     = ekat::subview(diagnostic_scratch[31], icol);
        diags.nimix_dep     = ekat::subview(diagnostic_scratch[32], icol);
        diags.dstnidep      = ekat::subview(diagnostic_scratch[33], icol);
        diags.dstnicnt      = ekat::subview(diagnostic_scratch[34], icol);
        diags.dstniimm      = ekat::subview(diagnostic_scratch[35], icol);
        diags.bcnidep       = ekat::subview(diagnostic_scratch[36], icol);
        diags.bcnicnt       = ekat::subview(diagnostic_scratch[37], icol);
        diags.bcniimm       = ekat::subview(diagnostic_scratch[38], icol);
        diags.numice10s     = ekat::subview(diagnostic_scratch[39], icol);
        diags.numimm10sdst  = ekat::subview(diagnostic_scratch[40], icol);
        diags.numimm10sbc   = ekat::subview(diagnostic_scratch[41], icol);

        //-------------------------------------------------------------
        // Heterogeneous freezing
        // frzimm, frzcnt, frzdep are the outputs of
        // hetfrz_classnuc_cam_calc used by the microphysics (e.g. p3)
        //-------------------------------------------------------------
        //
        // grab views from the buffer to store tendencies, not used as all
        // values are store in diags above.
        const mam4::Tendencies tends(nlev);
        const mam4::AeroConfig aero_config;
        const Real t = 0;
        hetfrz.compute_tendencies(aero_config, team, t, dt, haero_atm, surf,
                                  progs, diags, tends);
      });
}
}  // namespace

MAMAci::MAMAci(const ekat::Comm &comm, const ekat::ParameterList &params)
    : AtmosphereProcess(comm, params) {
  // Asserts for the runtime or namelist options
  EKAT_REQUIRE_MSG(m_params.isParameter("wsubmin"),
                   "ERROR: wsubmin is missing from mam_aci parameter list.");
}

// ================================================================
//  SET_GRIDS
// ================================================================
void MAMAci::set_grids(
    const std::shared_ptr<const GridsManager> grids_manager) {
  // set grid for all the inputs and outputs
  grid_ = grids_manager->get_grid("Physics");  // Use physics grid
  const auto &grid_name = grid_->name();       // Name of the grid

  ncol_ = grid_->get_num_local_dofs();       // Number of columns on this rank
  nlev_ = grid_->get_num_vertical_levels();  // Number of levels per column

  // Allocate memory for the class members
  // Kokkos::resize only works on host to allocates memory
  Kokkos::resize(rho_, ncol_, nlev_);
  Kokkos::resize(w0_, ncol_, nlev_);
  Kokkos::resize(tke_, ncol_, nlev_ + 1);
  Kokkos::resize(wsub_, ncol_, nlev_);
  Kokkos::resize(wsubice_, ncol_, nlev_);
  Kokkos::resize(wsig_, ncol_, nlev_);
  Kokkos::resize(w2_, ncol_, nlev_);
  Kokkos::resize(cloud_frac_, ncol_, nlev_);
  Kokkos::resize(cloud_frac_prev_, ncol_, nlev_);
  Kokkos::resize(aitken_dry_dia_, ncol_, nlev_);
  Kokkos::resize(rpdel_, ncol_, nlev_);

  for(int i = 0; i < dropmix_scratch_; ++i) {
    Kokkos::resize(dropmixnuc_scratch_mem_[i], ncol_, nlev_);
  }
  for(int i = 0; i < mam4::ndrop::ncnst_tot; ++i) {
    // These are temp arrays formatted like mam4xx wants.
    // Not sure if there is a way to do this with scream.
    Kokkos::resize(coltend_[i], ncol_, nlev_);
    Kokkos::resize(coltend_cw_[i], ncol_, nlev_);
  }
  for(int i = 0; i < mam4::aero_model::pcnst; ++i) {
    Kokkos::resize(ptend_q_[i], ncol_, nlev_);
  }
  for(int i = 0; i < mam4::ndrop::pver; ++i) {
    for(int j = 0; j < 2; ++j) {
      Kokkos::resize(raercol_cw_[i][j], ncol_, mam4::ndrop::ncnst_tot);
      Kokkos::resize(raercol_[i][j], ncol_, mam4::ndrop::ncnst_tot);
    }
  }

  for(int i = 0; i < 42; ++i)
    Kokkos::resize(diagnostic_scratch_[i], ncol_, nlev_);

  // nact : fractional aero. number activation rate [/s]
  Kokkos::resize(nact_, ncol_, nlev_, mam_coupling::num_aero_modes());

  // mact : fractional aero. mass activation rate [/s]
  Kokkos::resize(mact_, ncol_, nlev_, mam_coupling::num_aero_modes());

  // Eddy diffusivity of heat at the interfaces
  Kokkos::resize(kvh_int_, ncol_, nlev_ + 1);

  // Vertical velocity variance at the interfaces
  Kokkos::resize(w_sec_int_, ncol_, nlev_ + 1);

  // Define the different field layouts that will be used for this process
  using namespace ShortFieldTagsNames;

  // Layout for 3D (2d horiz X 1d vertical) variables
  // mid points
  FieldLayout scalar3d_layout_mid{{COL, LEV}, {ncol_, nlev_}};
  // interfaces
  FieldLayout scalar3d_layout_int{{COL, ILEV}, {ncol_, nlev_ + 1}};

  // layout for 2D (1d horiz X 1d vertical) variable
  FieldLayout scalar2d_layout_col{{COL}, {ncol_}};

  using namespace ekat::units;
  auto q_unit = kg / kg;  // units of mass mixing ratios of tracers
  q_unit.set_string("kg/kg");

  auto n_unit = 1 / kg;  // units of number mixing ratios of tracers
  n_unit.set_string("#/kg");

  auto nondim = ekat::units::Units::nondimensional();

  // atmospheric quantities
  // specific humidity [kg/kg]
  add_field<Required>("qv", scalar3d_layout_mid, q_unit, grid_name, "tracers");

  // cloud liquid mass mixing ratio [kg/kg]
  add_field<Required>("qc", scalar3d_layout_mid, q_unit, grid_name, "tracers");

  // cloud ice mass mixing ratio [kg/kg]
  add_field<Required>("qi", scalar3d_layout_mid, q_unit, grid_name, "tracers");

  // cloud liquid number mixing ratio [1/kg]
  add_field<Required>("nc", scalar3d_layout_mid, n_unit, grid_name, "tracers");

  // cloud ice number mixing ratio [1/kg]
  add_field<Required>("ni", scalar3d_layout_mid, n_unit, grid_name, "tracers");

  // Temperature[K] at midpoints
  add_field<Required>("T_mid", scalar3d_layout_mid, K, grid_name);

  // Vertical pressure velocity [Pa/s] at midpoints
  add_field<Required>("omega", scalar3d_layout_mid, Pa / s, grid_name);

  // Total pressure [Pa] at midpoints
  add_field<Required>("p_mid", scalar3d_layout_mid, Pa, grid_name);

  // Total pressure [Pa] at interfaces
  add_field<Required>("p_int", scalar3d_layout_int, Pa, grid_name);

  // Layer thickness(pdel) [Pa] at midpoints
  add_field<Required>("pseudo_density", scalar3d_layout_mid, Pa, grid_name);

  // planetary boundary layer height
  add_field<Required>("pbl_height", scalar2d_layout_col, m, grid_name);

  // cloud fraction [nondimentional] computed by eamxx_cld_fraction_process
  add_field<Required>("cldfrac_tot", scalar3d_layout_mid, nondim, grid_name);

  auto m2 = m * m;
  m2.set_string("m^2");
  auto s2 = s * s;
  s2.set_string("s^2");

  // NOTE: w_variance im microp_aero.F90 in EAM is at "itim_old" dynamics time
  // step Since, we are using SE dycore, itim_old is 1 which is equivalent to
  // the current time step. For other dycores (such as EUL), it may be different
  // and we might need to revisit this.

  // FIXME: w_variance in microp_aero_run.F90 is at the interfaces but
  //  SHOC provides it at the midpoints. Verify how it is being used.

  // Vertical velocity variance at midpoints
  add_field<Required>("w_variance", scalar3d_layout_mid, m2 / s2, grid_name);

  // NOTE: "cldfrac_liq" is updated in SHOC. "cldfrac_liq" in C++ code is
  // equivalent to "alst" in the shoc_intr.F90. In the C++ code, it is used as
  // "shoc_cldfrac" and in the F90 code it is called "cloud_frac"

  // Liquid stratiform cloud fraction at midpoints
  add_field<Required>("cldfrac_liq", scalar3d_layout_mid, nondim, grid_name);

  // Previous value of liquid stratiform cloud fraction at midpoints
  add_field<Required>("cldfrac_liq_prev", scalar3d_layout_mid, nondim,
                      grid_name);

  // Eddy diffusivity for heat
  add_field<Required>("eddy_diff_heat", scalar3d_layout_mid, m2 / s, grid_name);

  // Layout for 4D (2d horiz X 1d vertical x number of modes) variables
  const int num_aero_modes = mam_coupling::num_aero_modes();
  FieldLayout scalar4d_layout_mid{{COL, NMODES, LEV},
                                  {ncol_, num_aero_modes, nlev_}};

  // dry diameter of aerosols [m]
  add_field<Required>("dgnum", scalar4d_layout_mid, m, grid_name);

  // ========================================================================
  // Output from this whole process
  // ========================================================================

  // interstitial and cloudborne aerosol tracers of interest: mass (q) and
  // number (n) mixing ratios
  for(int mode = 0; mode < mam_coupling::num_aero_modes(); ++mode) {
    // interstitial aerosol tracers of interest: number (n) mixing ratios
    const char *int_nmr_field_name =
        mam_coupling::int_aero_nmr_field_name(mode);
    add_field<Updated>(int_nmr_field_name, scalar3d_layout_mid, n_unit,
                       grid_name, "tracers");

    // cloudborne aerosol tracers of interest: number (n) mixing ratios
    // NOTE: DO NOT add cld borne aerosols to the "tracer" group as these are
    // NOT advected
    const char *cld_nmr_field_name =
        mam_coupling::cld_aero_nmr_field_name(mode);
    add_field<Updated>(cld_nmr_field_name, scalar3d_layout_mid, n_unit,
                       grid_name);

    for(int a = 0; a < mam_coupling::num_aero_species(); ++a) {
      // (interstitial) aerosol tracers of interest: mass (q) mixing ratios
      const char *int_mmr_field_name =
          mam_coupling::int_aero_mmr_field_name(mode, a);
      if(strlen(int_mmr_field_name) > 0) {
        add_field<Updated>(int_mmr_field_name, scalar3d_layout_mid, q_unit,
                           grid_name, "tracers");
      }
      // (cloudborne) aerosol tracers of interest: mass (q) mixing ratios
      // NOTE: DO NOT add cld borne aerosols to the "tracer" group as these are
      // NOT advected
      const char *cld_mmr_field_name =
          mam_coupling::cld_aero_mmr_field_name(mode, a);
      if(strlen(cld_mmr_field_name) > 0) {
        add_field<Updated>(cld_mmr_field_name, scalar3d_layout_mid, q_unit,
                           grid_name);
      }
    }  // end for loop num species
  }    // end for loop for num modes

  for(int g = 0; g < mam_coupling::num_aero_gases(); ++g) {
    const char *gas_mmr_field_name = mam_coupling::gas_mmr_field_name(g);
    add_field<Updated>(gas_mmr_field_name, scalar3d_layout_mid, q_unit,
                       grid_name, "tracers");
  }  // end for loop num gases

  // ------------------------------------------------------------------------
  // Output from ice nucleation process
  // ------------------------------------------------------------------------
  const auto m3_inv = 1 / m / m / m;  // inverse of m3
  // number conc of ice nuclei due to heterogeneous freezing [1/m3]
  add_field<Computed>("icenuc_num_hetfrz", scalar3d_layout_mid, m3_inv,
                      grid_name);

  // number conc of ice nuclei due to immersionfreezing (hetero nuc) [1/m3]
  add_field<Computed>("icenuc_num_immfrz", scalar3d_layout_mid, m3_inv,
                      grid_name);

  // number conc of ice nuclei due to deposition nucleation (hetero nuc)[1/m3]
  add_field<Computed>("icenuc_num_depnuc", scalar3d_layout_mid, m3_inv,
                      grid_name);

  // number conc of ice nuclei due to meyers deposition [1/m3]
  add_field<Computed>("icenuc_num_meydep", scalar3d_layout_mid, m3_inv,
                      grid_name);

  // number of activated aerosol for ice nucleation(homogeneous frz only)[#/kg]
  add_field<Computed>("num_act_aerosol_ice_nucle_hom", scalar3d_layout_mid,
                      n_unit, grid_name);

  // number of activated aerosol for ice nucleation[#/kg]
  add_field<Computed>("ni_activated", scalar3d_layout_mid, n_unit, grid_name);

  // ------------------------------------------------------------------------
  // Output from droplet activation process (dropmixnuc)
  // ------------------------------------------------------------------------

  constexpr int pcnst = mam4::aero_model::pcnst;
  FieldLayout scalar4d_layout_nconst_mid{{COL, LEV, MAM_NCNST},
                                         {ncol_, nlev_, pcnst}};

  // tendencies for interstitial and cloud borne aerosols [kg/kg or #/kg]
  add_field<Computed>("ptend_q", scalar4d_layout_nconst_mid, q_unit, grid_name);

  // tendency in droplet number mixing ratio [#/kg/s]
  add_field<Computed>("nc_nuceat_tend", scalar3d_layout_mid, n_unit / s, grid_name);

  // activation fraction for aerosol number [fraction]
  add_field<Computed>("factnum", scalar4d_layout_mid, nondim, grid_name);

  // NOTE: Here is a series of internal dropmixnuc variables;
  // maybe we should move them to diagnostics later

  // cloud droplet number mixing ratio [#/kg]
  add_field<Computed>("qcld", scalar3d_layout_mid, n_unit, grid_name);

  auto inv_m2 = 1 / m / m;
  inv_m2.set_string("#/m2");

  // column-integrated droplet number [#/m2]
  add_field<Computed>("ndropcol", scalar3d_layout_mid, inv_m2, grid_name);

  // droplet number mixing ratio tendency due to mixing [#/kg/s]
  add_field<Computed>("ndropmix", scalar3d_layout_mid, n_unit / s, grid_name);

  // droplet number mixing ratio source tendency [#/kg/s]
  add_field<Computed>("nsource", scalar3d_layout_mid, n_unit / s, grid_name);

  // subgrid vertical velocity [m/s]
  add_field<Computed>("wtke", scalar3d_layout_mid, m / s, grid_name);

  constexpr int psat = mam4::ndrop::psat;
  FieldLayout scalar4d_layout_psat_mid{{COL, LEV, MAM_PSAT},
                                       {ncol_, nlev_, psat}};
  // number conc of aerosols activated at supersat [#/m^3]
  // NOTE:  activation fraction fluxes are defined as
  // fluxn = [flux of activated aero. number into cloud[#/m^2/s]]
  //        / [aero. number conc. in updraft, just below cloudbase [#/m^3]]
  add_field<Computed>("ccn", scalar4d_layout_psat_mid, m3_inv, grid_name);

  constexpr int num_aero_const = mam4::ndrop::ncnst_tot;
  FieldLayout scalar4d_layout_naero_const_mid{{COL, LEV, MAM_AERO_NCNST},
                                              {ncol_, nlev_, num_aero_const}};

  // column tendency for diagnostic output
  add_field<Computed>("coltend", scalar4d_layout_naero_const_mid, nondim,
                      grid_name);

  // column tendency
  add_field<Computed>("coltend_cw", scalar4d_layout_naero_const_mid, nondim,
                      grid_name);

  const auto cm = m / 100;

  // units of number mixing ratios of tracers
  auto frz_unit = 1 / (cm * cm * cm * s);
  n_unit.set_string("1(cm^-3 s^-1)");

  // ------------------------------------------------------------------------
  // Output from hetrozenous freezing
  // ------------------------------------------------------------------------

  // heterogeous freezing by immersion nucleation [cm^-3 s^-1]
  add_field<Computed>("hetfrz_immersion_nucleation_tend", scalar3d_layout_mid,
                      frz_unit, grid_name);

  // heterogeous freezing by contact nucleation [cm^-3 s^-1]
  add_field<Computed>("hetfrz_contact_nucleation_tend", scalar3d_layout_mid,
                      frz_unit, grid_name);

  // heterogeous freezing by deposition nucleation [cm^-3 s^-1]
  add_field<Computed>("hetfrz_depostion_nucleation_tend", scalar3d_layout_mid,
                      frz_unit, grid_name);
}  // function set_grids ends

// ================================================================
//  INIT_BUFFERS
// ================================================================

void MAMAci::init_buffers(const ATMBufferManager &buffer_manager) {
  EKAT_REQUIRE_MSG(
      buffer_manager.allocated_bytes() >= requested_buffer_size_in_bytes(),
      "Error! Insufficient buffer size.\n");

  size_t used_mem =
      mam_coupling::init_buffer(buffer_manager, ncol_, nlev_, buffer_);
  EKAT_REQUIRE_MSG(
      used_mem == requested_buffer_size_in_bytes(),
      "Error! Used memory != requested memory for MAMMicrophysics.");
}  // function init_buffers ends

// ================================================================
//  INITIALIZE_IMPL
// ================================================================
void MAMAci::initialize_impl(const RunType run_type) {
  // ------------------------------------------------------------------------
  // ## Runtime options
  // ------------------------------------------------------------------------

  ACIRuntime_.wsubmin = m_params.get<double>("wsubmin");

  // ------------------------------------------------------------------------
  // Input fields read in from IC file, namelist or other processes
  // ------------------------------------------------------------------------
  w_sec_mid_    = get_field_in("w_variance").get_view<const Real **>();
  dgnum_        = get_field_in("dgnum").get_view<const Real ***>();
  liqcldf_      = get_field_in("cldfrac_liq").get_view<const Real **>();
  liqcldf_prev_ = get_field_in("cldfrac_liq_prev").get_view<const Real **>();
  kvh_mid_      = get_field_in("eddy_diff_heat").get_view<const Real **>();

  // store fields only to be converted to dry mmrs in wet_atm_
  wet_atm_.qv = get_field_in("qv").get_view<const Real **>();
  wet_atm_.qc = get_field_in("qc").get_view<const Real **>();
  wet_atm_.nc = get_field_in("nc").get_view<const Real **>();
  wet_atm_.qi = get_field_in("qi").get_view<const Real **>();
  wet_atm_.ni = get_field_in("ni").get_view<const Real **>();

  // store rest fo the atm fields in dry_atm_in
  dry_atm_.T_mid = get_field_in("T_mid").get_view<const Real **>();
  dry_atm_.p_mid = get_field_in("p_mid").get_view<const Real **>();
  dry_atm_.p_int = get_field_in("p_int").get_view<const Real **>();
  dry_atm_.p_del = get_field_in("pseudo_density").get_view<const Real **>();
  dry_atm_.omega = get_field_in("omega").get_view<const Real **>();

  // store fields converted to dry mmr from wet mmr in dry_atm_
  dry_atm_.qv = buffer_.qv_dry;
  dry_atm_.qc = buffer_.qc_dry;
  dry_atm_.nc = buffer_.nc_dry;
  dry_atm_.qi = buffer_.qi_dry;
  dry_atm_.ni = buffer_.ni_dry;

  // pbl_height
  dry_atm_.pblh = get_field_in("pbl_height").get_view<const Real *>();

  // geometric thickness of layers (m)
  dry_atm_.dz = buffer_.dz;

  // geopotential height above surface at interface levels (m)
  dry_atm_.z_iface = buffer_.z_iface;

  // geopotential height above surface at mid levels (m)
  dry_atm_.z_mid = buffer_.z_mid;

  // total cloud fraction
  dry_atm_.cldfrac = get_field_in("cldfrac_tot").get_view<const Real **>();

  // computed updraft velocity
  dry_atm_.w_updraft = buffer_.w_updraft;

  nihf_  = get_field_out("icenuc_num_hetfrz").get_view<Real **>();
  niim_  = get_field_out("icenuc_num_immfrz").get_view<Real **>();
  nidep_ = get_field_out("icenuc_num_depnuc").get_view<Real **>();
  nimey_ = get_field_out("icenuc_num_meydep").get_view<Real **>();
  naai_hom_ =
      get_field_out("num_act_aerosol_ice_nucle_hom").get_view<Real **>();

  naai_            = get_field_out("ni_activated").get_view<Real **>();
  qcld_            = get_field_out("qcld").get_view<Real **>();
  ptend_q_output_  = get_field_out("ptend_q").get_view<Real ***>();
  tendnd_          = get_field_out("nc_nuceat_tend").get_view<Real **>();
  factnum_         = get_field_out("factnum").get_view<Real ***>();
  ndropcol_        = get_field_out("ndropcol").get_view<Real **>();
  ndropmix_        = get_field_out("ndropmix").get_view<Real **>();
  nsource_         = get_field_out("nsource").get_view<Real **>();
  wtke_            = get_field_out("wtke").get_view<Real **>();
  ccn_             = get_field_out("ccn").get_view<Real ***>();
  coltend_outp_    = get_field_out("coltend").get_view<Real ***>();
  coltend_cw_outp_ = get_field_out("coltend_cw").get_view<Real ***>();
  hetfrz_immersion_nucleation_tend_ =
      get_field_out("hetfrz_immersion_nucleation_tend").get_view<Real **>();
  hetfrz_contact_nucleation_tend_ =
      get_field_out("hetfrz_contact_nucleation_tend").get_view<Real **>();
  hetfrz_depostion_nucleation_tend_ =
      get_field_out("hetfrz_depostion_nucleation_tend").get_view<Real **>();

  // interstitial and cloudborne aerosol tracers of interest: mass (q) and
  // number (n) mixing ratios
  for(int m = 0; m < mam_coupling::num_aero_modes(); ++m) {
    // interstitial aerosol tracers of interest: number (n) mixing ratios
    const char *int_nmr_field_name = mam_coupling::int_aero_nmr_field_name(m);
    wet_aero_.int_aero_nmr[m] =
        get_field_out(int_nmr_field_name).get_view<Real **>();
    dry_aero_.int_aero_nmr[m] = buffer_.dry_int_aero_nmr[m];

    // cloudborne aerosol tracers of interest: number (n) mixing ratios
    const char *cld_nmr_field_name = mam_coupling::cld_aero_nmr_field_name(m);
    wet_aero_.cld_aero_nmr[m] =
        get_field_out(cld_nmr_field_name).get_view<Real **>();
    dry_aero_.cld_aero_nmr[m] = buffer_.dry_cld_aero_nmr[m];

    for(int a = 0; a < mam_coupling::num_aero_species(); ++a) {
      // (interstitial) aerosol tracers of interest: mass (q) mixing ratios
      const char *int_mmr_field_name =
          mam_coupling::int_aero_mmr_field_name(m, a);

      if(strlen(int_mmr_field_name) > 0) {
        wet_aero_.int_aero_mmr[m][a] =
            get_field_out(int_mmr_field_name).get_view<Real **>();
        dry_aero_.int_aero_mmr[m][a] = buffer_.dry_int_aero_mmr[m][a];
      }

      // (cloudborne) aerosol tracers of interest: mass (q) mixing ratios
      const char *cld_mmr_field_name =
          mam_coupling::cld_aero_mmr_field_name(m, a);
      if(strlen(cld_mmr_field_name) > 0) {
        wet_aero_.cld_aero_mmr[m][a] =
            get_field_out(cld_mmr_field_name).get_view<Real **>();
        dry_aero_.cld_aero_mmr[m][a] = buffer_.dry_cld_aero_mmr[m][a];
      }
    }
  }
  for(int g = 0; g < mam_coupling::num_aero_gases(); ++g) {
    const char *gas_mmr_field_name = mam_coupling::gas_mmr_field_name(g);
    wet_aero_.gas_mmr[g] =
        get_field_out(gas_mmr_field_name).get_view<Real **>();
    dry_aero_.gas_mmr[g] = buffer_.dry_gas_mmr[g];
  }

  // Allocate work arrays
  for(int icnst = 0; icnst < mam4::ndrop::ncnst_tot; ++icnst) {
    qqcw_fld_work_[icnst] = view_2d("qqcw_fld_work_", ncol_, nlev_);
  }
  state_q_work_ =
      view_3d("state_q_work_", ncol_, nlev_, mam4::aero_model::pcnst);

  mam4::AeroConfig aero_config;
  // configure the nucleation parameterization
  mam4::NucleateIce::Config nucleate_ice_config;
  nucleate_ice_.init(aero_config, nucleate_ice_config);

  // configure the heterogeneous freezing parameterization
  mam4::Hetfrz::Config hetfrz_config;
  hetfrz_.init(aero_config, hetfrz_config);

  // set up our preprocess functor
  preprocess_.initialize(ncol_, nlev_, wet_atm_, wet_aero_, dry_atm_,
                         dry_aero_);
}  // end function initialize_impl

// ================================================================
//  RUN_IMPL
// ================================================================
void MAMAci::run_impl(const double dt) {
  // for (int kk=0; kk<72; ++kk){
  // std::cout << "num_cb_accum_top:"<<wet_aero_.cld_aero_nmr[0](0,kk)<<
  // std::endl;
  // }
  const auto scan_policy = ekat::ExeSpaceUtils<
      // KT::ExeSpace>::get_thread_range_parallel_scan_team_policy(ncol_,
      // nlev_);
      KT::ExeSpace>::get_thread_range_parallel_scan_team_policy(1, nlev_);

  // preprocess input -- needs a scan for the calculation of local derivied
  // quantities
  Kokkos::parallel_for("preprocess", scan_policy, preprocess_);
  Kokkos::fence();

  // haero::ThreadTeamPolicy team_policy(ncol_, Kokkos::AUTO);
  haero::ThreadTeamPolicy team_policy(1, Kokkos::AUTO);

  const int kb = 62;
  compute_w0_and_rho(team_policy, dry_atm_, top_lev_, nlev_,
                     // output
                     w0_, rho_);
  // std::cout << "T_mid:" << dry_atm_.T_mid(0, kb) << std::endl;
  // std::cout << "p_mid:" << dry_atm_.p_mid(0, kb) << std::endl;
  // std::cout << "wsec:" << w_sec_(0, kb) << std::endl;
  // std::cout << "w0:" << w0_(0, kb) << std::endl;
  // std::cout << " rho: " << rho_(0, kb) << std::endl;

  // compute vertical velocity variance  at the interfaces
  const Real w_sec_e3sm[73] = {
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.40000000000000002E-003,
      0.40000000000000002E-003, 0.44201892319518146E-003,
      0.77315620137962326E-003, 0.24806301482800117E-002,
      0.11109772692898754E-001, 0.40651094032490273E-001,
      0.82156694426095800E-001, 0.12207124453993526E+000,
      0.15516728994634110E+000, 0.17775318086169636E+000,
      0.18549817250146838E+000, 0.17184548286554119E+000,
      0.12741230682196053E+000, 0.65495229516041628E-001,
      0.26909155217660592E-001};
  for(int icol = 0; icol < ncol_; ++icol) {
    for(int kk = 0; kk < nlev_; ++kk) {
      w_sec_int_(icol, kk) = w_sec_e3sm[kk];
      // w_sec_int_(icol, kk) = w_sec_mid_(icol, kk);
    }
    // w_sec_int_(icol, nlev_ + 1) = w_sec_mid_(icol, nlev_);
    w_sec_int_(icol, nlev_ + 1) = w_sec_e3sm[nlev_ + 1];
  }

  compute_tke_using_w_sec(team_policy, w_sec_int_, nlev_,
                          // output
                          tke_);
  // std::cout << "TKE:" << tke_(0, kb) << std::endl;

  Kokkos::fence();  // wait for for tke_ to be computed.
  const Real &wsubmin = ACIRuntime_.wsubmin;  // runtime variable
  compute_subgrid_scale_velocities(team_policy, tke_, wsubmin, top_lev_, nlev_,
                                   // output
                                   wsub_, wsubice_, wsig_);
  // std::cout << "WSUB:" << wsub_(0, kb) << std::endl;
  // std::cout << "WICE:" << wsubice_(0, kb) << std::endl;
  // std::cout << "WSIG:" << wsig_(0, kb) << std::endl;

  Kokkos::fence();  // wait for wsig_ to be computed.

  compute_aitken_dry_diameter(team_policy, dgnum_, top_lev_, nlev_,
                              // output
                              aitken_dry_dia_);
  const Real dgnum_ait_e3sm[72] = {
      0.20877713336487552E-007, 0.21782230353342090E-007,
      0.21688324003865861E-007, 0.21112855042342451E-007,
      0.19162058462939536E-007, 0.18102979880838476E-007,
      0.17906980715477606E-007, 0.20271254074583327E-007,
      0.22698983422181942E-007, 0.24134835117044986E-007,
      0.25498156808001372E-007, 0.29796738799905547E-007,
      0.35822987394021908E-007, 0.41170963764365215E-007,
      0.44892726528330642E-007, 0.47217231342203107E-007,
      0.48928661807108766E-007, 0.50170939816128735E-007,
      0.51078750853732200E-007, 0.52247333465736065E-007,
      0.53190758580174931E-007, 0.53576491941850044E-007,
      0.53915614473890715E-007, 0.54510964775236826E-007,
      0.55643231691556703E-007, 0.57057811112589899E-007,
      0.58177383586181116E-007, 0.58209849180850108E-007,
      0.57976751598840998E-007, 0.52000000000000002E-007,
      0.50728746567226150E-007, 0.49119902704480870E-007,
      0.48212162162050883E-007, 0.49227715213506454E-007,
      0.46876827233752246E-007, 0.45360603896257791E-007,
      0.49986783979004747E-007, 0.51186879246229022E-007,
      0.50009353247048599E-007, 0.48250264542204811E-007,
      0.47560278748093609E-007, 0.48298089720730957E-007,
      0.49095935613468768E-007, 0.49493024126912931E-007,
      0.50250797590476007E-007, 0.51949267668322422E-007,
      0.53778727208416418E-007, 0.53563593301099588E-007,
      0.51218136771199298E-007, 0.43171429694325200E-007,
      0.39019610039033895E-007, 0.36175109143257051E-007,
      0.42731638777892750E-007, 0.38060728507221777E-007,
      0.44046323901481340E-007, 0.39216732751330010E-007,
      0.34842233953609988E-007, 0.34068804733226066E-007,
      0.30636043694263528E-007, 0.28302341686131413E-007,
      0.33023014309036320E-007, 0.34745748365385196E-007,
      0.43623545003583371E-007, 0.48206451795644064E-007,
      0.49854490325455530E-007, 0.50346335647724146E-007,
      0.50661560988561763E-007, 0.50986261962838767E-007,
      0.51256955985111086E-007, 0.51482578449096488E-007,
      0.51684364851091471E-007, 0.51849719162939729E-007};
  for(int icol = 0; icol < ncol_; ++icol) {
    for(int kk = 0; kk < nlev_; ++kk) {
      aitken_dry_dia_(icol, kk) = dgnum_ait_e3sm[kk];
    }
  }

  Kokkos::fence();  // wait for aitken_dry_dia_ to be computed.
  // std::cout << "aitken_dry_dia_:" << aitken_dry_dia_(0, kb) << std::endl;
  //  Compute Ice nucleation
  //  NOTE: The Fortran version uses "ast" for cloud fraction which is
  //  equivalent to "cldfrac_tot" in FM. It is part of the "dry_atm_" struct
  compute_nucleate_ice_tendencies(
      nucleate_ice_, team_policy, dry_atm_, dry_aero_, wsubice_,
      aitken_dry_dia_, nlev_, dt,
      // output
      nihf_, niim_, nidep_, nimey_, naai_hom_, naai_);

  for(int icol = 0; icol < ncol_; ++icol) {
    for(int kk = 0; kk < nlev_; ++kk) {
      naai_(icol, kk) =9876.0;
    }
  }

  store_liquid_cloud_fraction(team_policy, dry_atm_, liqcldf_, liqcldf_prev_,
                              top_lev_, nlev_,
                              // output
                              cloud_frac_, cloud_frac_prev_);

  // std::cout << "naai_hom_:" << naai_hom_(0, kb) << std::endl;
  // std::cout << "naai_:" << naai_(0, kb) << std::endl;

  // MUST FIXME: save cloud borne aerosols here!!!!
  //-------------------------------------------------------------
  //  Save cloud borne aerosols to be used in the heterozenous
  //  freezing before they are changed by the droplet activation
  //  process. This is only a select subset of cloud borne
  //  aerosols, not all the cloud borne aerosols.
  //-------------------------------------------------------------
  /*NOTE: We probably need to store indices for the select few cloud borne
  aerosols Fortran code: lchnk_zb = lchnk - begchunk ! save copy of cloud borne
  aerosols for use in heterogeneous freezing before ! we change it in dropmixnuc
  do ispec = 1, ncnst
     call pbuf_get_field(pbuf, hetfrz_aer_spec_idx(ispec), ptr2d)
     aer_cb(:ncol,:,ispec,lchnk_zb) = ptr2d(:ncol,:)
     aer_cb(:ncol,:,ispec,lchnk_zb) = aer_cb(:ncol,:,ispec,lchnk_zb) *
  rho(:ncol,:) enddo
  */

  compute_recipical_pseudo_density(team_policy, dry_atm_.p_del, nlev_,
                                   // output
                                   rpdel_);
  // std::cout << "rpdel_:" << rpdel_(0, kb) << std::endl;

  Kokkos::fence();  // wait for rpdel_ to be computed.

  const Real kvh_e3sm[73] = {0.25020913575496480E-002, 0.25021052914616470E-002,
                             0.75991761081225006E-002, 0.12291092068185365E-001,
                             0.11484807652762415E-001, 0.10856880396302943E-001,
                             0.10500384508819637E-001, 0.10361486171738229E-001,
                             0.10333325067964508E-001, 0.10258838031435397E-001,
                             0.10027325248446619E-001, 0.97784259072973521E-002,
                             0.96611845055866539E-002, 0.96249746122327937E-002,
                             0.95773431515696512E-002, 0.95180614513688099E-002,
                             0.94713233348487150E-002, 0.94503864489758338E-002,
                             0.94536294366578833E-002, 0.94575972194308883E-002,
                             0.94403767489615684E-002, 0.93975694769176284E-002,
                             0.93322843554751022E-002, 0.92777070192527501E-002,
                             0.92456776697171228E-002, 0.92266924824142716E-002,
                             0.92123025773060436E-002, 0.91888715633294191E-002,
                             0.91516797753615851E-002, 0.90958299606649744E-002,
                             0.89988037524983237E-002, 0.88220984587642423E-002,
                             0.85231270833157156E-002, 0.81397522619395188E-002,
                             0.79160421807845088E-002, 0.81206851117902653E-002,
                             0.86526891616674779E-002, 0.91682975412125615E-002,
                             0.96043394254592580E-002, 0.10033689085881327E-001,
                             0.10428656694074272E-001, 0.10715913043864789E-001,
                             0.10919631245454951E-001, 0.11250937075285789E-001,
                             0.11829292157343831E-001, 0.12413311776454055E-001,
                             0.12851317662157077E-001, 0.13175523677700330E-001,
                             0.13224182907540188E-001, 0.13085937680733115E-001,
                             0.12615055546741534E-001, 0.11995423733019836E-001,
                             0.12346556881757400E-001, 0.13433752971524651E-001,
                             0.13904308240950175E-001, 0.13539811748121957E-001,
                             0.12555099320041433E-001, 0.11519643673351362E-001,
                             0.11414071302852231E-001, 0.13409756835238139E-001,
                             0.24071962815959351E-001, 0.75489419450816414E-001,
                             0.62082011878960308E+000, 0.63952862312816796E+001,
                             0.16226857944175123E+002, 0.21882852534279891E+002,
                             0.24966173574402408E+002, 0.25710753126453692E+002,
                             0.24069881024271943E+002, 0.19743922403487922E+002,
                             0.98667814246712027E+001, 0.25633359450143991E+001,
                             0.14682471685037493E+001};
  // compute eddy diffusivity of heat at the interfaces
  for(int icol = 0; icol < ncol_; ++icol) {
    for(int kk = 0; kk < nlev_; ++kk) {
      // kvh_int_(icol, kk) = kvh_mid_(icol, kk);
      kvh_int_(icol, kk) = kvh_e3sm[kk];
    }
    // kvh_int_(icol, nlev_ + 1) = kvh_mid_(icol, nlev_);
    kvh_int_(icol, nlev_ + 1) = kvh_e3sm[nlev_ + 1];
  }

  call_function_dropmixnuc(team_policy, dt, dry_atm_, rpdel_, kvh_int_, wsub_,
                           cloud_frac_, cloud_frac_prev_, dry_aero_, nlev_,
                           // output
                           qqcw_fld_work_, ptend_q_, factnum_, tendnd_,
                           coltend_, coltend_cw_, qcld_, ndropcol_, ndropmix_,
                           nsource_, wtke_, ccn_,
                           // work arrays
                           raercol_cw_, raercol_, state_q_work_, nact_, mact_,
                           dropmixnuc_scratch_mem_);
  Kokkos::fence();  // wait for ptend_q_ to be computed.
  for(int icol = 0; icol < ncol_; ++icol) {
    for(int kk = 0; kk < nlev_; ++kk) {
      tendnd_(icol, kk) =1.2345;
    }
  }
  // std::cout << "factnum_:" << factnum_(0, 0, kb)<<" : "<< factnum_(0, 1,
  // kb)<<" : "<<factnum_(0, 2, kb)<<" : "<<factnum_(0, 3, kb)<< std::endl;
  // std::cout << "tendnd_:" << tendnd_(0, kb)<< std::endl;
  // for (int ic=9; ic<40; ++ic){
  // std::cout << "ptend_q_:" <<ic<<": "<< ptend_q_[ic](0, kb)<< std::endl;
  //}
  // for (int ic=0; ic<25; ++ic){
  // std::cout << "qqcw_:" <<ic<<": "<< qqcw_fld_work_[ic](0, kb)<< std::endl;
  //}
  std::cout << "num_cb_accum:" << dry_aero_.cld_aero_nmr[0](0, kb) << " : "
            << wet_aero_.cld_aero_nmr[0](0, kb) << std::endl;

  copy_mam4xx_array_to_scream<mam4::aero_model::pcnst>(
      team_policy, ptend_q_output_, ptend_q_, nlev_);
  copy_mam4xx_array_to_scream<mam4::ndrop::ncnst_tot>(
      team_policy, coltend_outp_, coltend_, nlev_);
  copy_mam4xx_array_to_scream<mam4::ndrop::ncnst_tot>(
      team_policy, coltend_cw_outp_, coltend_cw_, nlev_);

  call_hetfrz_compute_tendencies(
      team_policy, hetfrz_, dry_atm_, dry_aero_, factnum_, dt, nlev_,
      // output
      hetfrz_immersion_nucleation_tend_, hetfrz_contact_nucleation_tend_,
      hetfrz_depostion_nucleation_tend_,
      // work arrays
      diagnostic_scratch_);
  std::cout << "hetfrz_immersion_nucleation_tend_:"
            << hetfrz_immersion_nucleation_tend_(0, kb) << std::endl;
  std::cout << "hetfrz_contact_nucleation_tend_:"
            << hetfrz_contact_nucleation_tend_(0, kb) << std::endl;
  std::cout << "hetfrz_depostion_nucleation_tend_:"
            << hetfrz_depostion_nucleation_tend_(0, kb) << std::endl;

  Kokkos::fence();  // wait before returning to calling function
}

void MAMAci::finalize_impl() {
  m_atm_logger->log(ekat::logger::LogLevel::info, "calling ACI final");
}

}  // namespace scream

/*
FORTRAN OUTPUT:

 ----- OUPUT AT time step:           6           6
 wo  3.343177351577457E-004
 rho:   1.28186433384113
 TKE:  1.666465903934813E-002  6.097664104873541E-002
 wsub:  0.160873967324407
 wsubi:  0.200000000000000
 wsig:  0.160873967324407
 dgnum_ait:  4.362354500358337E-008
 naai:   11464.5957222634
 naai_hom:   29838.6879763933
 aer_cb: so4_c1  4.327927689273498E-012 so4_c1  4.327927689273498E-012
 aer_cb: bc_c1  6.811694223739704E-014 bc_c1  6.811694223739704E-014
 aer_cb: pom_c1  8.647238075040541E-013 pom_c1  8.647238075040541E-013
 aer_cb: soa_c1  6.734058317716400E-012 soa_c1  6.734058317716400E-012
 aer_cb: dst_c1  8.239303776951524E-014 dst_c1  8.239303776951524E-014
 aer_cb: ncl_c1  2.685810991448475E-014 ncl_c1  2.685810991448475E-014
 aer_cb: mom_c1  2.218253512777307E-015 mom_c1  2.218253512777307E-015
 aer_cb: num_c1   477478.884677930      num_c1   477478.884677930
 aer_cb: dst_c3  2.869197497896834E-012 dst_c3  2.869197497896834E-012
 aer_cb: ncl_c3  4.636857908110365E-013 ncl_c3  4.636857908110365E-013
 aer_cb: so4_c3  4.375223642985256E-014 so4_c3  4.375223642985256E-014
 aer_cb: bc_c3  3.674860666928688E-016 bc_c3  3.674860666928688E-016
 aer_cb: pom_c3  1.114802999982066E-015 pom_c3  1.114802999982066E-015
 aer_cb: soa_c3  8.915776993183282E-015 soa_c3  8.915776993183282E-015
 aer_cb: mom_c3  7.802642470782658E-017 mom_c3  7.802642470782658E-017
 aer_cb: num_c3   151.593971049442      num_c3   151.593971049442
 aer_cb: bc_c4  1.127495264613331E-027 bc_c4  1.127495264613331E-027
 aer_cb: pom_c4  8.204883715624573E-027 pom_c4  8.204883715624573E-027
 aer_cb: mom_c4  3.249069782816687E-033 mom_c4  3.249069782816687E-033
 aer_cb: num_c4  1.345885758586041E-013 num_c4  1.345885758586041E-013
 tendnd:  -839.404918218856        7289315.36108503        7792958.31201634
  1.666666666666667E-003
 factnum:  0.998556176544263       0.861689826773470
  0.999999999974140       0.000000000000000E+000
 nctend_mixnuc:  -839.404918218856
 O3_tend:  0.000000000000000E+000
 H2O2_tend:  0.000000000000000E+000
 H2SO4_tend:  0.000000000000000E+000
 SO2_tend:  0.000000000000000E+000
 DMS_tend:  0.000000000000000E+000
 SOAG_tend:  0.000000000000000E+000
 so4_a1_tend: -1.384310943569190E-014
 pom_a1_tend: -2.950799002682271E-015
 soa_a1_tend: -2.283869371271150E-014
 bc_a1_tend: -2.202672700280516E-016
 dst_a1_tend: -2.194060414083922E-016
 ncl_a1_tend: -6.981124745267083E-017
 mom_a1_tend: -5.662690535048673E-018
 num_a1_tend:  -1626.38730532537
 so4_a2_tend: -2.548707408341951E-017
 soa_a2_tend: -1.929990276139139E-018
 ncl_a2_tend: -6.855087738119723E-019
 mom_a2_tend: -4.571691604185304E-020
 num_a2_tend:  -111.890330245159
 dst_a3_tend: -7.609611333715698E-015
 ncl_a3_tend: -1.256350252705383E-015
 so4_a3_tend: -1.320988655880598E-016
 bc_a3_tend: -1.099586316024402E-018
 pom_a3_tend: -3.309939677824050E-018
 soa_a3_tend: -2.767668335981830E-017
 mom_a3_tend: -2.254911604324925E-019
 num_a3_tend: -0.406788905791186
 pom_a4_tend: -2.375070578317096E-017
 bc_a4_tend:  4.082479713528180E-017
 mom_a4_tend: -4.901356204764327E-023
 num_a4_tend:  -34.4232851017138
 so4_c1:    1322558.40616450
 pom_c1:   1.233997159684093E-011
 soa_c1:   2.393397505793018E-012
 bc_c1:   1.872520079842212E-011
 dst_c1:   1.922157644491776E-013
 ncl_c1:   2.530385880698310E-013
 mom_c1:   8.315014074126540E-014
 num_c1:   6.895143982427237E-015
 so4_c2:    142214.467855250
 soa_c2:   3.181118913367564E-014
 ncl_c2:   2.996638282752921E-015
 mom_c2:   8.055333842809433E-016
 num_c2:   5.202632275847699E-017
 dst_c3:    464.524744048298
 ncl_c3:   8.814810290885256E-012
 so4_c3:   1.417415096039318E-012
 bc_c3:   1.284022582681906E-013
 pom_c3:   1.081226492056768E-015
 soa_c3:   3.292708392236485E-015
 mom_c3:   2.584587804061707E-014
 num_c3:   2.324887367507579E-016
 pom_c4:   6.934165370527951E-014
 bc_c4:   2.703924752644086E-026
 mom_c4:   3.684880329580097E-027
 num_c4:   1.049556651713384E-032

 frzimm:  5.651860156917002E-006
 frzcnt:  0.000000000000000E+000
 frzdep:  0.000000000000000E+000

   */