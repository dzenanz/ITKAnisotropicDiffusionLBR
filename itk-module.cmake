set(DOCUMENTATION "This module implements Anisotropic Diffusion, using Lattice Basis Reduction.

Documentation can be found in the Insight Journal article, 'Anisotropic
Diffusion in ITK', by Mirebeau J., Fehrenbach J., Risser L., Tobji S.

http://insight-journal.org/browse/publication/953
http://hdl.handle.net/10380/3505
")

# itk_module() defines the module dependencies in ITKAnisotropicFastMarchingLBR;
# ITKAnisotropicFastMarchingLBR depends on ITKCommon;
# The testing module in ITKExternalTemplate depends on ITKTestKernel,
# and ITKMetaIO for image IO (besides ITKAnisotropicDiffusionLBR itself)

itk_module(AnisotropicDiffusionLBR
  DEPENDS
    ITKCommon
    ITKIOImageBase
    ITKIOSpatialObjects
    ITKMetaIO
    ITKImageGradient
  TEST_DEPENDS
    ITKTestKernel
  EXCLUDE_FROM_DEFAULT
  DESCRIPTION
    "${DOCUMENTATION}"
)
