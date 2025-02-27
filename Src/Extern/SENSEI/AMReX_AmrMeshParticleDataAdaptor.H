#ifndef AMReX_AmrMeshParticleDataAdaptor_h
#define AMReX_AmrMeshParticleDataAdaptor_h

// AMReX includes
#include <AMReX_Config.H>
#ifdef AMREX_PARTICLES

#include <AMReX_Particles.H>

#include <AMReX_AmrMesh.H>
#include <AMReX_MultiFab.H>
#include <AMReX_AmrMeshDataAdaptor.H>
#include <AMReX_ParticleDataAdaptor.H>
// sensei includes
#include "DataAdaptor.h"

namespace amrex
{
template<int NStructReal, int NStructInt, int NArrayReal, int NArrayInt>
class AmrMeshParticleDataAdaptor : public sensei::DataAdaptor
{
public:
  static AmrMeshParticleDataAdaptor* New();
  senseiTypeMacro(AmrMeshParticleDataAdaptor, sensei::DataAdaptor);

  int SetDataSource(
    AmrMesh *mesh,
    const std::vector<amrex::Vector<amrex::MultiFab>*> &mesh_states,
    const std::vector<std::vector<std::string>> &mesh_names,
    amrex::ParticleContainer<NStructReal, NStructInt, NArrayReal, NArrayInt> * particles,
    const std::map<std::string, std::vector<int>> & rStructs = {},
    const std::map<std::string, int> & iStructs = {},
    const std::map<std::string, std::vector<int>> & rArrays = {},
    const std::map<std::string, int> & iArrays = {}
  );

  // SENSEI API
#if SENSEI_VERSION_MAJOR >= 3
  int GetMeshMetadata(unsigned int id, sensei::MeshMetadataPtr &metadata) override;
#else
  int GetMeshName(unsigned int id, std::string &meshName) override;
  int GetMeshHasGhostNodes(const std::string &meshName, int &nLayers) override;
  int GetMeshHasGhostCells(const std::string &meshName, int &nLayers) override;
  int GetNumberOfArrays(const std::string &meshName, int association, unsigned int &numberOfArrays) override;
  int GetArrayName(const std::string &meshName, int association, unsigned int index, std::string &arrayName) override;
#endif
  int GetNumberOfMeshes(unsigned int &numMeshes) override;
  int GetMesh(const std::string &meshName, bool structureOnly, vtkDataObject *&mesh) override;
  int AddGhostNodesArray(vtkDataObject* mesh, const std::string &meshName) override;
  int AddGhostCellsArray(vtkDataObject* mesh, const std::string &meshName) override;
  int AddArray(vtkDataObject* mesh, const std::string &meshName, int association, const std::string &arrayName) override;
  int ReleaseData() override;

protected:
  AmrMeshParticleDataAdaptor()
  {
    m_meshAdaptor = AmrMeshDataAdaptor::New();
    m_particleAdaptor = ParticleDataAdaptor<NStructReal, NStructInt, NArrayReal, NArrayInt>::New();
  }

  ~AmrMeshParticleDataAdaptor()
  {
    m_meshAdaptor->Delete();
    m_particleAdaptor->Delete();
  }

private:
  ParticleDataAdaptor<NStructReal, NStructInt, NArrayReal, NArrayInt>* m_particleAdaptor;
  AmrMeshDataAdaptor* m_meshAdaptor;

  const std::string m_meshName = "mesh";
  const std::string m_particlesName = "particles";
};
}

#include "AMReX_AmrMeshParticleDataAdaptorI.H"
#endif // AMREX_PARTICLES
#endif // AMReX_AmrMeshParticleDataAdaptor_h
