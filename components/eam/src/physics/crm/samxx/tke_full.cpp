
#include "tke_full.h"

void tke_full(real5d &tke, int ind_tke, real5d &tk, int ind_tk, real5d &tkh, int ind_tkh) {
  auto &bet            = :: bet;
  auto &dz             = :: dz;
  auto &adzw           = :: adzw;
  auto &adz            = :: adz;
  auto &dt             = :: dt;
  auto &dtn            = :: dtn;
  auto &dx             = :: dx;
  auto &dy             = :: dy;
  auto &tabs           = :: tabs;
  auto &qcl            = :: qcl;
  auto &qci            = :: qci;
  auto &qv             = :: qv;
  auto &qpl            = :: qpl;
  auto &qpi            = :: qpi;
  auto &t              = :: t;
  auto &epsv           = :: epsv;
  auto &presi          = :: presi;
  auto &tkelediss      = :: tkelediss;
  auto &tkesbdiss      = :: tkesbdiss;
  auto &tkesbshear     = :: tkesbshear;
  auto &tkesbbuoy      = :: tkesbbuoy;
  auto &grdf_x         = :: grdf_x;
  auto &grdf_y         = :: grdf_y;
  auto &grdf_z         = :: grdf_z;
  auto &dosmagor       = :: dosmagor;
  auto &sgs_field      = :: sgs_field;
  auto &sgs_field_diag = :: sgs_field_diag;
  auto &ncrms          = :: ncrms;
  auto &fluxbt         = :: fluxbt;
  auto &fluxbq         = :: fluxbq;
  auto &tsfc           = :: tsfc;

  real constexpr tk_min_value = 0.05;
  real constexpr tk_min_depth = 500.0;
  real constexpr Cs = 0.15;
  real constexpr Ck = 0.1;
  real constexpr Ce = (Ck*Ck*Ck)/(Cs*Cs*Cs*Cs);
  real constexpr Ces = Ce/0.7*3.0;
  real constexpr Pr = 1.0;

  real4d def2("def2", nzm, ny, nx, ncrms);
  real4d buoy_sgs_vert("buoy_sgs_vert", nzm+1,ny,nx,ncrms);
  real4d a_prod_bu_vert("buoy_sgs_vert", nzm+1,ny,nx,ncrms);

  if (RUN3D) {
    shear_prod3D(def2);
  } else {
    shear_prod2D(def2);
  }

  //  for (int j=0; j<ny; j++) {
  //   for (int i=0; i<nx; i++) {
  //     for (int icrm=0; icrm<ncrms; icrm++) {
  parallel_for( SimpleBounds<3>(ny,nx,ncrms) , YAKL_LAMBDA (int j, int i, int icrm) {
    a_prod_bu_vert(0,j,i,icrm) = 0.0;
    buoy_sgs_vert(0,j,i,icrm) = 0.0;
    a_prod_bu_vert(nzm,j,i,icrm) = 0.0;
    buoy_sgs_vert(nzm,j,i,icrm) = 0.0;
  });

#ifdef MMF_TKE_MOD
  if ((nstep==1) and (icycle==1)) {
    // At start of simulation, make sure that subgrid TKE
    // is non-zero at surface if surface buoyancy fluxes are positive.
    // If they are, compute the TKE implied by local equilibrium between
    // turbulence production by the surface fluxes and dissipation. Take
    // the initial TKE in the lowest level to be the larger of that and the 
    // initial value. Since the present values of TKE, eddy viscosity and eddy
    // diffusivity are used in computing the new values, it is
    // important for them not to be zero initially if the buoyancy
    // flux is non-zero initially.
    parallel_for( SimpleBounds<3>(ny,nx,ncrms) , YAKL_LAMBDA (int j, int i, int icrm) {
      // compute suface buoyancy flux
      real bbb = 1.+epsv*qv(0,j,i,icrm);
      a_prod_bu_vert(0,j,i,icrm) = bbb*bet(0,icrm)*fluxbt(j,i,icrm) + 
                                   bet(0,icrm)*epsv*(tsfc(j,i,icrm))*fluxbq(j,i,icrm);
      real grd = dz(icrm)*adz(0,icrm);
      real Ce1 = Ce/0.7*0.19;
      real Ce2 = Ce/0.7*0.51;
      real Cee = Ce1+Ce2;
      // Choose the subgrid TKE to be the larger of the initial value or
      // that which satisfies local equilibrium, buoyant production = dissipation
      // or a_prod_bu = Cee/grd * tke^(3/2).
      // NOTE: We're ignoring shear production here.
      real tke_tmp = pow( grd/Cee * max( 1.0-20, 0.5*a_prod_bu_vert(0,j,i,icrm) ) ,(2.0/3.0));
      tke(ind_tke,0,j+offy_s,i+offx_s,icrm) = max( tke(ind_tke,0,j+offy_s,i+offx_s,icrm), tke_tmp );
      // eddy viscosity = Ck*grd * sqrt(tke) --- analogous for Smagorinksy.
      tk(ind_tk,0,j+offy_d,i+offx_d,icrm) = Ck*grd * sqrt( tke(ind_tke,0,j+offy_s,i+offx_s,icrm) );
      // eddy diffusivity = Pr * eddy viscosity
      tkh(ind_tkh,0,j+offy_d,i+offx_d,icrm) = Pr*tk(0,j,i,icrm);
    });
  }

  // compute subgrid buoyancy flux at w-levels, starting with surface buoyancy flux
  parallel_for( SimpleBounds<3>(ny,nx,ncrms) , YAKL_LAMBDA (int j, int i, int icrm) {
    // Use surface temperature and vapor mixing ratio. This is slightly inconsistent, 
    // but the error is small, and it's cheaper than another saturation mixing ratio computation.
    real bbb = 1.+epsv*qv(0,j,i,icrm);
    a_prod_bu_vert(0,j,i,icrm) = bbb*bet(0,icrm)*fluxbt(j,i,icrm) + 
                                bet(0,icrm)*epsv*(tsfc(j,i,icrm))*fluxbq(j,i,icrm);
    // back buoy_sgs out from buoyancy flux, a_prod_bu = - (tkh(k,j,i,icrm)+0.001)*buoy_sgs
    buoy_sgs_vert(0,j,i,icrm) = - a_prod_bu_vert(0,j,i,icrm)/(tkh(0,j,i,icrm)+0.001);
  });
#endif

  // for (int k=0; k<nzm-1; k++) {
  //   for (int j=0; j<ny; j++) {
  //     for (int i=0; i<nx; i++) {
  //       for (int icrm=0; icrm<ncrms; icrm++) {
  parallel_for( SimpleBounds<4>(nzm-1,ny,nx,ncrms) , YAKL_LAMBDA (int k, int j, int i, int icrm) {
    int kb,kc;
    real betdz, tabs_interface, qtot_interface, qp_interface, bbb, buoy_sgs,
         qctot, omn, qsat_interface, qsat_check, lstarn, dqsat, qsatt, 
         qsatw, qsati, dtqsatw, dtqsati;

    if (k<nzm-1) {
      //k always less than nzm-1??????
      kc = k+1;
      kb = k;
    } else {
      kc = k;
      kb = k-1;
    }

    // first compute subgrid buoyancy flux at interface above this level.
    // average betdz to w-levels
    betdz = 0.5*(bet(kc,icrm)+bet(kb,icrm))/dz(icrm)/adzw(k+1,icrm);
    
    // compute temperature of mixture between two grid levels if all cloud
    // were evaporated and sublimated
    tabs_interface = 
     0.5*( tabs(kc,j,i,icrm) + fac_cond*qcl(kc,j,i,icrm) + fac_sub*qci(kc,j,i,icrm) 
         + tabs(kb,j,i,icrm) + fac_cond*qcl(kb,j,i,icrm) + fac_sub*qci(kb,j,i,icrm) );

     // similarly for water vapor if all cloud evaporated/sublimated
     qtot_interface = 
         0.5*( qv(kc,j,i,icrm) + qcl(kc,j,i,icrm) + qci(kc,j,i,icrm) 
             + qv(kb,j,i,icrm) + qcl(kb,j,i,icrm) + qci(kb,j,i,icrm) );

     qp_interface = 0.5*( qpl(kc,j,i,icrm) + qpi(kc,j,i,icrm) + qpl(kb,j,i,icrm) + qpi(kb,j,i,icrm) );

     bbb = 1.0+epsv*qtot_interface - qp_interface;
     buoy_sgs=betdz*( bbb*(t(kc,j+offy_s,i+offx_s,icrm)-t(kb,j+offy_s,i+offx_s,icrm))
         +epsv*tabs_interface* 
         (qv(kc,j,i,icrm)+qcl(kc,j,i,icrm)+qci(kc,j,i,icrm)-qv(kb,j,i,icrm)-qcl(kb,j,i,icrm)-qci(kb,j,i,icrm)) 
         +(bbb*fac_cond-tabs_interface)*(qpl(kc,j,i,icrm)-qpl(kb,j,i,icrm))
         +(bbb*fac_sub -tabs_interface)*(qpi(kc,j,i,icrm)-qpi(kb,j,i,icrm)) );


     buoy_sgs_vert(k+1,j,i,icrm) = buoy_sgs;
     a_prod_bu_vert(k+1,j,i,icrm) = -0.5*(tkh(ind_tkh,kc,j+offy_d,i+offx_d,icrm)+
                                          tkh(ind_tkh,kb,j+offy_d,i+offx_d,icrm)+0.002)*buoy_sgs;

     //-----------------------------------------------------------------------
     // now go back and check for cloud
     //-----------------------------------------------------------------------

     // if there's any cloud in the grid cells above or below, check to see if
     // the mixture between the two levels is also cloudy
     qctot = qcl(kc,j,i,icrm)+qci(kc,j,i,icrm)+qcl(kb,j,i,icrm)+qci(kb,j,i,icrm);

     if(qctot > 0.0) {
     

      // figure out the fraction of condensate that's liquid
      omn = (qcl(kc,j,i,icrm)+qcl(kb,j,i,icrm))/(qctot+1.e-20); 

      // compute temperature of mixture between two grid levels
      // if all cloud were evaporated and sublimated
      tabs_interface = 
           0.5*( tabs(kc,j,i,icrm) + fac_cond*qcl(kc,j,i,icrm) + fac_sub*qci(kc,j,i,icrm)
               + tabs(kb,j,i,icrm) + fac_cond*qcl(kb,j,i,icrm) + fac_sub*qci(kb,j,i,icrm ) ); 

      // similarly for total water (vapor + cloud) mixing ratio
      qtot_interface = 
           0.5*( qv(kc,j,i,icrm) + qcl(kc,j,i,icrm) + qci(kc,j,i,icrm)
               + qv(kb,j,i,icrm) + qcl(kb,j,i,icrm) + qci(kb,j,i,icrm) );

      // compute saturation mixing ratio at this temperature
      qsatw_crm(tabs_interface,presi(k+1,icrm),qsatw);
      qsati_crm(tabs_interface,presi(k+1,icrm),qsati);
      qsat_check = omn*qsatw + (1.0-omn)*qsati;

      // check to see if the total water exceeds this saturation mixing ratio.
      // if so, apply the cloudy relations for subgrid buoyancy flux
      if(qtot_interface > qsat_check) {
          
        // apply cloudy relations for buoyancy flux, use the liquid-ice breakdown computed above.
        lstarn = fac_cond+(1.0-omn)*fac_fus;
        // use the average values of T from the two levels to compute qsat, dqsat
        // and the multipliers for the subgrid buoyancy fluxes.  Note that the
        // interface is halfway between neighboring levels, so that the potential
        // energy cancels out.  This is approximate and neglects the effects of
        // evaporation/condensation with mixing.  Hopefully good enough.
        tabs_interface = 0.5*( tabs(kc,j,i,icrm) + tabs(kb,j,i,icrm) );

        qp_interface = 0.5*( qpl(kc,j,i,icrm) + qpi(kc,j,i,icrm) + qpl(kb,j,i,icrm) + qpi(kb,j,i,icrm) );

        dtqsatw_crm(tabs_interface,presi(k+1,icrm),dtqsatw);
        dtqsati_crm(tabs_interface,presi(k+1,icrm),dtqsati);
        dqsat = omn*dtqsatw + (1.0-omn)*dtqsati;

        qsatw_crm(tabs_interface,presi(k+1,icrm),qsatw);
        qsati_crm(tabs_interface,presi(k+1,icrm),qsati);
        qsatt = omn*qsatw + (1.0-omn)*qsati;


        // condensate loading term
        bbb = 1.0 + epsv*qsatt 
            + qsatt - qtot_interface - qp_interface 
            +1.61*tabs_interface*dqsat;
        bbb = bbb / (1.0+lstarn*dqsat);

        buoy_sgs = betdz*(bbb*(t(kc,j+offy_s,i+offx_s,icrm)-t(kb,j+offy_s,i+offx_s,icrm))
                 +(bbb*lstarn - (1.0+lstarn*dqsat)*tabs_interface)*
                 (qv(kc,j,i,icrm)+qcl(kc,j,i,icrm)+qci(kc,j,i,icrm)-qv(kb,j,i,icrm)-qcl(kb,j,i,icrm)-qci(kb,j,i,icrm))
                 + ( bbb*fac_cond-(1.0+fac_cond*dqsat)*tabs(k,j,i,icrm) ) * ( qpl(kc,j,i,icrm)-qpl(kb,j,i,icrm) )
                 + ( bbb*fac_sub -(1.0+fac_sub *dqsat)*tabs(k,j,i,icrm) ) * ( qpi(kc,j,i,icrm)-qpi(kb,j,i,icrm) ) );

        buoy_sgs_vert(k+1,j,i,icrm) = buoy_sgs;
        a_prod_bu_vert(k+1,j,i,icrm) = -0.5*(tkh(ind_tkh,kc,j+offy_d,i+offx_d,icrm)+
                                             tkh(ind_tkh,kb,j+offy_d,i+offx_d,icrm)+0.002)*buoy_sgs;
      }
    }

  });
  
  // for (int k=0; k<nzm-1; k++) {
  //  for (int icrm=0; icrm<ncrms; icrm++) {
  parallel_for( SimpleBounds<2>(nz,ncrms) , YAKL_LAMBDA (int k, int icrm) {
    tkelediss(k,icrm) = 0.0;
    tkesbdiss(k,icrm) = 0.0;
    tkesbshear(k,icrm) = 0.0;
    tkesbbuoy(k,icrm) = 0.0;
  });

  // for (int k=0; k<nzm-1; k++) {
  //   for (int j=0; j<ny; j++) {
  //     for (int i=0; i<nx; i++) {
  //       for (int icrm=0; icrm<ncrms; icrm++) {
  parallel_for( SimpleBounds<4>(nzm-1,ny,nx,ncrms) , YAKL_DEVICE_LAMBDA (int k, int j, int i, int icrm) {
    real grd, Ce1, Ce2, cx, cy, cz, tkmax, smix, ratio, Cee, a_prod_sh, a_prod_bu,
         a_diss, tmp, buoy_sgs;

    grd = dz(icrm)*adz(k,icrm);
    Ce1 = Ce/0.7*0.19;
    Ce2 = Ce/0.7*0.51;
    // compute correction factors for eddy visc/cond not to acceed 3D stability
    cx = dx*dx/dt/grdf_x(k,icrm);
    cy = dy*dy/dt/grdf_y(k,icrm);
    real tmp1 = dz(icrm)*min(adzw(k,icrm),adzw(k+1,icrm));
    cz = tmp1*tmp1/dt/grdf_z(k,icrm);
    // maximum value of eddy visc/cond
    tkmax = 0.09/(1.0/cx+1.0/cy+1.0/cz);
    buoy_sgs = 0.5*( buoy_sgs_vert(k,j,i,icrm) + buoy_sgs_vert(k+1,j,i,icrm) );
    if (buoy_sgs <= 0.0) {
      smix = grd;
    } else {
      smix = min(grd,max(0.1*grd, sqrt(0.76*tk(ind_tk,k,j+offy_d,i+offx_d,icrm)/Ck/sqrt(buoy_sgs+1.e-10))));
    }
    ratio = smix/grd;
    Cee = Ce1+Ce2*ratio;
    if (dosmagor) {
      tk(ind_tk,k,j+offy_d,i+offx_d,icrm) = sqrt(Ck*Ck*Ck/Cee*max(0.0,def2(k,j,i,icrm)-Pr*buoy_sgs))*smix*smix;
      tmp1 = tk(ind_tk,k,j+offy_d,i+offx_d,icrm)/(Ck*smix);
      tke(ind_tke,k,j+offy_s,i+offx_s,icrm) = tmp1*tmp1;
      a_prod_sh = (tk(ind_tk,k,j+offy_d,i+offx_d,icrm)+0.001)*def2(k,j,i,icrm);
      a_prod_bu = 0.5*( a_prod_bu_vert(k,j,i,icrm) + a_prod_bu_vert(k+1,j,i,icrm) );
      a_diss = a_prod_sh+a_prod_bu;
    } else {
      tke(ind_tke,k,j+offy_s,i+offx_s,icrm) = max(0.0,tke(ind_tke,k,j+offy_s,i+offx_s,icrm));
      a_prod_sh = (tk(ind_tk,k,j+offy_d,i+offx_d,icrm)+0.001)*def2(k,j,i,icrm);
      a_prod_bu = 0.5*( a_prod_bu_vert(k,j,i,icrm) + a_prod_bu_vert(k+1,j,i,icrm) );
      // cap the diss rate (useful for large time steps)
      a_diss = min(tke(ind_tke,k,j+offy_s,i+offx_s,icrm)/(4.0*dt),Cee/smix*pow(tke(ind_tke,k,j+offy_s,i+offx_s,icrm),1.5));
      tke(ind_tke,k,j+offy_s,i+offx_s,icrm) = max(0.0,tke(ind_tke,k,j+offy_s,i+offx_s,icrm)+
                                                      dtn*(max(0.0,a_prod_sh+a_prod_bu)-a_diss));
      tk(ind_tk,k,j+offy_d,i+offx_d,icrm)  = Ck*smix*sqrt(tke(ind_tke,k,j+offy_s,i+offx_s,icrm));
    }
    tk(ind_tk,k,j+offy_d,i+offx_d,icrm)  = min(tk(ind_tk,k,j+offy_d,i+offx_d,icrm),tkmax);
    tkh(ind_tkh,k,j+offy_d,i+offx_d,icrm) = Pr*tk(ind_tk,k,j+offy_d,i+offx_d,icrm);

    tmp = a_prod_sh/( (real) nx * (real) ny );
    yakl::atomicAdd(tkelediss(k,icrm),-tmp);
    yakl::atomicAdd(tkesbdiss(k,icrm),a_diss);
    yakl::atomicAdd(tkesbshear(k,icrm),a_prod_sh);
    yakl::atomicAdd(tkesbbuoy(k,icrm),a_prod_bu);

  });
}


