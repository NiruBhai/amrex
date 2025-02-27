#ifndef AMREX_EB2_LEVEL_H_
#define AMREX_EB2_LEVEL_H_
#include <AMReX_Config.H>

#include <AMReX_ParmParse.H>
#include <AMReX_Geometry.H>
#include <AMReX_MultiFab.H>
#include <AMReX_LayoutData.H>
#include <AMReX_VisMF.H>
#include <AMReX_Array.H>
#include <AMReX_EBCellFlag.H>
#include <AMReX_MultiCutFab.H>
#include <AMReX_EB2_MultiGFab.H>
#include <AMReX_EB2_C.H>
#include <AMReX_EB2_IF_AllRegular.H>

#ifdef AMREX_USE_OMP
#include <omp.h>
#endif

#include <unordered_map>
#include <limits>
#include <cmath>
#include <type_traits>

namespace amrex { namespace EB2 {

class IndexSpace;

class Level
{
public:

    bool isAllRegular () const noexcept { return m_allregular; }
    bool isOK () const noexcept { return m_ok; }
    void fillEBCellFlag (FabArray<EBCellFlagFab>& cellflag, const Geometry& geom) const;
    void fillVolFrac (MultiFab& vfrac, const Geometry& geom) const;
    void fillCentroid (MultiCutFab& centroid, const Geometry& geom) const;
    void fillCentroid (   MultiFab& centroid, const Geometry& geom) const;
    void fillBndryArea (MultiCutFab& bndryarea, const Geometry& geom) const;
    void fillBndryArea (   MultiFab& bndryarea, const Geometry& geom) const;
    void fillBndryCent (MultiCutFab& bndrycent, const Geometry& geom) const;
    void fillBndryCent (   MultiFab& bndrycent, const Geometry& geom) const;
    void fillBndryNorm (MultiCutFab& bndrynorm, const Geometry& geom) const;
    void fillBndryNorm (   MultiFab& bndrynorm, const Geometry& geom) const;
    void fillAreaFrac (Array<MultiCutFab*,AMREX_SPACEDIM> const& areafrac, const Geometry& geom) const;
    void fillAreaFrac (Array<   MultiFab*,AMREX_SPACEDIM> const& areafrac, const Geometry& geom) const;
    void fillFaceCent (Array<MultiCutFab*,AMREX_SPACEDIM> const& facecent, const Geometry& geom) const;
    void fillFaceCent (Array<   MultiFab*,AMREX_SPACEDIM> const& facecent, const Geometry& geom) const;
    void fillEdgeCent (Array<MultiCutFab*,AMREX_SPACEDIM> const& edgecent, const Geometry& geom) const;
    void fillEdgeCent (Array<   MultiFab*,AMREX_SPACEDIM> const& edgecent, const Geometry& geom) const;
    void fillLevelSet (MultiFab& levelset, const Geometry& geom) const;

    const BoxArray& boxArray () const noexcept { return m_grids; }
    const DistributionMapping& DistributionMap () const noexcept { return m_dmap; }

    Level (IndexSpace const* is, const Geometry& geom) : m_geom(geom), m_parent(is) {}
    void prepareForCoarsening (const Level& rhs, int max_grid_size, IntVect ngrow);

    const Geometry& Geom () const noexcept { return m_geom; }
    IndexSpace const* getEBIndexSpace () const noexcept { return m_parent; }

protected:

    Level (Level && rhs) = default;

    Level (Level const& rhs) = delete;
    Level& operator=(Level const& rhs) = delete;
    Level& operator=(Level && rhs) = delete;

    Geometry m_geom;
    IntVect  m_ngrow;
    BoxArray m_grids;
    BoxArray m_covered_grids;
    DistributionMapping m_dmap;
    MultiGFab m_mgf;
    MultiFab m_levelset;
    FabArray<EBCellFlagFab> m_cellflag;
    MultiFab m_volfrac;
    MultiFab m_centroid;
    MultiFab m_bndryarea;
    MultiFab m_bndrycent;
    MultiFab m_bndrynorm;
    Array<MultiFab,AMREX_SPACEDIM> m_areafrac;
    Array<MultiFab,AMREX_SPACEDIM> m_facecent;
    Array<MultiFab,AMREX_SPACEDIM> m_edgecent;
    bool m_allregular = false;
    bool m_ok = false;
    IndexSpace const* m_parent;

public: // for cuda
    int coarsenFromFine (Level& fineLevel, bool fill_boundary);
    void buildCellFlag ();
};

template <typename G>
class GShopLevel
    : public Level
{
public:
    GShopLevel (IndexSpace const* is, G const& gshop, const Geometry& geom, int max_grid_size, int ngrow, bool extend_domain_face);
    GShopLevel (IndexSpace const* is, int ilev, int max_grid_size, int ngrow,
                const Geometry& geom, GShopLevel<G>& fineLevel);
};

template <typename G>
GShopLevel<G>::GShopLevel (IndexSpace const* is, G const& gshop, const Geometry& geom,
                           int max_grid_size, int ngrow, bool extend_domain_face)
    : Level(is, geom)
{
    if (std::is_same<typename G::FunctionType, AllRegularIF>::value) {
        m_allregular = true;
        m_ok = true;
        return;
    }

    if (amrex::Verbose() > 0 && extend_domain_face == false) {
        amrex::Print() << "AMReX WARNING: extend_domain_face=false is not recommended!\n";
    }

    BL_PROFILE("EB2::GShopLevel()-fine");

    Real small_volfrac = 1.e-14;
    bool cover_multiple_cuts = false;
    int maxiter = 32;
    {
        ParmParse pp("eb2");
        pp.queryAdd("small_volfrac", small_volfrac);
        pp.queryAdd("cover_multiple_cuts", cover_multiple_cuts);
        pp.queryAdd("maxiter", maxiter);
    }

    // make sure ngrow is multiple of 16
    m_ngrow = IntVect{static_cast<int>(std::ceil(ngrow/16.)) * 16};

    Box const& domain = geom.Domain();
    Box domain_grown = domain;
    for (int idim = 0; idim < AMREX_SPACEDIM; ++idim) {
        if (geom.isPeriodic(idim)) {
            m_ngrow[idim] = 0;
        } else {
            m_ngrow[idim] = std::min(m_ngrow[idim], domain_grown.length(idim));
        }
    }
    domain_grown.grow(m_ngrow);
    Box bounding_box = (extend_domain_face) ? domain : domain_grown;
    bounding_box.surroundingNodes();

    BoxList bl(domain);
    bl.maxSize(max_grid_size);
    if (m_ngrow != 0) {
        const IntVect& domlo = domain.smallEnd();
        const IntVect& domhi = domain.bigEnd();
        for (auto& b : bl) {
            for (int idim = 0; idim < AMREX_SPACEDIM; ++idim) {
                if (m_ngrow[idim] != 0) {
                    if (b.smallEnd(idim) == domlo[idim]) {
                        b.growLo(idim,m_ngrow[idim]);
                    }
                    if (b.bigEnd(idim) == domhi[idim]) {
                        b.growHi(idim,m_ngrow[idim]);
                    }
                }
            }
        }
    }

    m_grids.define(std::move(bl));
    m_dmap.define(m_grids);

    Vector<Box> cut_boxes;
    Vector<Box> covered_boxes;

    for (MFIter mfi(m_grids, m_dmap); mfi.isValid(); ++mfi)
    {
        const Box& vbx = mfi.validbox();
        const Box& gbx = amrex::surroundingNodes(amrex::grow(vbx,1));
        int box_type = gshop.getBoxType(gbx & bounding_box, geom, RunOn::Gpu);
        if (box_type == gshop.allcovered) {
            covered_boxes.push_back(vbx);
        } else if (box_type == gshop.mixedcells) {
            cut_boxes.push_back(vbx);
        }
    }

    amrex::AllGatherBoxes(cut_boxes);
    amrex::AllGatherBoxes(covered_boxes);

    if ( cut_boxes.empty() &&
        !covered_boxes.empty())
    {
        amrex::Abort("AMReX_EB2_Level.H: Domain is completely covered");
    }

    if (!covered_boxes.empty()) {
        m_covered_grids = BoxArray(BoxList(std::move(covered_boxes)));
    }

    if (cut_boxes.empty()) {
        m_grids = BoxArray();
        m_dmap = DistributionMapping();
        m_allregular = true;
        m_ok = true;
        return;
    }

    m_grids = BoxArray(BoxList(std::move(cut_boxes)));
    m_dmap = DistributionMapping(m_grids);

    m_mgf.define(m_grids, m_dmap);
    const int ng = GFab::ng;
    MFInfo mf_info;
    mf_info.SetTag("EB2::Level");
    m_cellflag.define(m_grids, m_dmap, 1, ng, mf_info);
    m_volfrac.define(m_grids, m_dmap, 1, ng, mf_info);
    m_centroid.define(m_grids, m_dmap, AMREX_SPACEDIM, ng, mf_info);
    m_bndryarea.define(m_grids, m_dmap, 1, ng, mf_info);
    m_bndrycent.define(m_grids, m_dmap, AMREX_SPACEDIM, ng, mf_info);
    m_bndrynorm.define(m_grids, m_dmap, AMREX_SPACEDIM, ng, mf_info);
    for (int idim = 0; idim < AMREX_SPACEDIM; ++idim) {
        m_areafrac[idim].define(amrex::convert(m_grids, IntVect::TheDimensionVector(idim)),
                                m_dmap, 1, ng, mf_info);
        m_facecent[idim].define(amrex::convert(m_grids, IntVect::TheDimensionVector(idim)),
                                m_dmap, AMREX_SPACEDIM-1, ng, mf_info);
        IntVect edge_type{1}; edge_type[idim] = 0;
        m_edgecent[idim].define(amrex::convert(m_grids, edge_type), m_dmap, 1, ng, mf_info);
    }

    const auto dx = geom.CellSizeArray();
    const auto problo = geom.ProbLoArray();

    for (int idim = 0; idim < AMREX_SPACEDIM; ++idim) {
        if (!extend_domain_face || geom.isPeriodic(idim)) {
            bounding_box.grow(idim,GFab::ng);
        }
    }

    RunOn gshop_run_on = (Gpu::inLaunchRegion() && gshop.isGPUable())
        ? RunOn::Gpu : RunOn::Cpu;

    bool hybrid = Gpu::inLaunchRegion() && (gshop_run_on == RunOn::Cpu);

    int iter = 0;
    for (; iter < maxiter; ++iter)
    {
        int nsmallcells = 0;
        int nmulticuts = 0;
#ifdef AMREX_USE_OMP
#pragma omp parallel if (Gpu::notInLaunchRegion()) reduction(+:nsmallcells,nmulticuts)
#endif
        {
#if (AMREX_SPACEDIM == 3)
            Array<BaseFab<Real>, AMREX_SPACEDIM> M2;
            EBCellFlagFab cellflagtmp;
#endif
            for (MFIter mfi(m_mgf); mfi.isValid(); ++mfi)
            {
                auto& gfab = m_mgf[mfi];
                const Box& vbx = gfab.validbox();

                auto& levelset = gfab.getLevelSet();
                if (iter == 0) {
                    gshop.fillFab(levelset, geom, gshop_run_on, bounding_box);
                    if (hybrid) {
                        levelset.prefetchToDevice();
                    }
                }

                auto& cellflag = m_cellflag[mfi];

                gfab.buildTypes(cellflag);

                Array4<Real const> const& clst = levelset.const_array();
                Array4<Real      > const&  lst = levelset.array();
                Array4<EBCellFlag> const& cfg = m_cellflag.array(mfi);
                Array4<Real> const& vfr = m_volfrac.array(mfi);
                Array4<Real> const& ctr = m_centroid.array(mfi);
                Array4<Real> const& bar = m_bndryarea.array(mfi);
                Array4<Real> const& bct = m_bndrycent.array(mfi);
                Array4<Real> const& bnm = m_bndrynorm.array(mfi);
                AMREX_D_TERM(Array4<Real> const& apx = m_areafrac[0].array(mfi);,
                             Array4<Real> const& apy = m_areafrac[1].array(mfi);,
                             Array4<Real> const& apz = m_areafrac[2].array(mfi););
                AMREX_D_TERM(Array4<Real> const& fcx = m_facecent[0].array(mfi);,
                             Array4<Real> const& fcy = m_facecent[1].array(mfi);,
                             Array4<Real> const& fcz = m_facecent[2].array(mfi););

                auto& facetype = gfab.getFaceType();
                AMREX_D_TERM(Array4<Type_t> const& ftx = facetype[0].array();,
                             Array4<Type_t> const& fty = facetype[1].array();,
                             Array4<Type_t> const& ftz = facetype[2].array(););

                int nmc = 0;
                int nsm = 0;

#if (AMREX_SPACEDIM == 3)
                auto& edgetype = gfab.getEdgeType();
                Array4<Type_t const> const& xdg = edgetype[0].const_array();
                Array4<Type_t const> const& ydg = edgetype[1].const_array();
                Array4<Type_t const> const& zdg = edgetype[2].const_array();

                Array4<Real> const& xip = m_edgecent[0].array(mfi);
                Array4<Real> const& yip = m_edgecent[1].array(mfi);
                Array4<Real> const& zip = m_edgecent[2].array(mfi);

                if (iter == 0) {
                    if (hybrid) {
                        Gpu::streamSynchronize();
                        for (int idim = 0; idim < AMREX_SPACEDIM; ++idim) {
                            edgetype[idim].prefetchToHost();
                            m_edgecent[idim][mfi].prefetchToHost();
                        }
                    }

                    gshop.getIntercept({xip,yip,zip}, {xdg,ydg,zdg},
                                       geom, gshop_run_on, bounding_box);

                    if (hybrid) {
                        for (int idim = 0; idim < AMREX_SPACEDIM; ++idim) {
                            edgetype[idim].prefetchToDevice();
                            m_edgecent[idim][mfi].prefetchToDevice();
                        }
                    }
                } else {
                    gshop.updateIntercept({xip,yip,zip}, {xdg,ydg,zdg}, clst, geom);
                }

                for (int idim = 0; idim < AMREX_SPACEDIM; ++idim) {
                    const Box& b = facetype[idim].box();
                    M2[idim].resize(b,3);
                }
                Array4<Real> const& xm2 = M2[0].array();
                Array4<Real> const& ym2 = M2[1].array();
                Array4<Real> const& zm2 = M2[2].array();

                nmc = build_faces(vbx, cfg, ftx, fty, ftz, xdg, ydg, zdg, lst,
                                  xip, yip, zip, apx, apy, apz, fcx, fcy, fcz,
                                  xm2, ym2, zm2, dx, problo, cover_multiple_cuts);

                cellflagtmp.resize(m_cellflag[mfi].box());
                Elixir cellflagtmp_eli = cellflagtmp.elixir();
                Array4<EBCellFlag> const& cfgtmp = cellflagtmp.array();

                build_cells(vbx, cfg, ftx, fty, ftz, apx, apy, apz,
                            fcx, fcy, fcz, xm2, ym2, zm2, vfr, ctr,
                            bar, bct, bnm, cfgtmp, lst,
                            small_volfrac, geom, extend_domain_face, cover_multiple_cuts,
                            nsm, nmc);

                // Becasue it is used in a synchronous reduction kernel in
                // build_cells, we do not need to worry about M2's lifetime.
                // But we still need to use Elixir to extend the life of
                // cellflagtmp.

#elif (AMREX_SPACEDIM == 2)
                Array4<Real> const& xip = m_edgecent[0].array(mfi);
                Array4<Real> const& yip = m_edgecent[1].array(mfi);

                if (iter == 0) {
                    if (hybrid) {
                        Gpu::streamSynchronize();
                        for (int idim = 0; idim < AMREX_SPACEDIM; ++idim) {
                            facetype[idim].prefetchToHost();
                            m_edgecent[idim][mfi].prefetchToHost();
                        }
                    }

                    //                         yes, factype[1] and then [0]
                    gshop.getIntercept({xip,yip},
                                       {facetype[1].const_array(), facetype[0].const_array()},
                                       geom, gshop_run_on, bounding_box);

                    if (hybrid) {
                        for (int idim = 0; idim < AMREX_SPACEDIM; ++idim) {
                            facetype[idim].prefetchToDevice();
                            m_edgecent[idim][mfi].prefetchToDevice();
                        }
                    }
                } else {
                    gshop.updateIntercept({xip,yip},
                                          {facetype[1].const_array(), facetype[0].const_array()},
                                          clst, geom);
                }

                nmc = build_faces(vbx, cfg, ftx, fty, clst, xip, yip, apx, apy, fcx, fcy,
                                  dx, problo, cover_multiple_cuts);

                build_cells(vbx, cfg, ftx, fty, apx, apy, vfr, ctr,
                            bar, bct, bnm, lst, small_volfrac, geom, extend_domain_face,
                            nsm, nmc);
#endif
                nsmallcells += nsm;
                nmulticuts  += nmc;
            }
        }

        ParallelAllReduce::Sum<int>({nsmallcells,nmulticuts}, ParallelContext::CommunicatorSub());
        if (nsmallcells == 0 && nmulticuts == 0) {
            break;
        } else {
            auto ls = m_mgf.getLevelSet();
            // This is an alias MulitFab, therefore FillBoundary on it is fine.
            ls.FillBoundary(geom.periodicity());
            if (amrex::Verbose() > 0) {
                if (nsmallcells) {
                    amrex::Print() << "AMReX EB: Iter. " << iter+1 << " fixed " << nsmallcells
                                   << " small cells" << std::endl;
                }
                if (nmulticuts) {
                    amrex::Print() << "AMReX EB: Iter. " << iter+1 << " fixed " << nmulticuts
                                   << " multicuts" << std::endl;
                }
            }
        }
    }

    AMREX_ALWAYS_ASSERT_WITH_MESSAGE(iter < maxiter, "EB: failed to fix small cells");

#ifdef AMREX_USE_OMP
#pragma omp parallel if (Gpu::notInLaunchRegion())
#endif
    for (MFIter mfi(m_mgf); mfi.isValid(); ++mfi)
    {
        auto& gfab = m_mgf[mfi];
        auto const& levelset = gfab.getLevelSet();
        Array4<Real const> const& clst = levelset.const_array();

        AMREX_D_TERM(Array4<Real> const& xip = m_edgecent[0].array(mfi);,
                     Array4<Real> const& yip = m_edgecent[1].array(mfi);,
                     Array4<Real> const& zip = m_edgecent[2].array(mfi);)
#if (AMREX_SPACEDIM == 3)
        auto const& edgetype = gfab.getEdgeType();
        Array4<Type_t const> const& xdg = edgetype[0].const_array();
        Array4<Type_t const> const& ydg = edgetype[1].const_array();
        Array4<Type_t const> const& zdg = edgetype[2].const_array();

        intercept_to_edge_centroid(xip, yip, zip, xdg, ydg, zdg, clst, dx, problo);

#elif (AMREX_SPACEDIM == 2)
        auto& facetype = gfab.getFaceType();
        Array4<Type_t const> const& ftx = facetype[0].const_array();
        Array4<Type_t const> const& fty = facetype[1].const_array();
        //                                  fty then ftx
        intercept_to_edge_centroid(xip, yip, fty, ftx, clst, dx, problo);
#endif
    }

    m_levelset = m_mgf.getLevelSet();

    m_ok = true;
}


template <typename G>
GShopLevel<G>::GShopLevel (IndexSpace const* is, int /*ilev*/, int max_grid_size, int /*ngrow*/,
                           const Geometry& geom, GShopLevel<G>& fineLevel)
    : Level(is, geom)
{
    if (fineLevel.isAllRegular()) {
        m_allregular = true;
        m_ok = true;
        return;
    }

    BL_PROFILE("EB2::GShopLevel()-coarse");

    const BoxArray& fine_grids = fineLevel.m_grids;
    const BoxArray& fine_covered_grids = fineLevel.m_covered_grids;

    const int coarse_ratio = 2;
    const int min_width = 8;
    bool coarsenable = fine_grids.coarsenable(coarse_ratio, min_width)
        && (fine_covered_grids.empty() || fine_covered_grids.coarsenable(coarse_ratio));

    m_ngrow = amrex::coarsen(fineLevel.m_ngrow,2);
    if (amrex::scale(m_ngrow,2) != fineLevel.m_ngrow) {
        m_ngrow = IntVect::TheZeroVector();
    }

    if (coarsenable)
    {
        int ierr = coarsenFromFine(fineLevel, true);
        m_ok = (ierr == 0);
    }
    else
    {
        Level fine_level_2(is, fineLevel.m_geom);
        fine_level_2.prepareForCoarsening(fineLevel, max_grid_size, amrex::scale(m_ngrow,2));
        int ierr = coarsenFromFine(fine_level_2, false);
        m_ok = (ierr == 0);
    }
}

}}

#endif
