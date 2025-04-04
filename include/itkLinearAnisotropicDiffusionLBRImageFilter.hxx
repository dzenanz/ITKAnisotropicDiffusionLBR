/*=========================================================================
 *
 *  Copyright NumFOCUS
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *         https://www.apache.org/licenses/LICENSE-2.0.txt
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *=========================================================================*/

//
//  Created by Jean-Marie Mirebeau on 28/02/2014.
//
//

#ifndef itkLinearAnisotropicDiffusionLBRImageFilter_hxx
#define itkLinearAnisotropicDiffusionLBRImageFilter_hxx

#include "itkUnaryFunctorImageFilter.h"
#include "itkImageRegionIterator.h"
#include "itkMinimumMaximumImageCalculator.h"
#include "itkCastImageFilter.h"
#include "itkExtractImageFilter.h"
#include "itkUnaryFunctorWithIndexImageFilter.h"
#include "itkTernaryFunctorImageFilter.h"

namespace itk
{

template <typename TImage, typename TScalar>
LinearAnisotropicDiffusionLBRImageFilter<TImage, TScalar>::LinearAnisotropicDiffusionLBRImageFilter()
  : m_DiffusionTime(1)
  , m_RatioToMaxStableTimeStep(0.7)
  ,

  m_EffectiveDiffusionTime(0)

{
  this->SetNumberOfRequiredInputs(2);
}


template <typename TImage, typename TScalar>
void
LinearAnisotropicDiffusionLBRImageFilter<TImage, TScalar>::SetInputImage(const ImageType * image)
{
  this->SetNthInput(0, const_cast<ImageType *>(image));
}


template <typename TImage, typename TScalar>
void
LinearAnisotropicDiffusionLBRImageFilter<TImage, TScalar>::SetInputTensor(const TensorImageType * tensorImage)
{
  this->SetNthInput(1, const_cast<TensorImageType *>(tensorImage));
}


template <typename TImage, typename TScalar>
typename TImage::ConstPointer
LinearAnisotropicDiffusionLBRImageFilter<TImage, TScalar>::GetInputImage()
{
  return static_cast<const ImageType *>(this->ProcessObject::GetInput(0));
}


template <typename TImage, typename TScalar>
typename LinearAnisotropicDiffusionLBRImageFilter<TImage, TScalar>::TensorImageType::ConstPointer
LinearAnisotropicDiffusionLBRImageFilter<TImage, TScalar>::GetInputTensor()
{
  return static_cast<const TensorImageType *>(this->ProcessObject::GetInput(1));
}


template <typename TImage, typename TScalar>
void
LinearAnisotropicDiffusionLBRImageFilter<TImage, TScalar>::GenerateData()
{
  GenerateStencils();
  this->UpdateProgress(0.5);

  this->ImageUpdateLoop();
}

// **************************** Computation ***********************
template <typename TImage, typename TScalar>
struct LinearAnisotropicDiffusionLBRImageFilter<TImage, TScalar>::StencilFunctor
{
public:
  using SpacingType = typename TensorImageType::SpacingType;
  void
  Initialize(RegionType region_, SpacingType spacing)
  {
    region = region_;
    prod[0] = 1;
    for (ImageDimensionType i = 1; i < ImageDimension; ++i)
    {
      prod[i] = prod[i - 1] * region.GetSize()[i - 1];
    }
    for (ImageDimensionType i = 0; i < ImageDimension; ++i)
    {
      invSpacing[i] = ScalarType(1) / spacing[i];
    }
  }

  InternalSizeT
  BufferIndex(const IndexType & x) const
  {
    IndexValueType ans = 0;
    for (ImageDimensionType i = 0; i < ImageDimension; ++i)
    {
      ans += this->prod[i] * (x[i] - this->region.GetIndex()[i]);
    }
    return ans;
  }

  StencilType
  operator()(const TensorType & tensor, const IndexType & x) const
  {
    StencilType        stencil;
    StencilOffsetsType offsets;

    // Diffusion tensors are homogeneous to the inverse of norms, and are thus rescaled with an inverse spacing.

    TensorType D;
    for (ImageDimensionType i = 0; i < ImageDimension; ++i)
      for (ImageDimensionType j = i; j < ImageDimension; ++j)
        D(i, j) = tensor(i, j) * this->invSpacing[i] * this->invSpacing[j];
    this->Stencil(Dispatch<ImageDimension>(), D, offsets, stencil.second);

    InternalSizeT * yIndex = &stencil.first[0];

    // Compute buffer offsets from geometrical offsets
    for (unsigned int i = 0; i < HalfStencilSize; ++i)
    {
      for (auto orientation = 0; orientation < 2; ++orientation, ++yIndex)
      {
        const IndexType y = orientation ? x - offsets[i] : x + offsets[i];
        if (this->region.IsInside(y))
        {
          *yIndex = this->BufferIndex(y);
        }
        else
        {
          // Neumann boundary conditions.
          *yIndex = this->OutsideBufferIndex();
        } // if y
      } // for eps
    } // for i
    return stencil;
  }

protected:
  struct DispatchBase
  {};
  template <unsigned int>
  struct Dispatch : public DispatchBase
  {};

  static void
  Stencil(const Dispatch<2> &,
          const TensorType &        D,
          StencilOffsetsType &      offsets,
          StencilCoefficientsType & coefficients)
  {
    // Construct a superbase, and make it obtuse with Selling's algorithm
    VectorType sb[ImageDimension + 1]; // SuperBase
    for (ImageDimensionType i = 0; i < ImageDimension; ++i)
    {
      for (ImageDimensionType j = 0; j < ImageDimension; ++j)
      {
        sb[i][j] = (i == j);
      }
    }

    sb[ImageDimension] = -(sb[0] + sb[1]);
    constexpr int maxIter = 200;
    int           iter = 0;
    for (; iter < maxIter; ++iter)
    {
      bool same = true;
      for (ImageDimensionType i = 1; i <= ImageDimension && same; ++i)
      {
        for (ImageDimensionType j = 0; j < i && same; ++j)
        {
          if (ScalarProduct(D, sb[i], sb[j]) > 0)
          {
            const VectorType u = sb[i], v = sb[j];
            sb[0] = v - u;
            sb[1] = u;
            sb[2] = -v;
            same = false;
          }
        }
      }
      if (same)
      {
        break;
      }
    }
    if (iter == maxIter)
    {
      std::cerr << "Warning: Selling's algorithm not stabilized." << std::endl;
    }

    for (ImageDimensionType i = 0; i < 3; ++i)
    {
      coefficients[i] = (-0.5) * ScalarProduct(D, sb[(i + 1) % 3], sb[(i + 2) % 3]);
      assert(coefficients[i] >= 0);

      offsets[i][0] = static_cast<OffsetValueType>(-sb[i][1]);
      offsets[i][1] = static_cast<OffsetValueType>(sb[i][0]);
    }
  }
  static void
  Stencil(const Dispatch<3> &,
          const TensorType &        D,
          StencilOffsetsType &      offsets,
          StencilCoefficientsType & coefficients)
  {
    // Construct a superbase, and make it obtuse with Selling's algorithm
    VectorType sb[ImageDimension + 1];
    for (ImageDimensionType i = 0; i < ImageDimension; ++i)
    {
      for (ImageDimensionType j = 0; j < ImageDimension; ++j)
      {
        sb[i][j] = (i == j);
      }
    }
    sb[ImageDimension] = -(sb[0] + sb[1] + sb[2]);

    constexpr int maxIter = 200;
    int           iter = 0;
    for (; iter < maxIter; ++iter)
    {
      bool same = true;
      for (ImageDimensionType i = 1; i <= ImageDimension && same; ++i)
        for (ImageDimensionType j = 0; j < i && same; ++j)
          if (ScalarProduct(D, sb[i], sb[j]) > 0)
          {
            const VectorType u = sb[i], v = sb[j];
            for (ImageDimensionType k = 0, l = 0; k <= ImageDimension; ++k)
              if (k != i && k != j)
                sb[l++] = sb[k] + u;
            sb[2] = -u;
            sb[3] = v;
            same = false;
          }
      if (same)
        break;
    }
    if (iter == maxIter)
    {
      std::cerr << "Warning: Selling's algorithm not stabilized." << std::endl;
    }

    // Computation of the weights
    SymmetricSecondRankTensor<ScalarType, ImageDimension + 1> Weights;
    for (ImageDimensionType i = 1; i < ImageDimension + 1; ++i)
    {
      for (ImageDimensionType j = 0; j < i; ++j)
      {
        Weights(i, j) = (-0.5) * ScalarProduct(D, sb[i], sb[j]);
      }
    }

    // Now that the obtuse superbasis has been created, generate the stencil.
    // First get the dual basis. Obtained by computing the comatrix of Basis[1..ImageDimension].

    for (ImageDimensionType i = 0; i < ImageDimension; ++i)
    {
      for (ImageDimensionType j = 0; j < ImageDimension; ++j)
      {
        offsets[i][j] = sb[(i + 1) % ImageDimension][(j + 1) % ImageDimension] *
                          sb[(i + 2) % ImageDimension][(j + 2) % ImageDimension] -
                        sb[(i + 2) % ImageDimension][(j + 1) % ImageDimension] *
                          sb[(i + 1) % ImageDimension][(j + 2) % ImageDimension];
      }
    }

    offsets[ImageDimension] = offsets[0] - offsets[1];
    offsets[ImageDimension + 1] = offsets[0] - offsets[2];
    offsets[ImageDimension + 2] = offsets[1] - offsets[2];

    // The corresponding coefficients are given by the scalar products.
    for (ImageDimensionType i = 0; i < ImageDimension; ++i)
    {
      coefficients[i] = Weights(i, 3);
    }

    coefficients[ImageDimension] = Weights(0, 1);
    coefficients[ImageDimension + 1] = Weights(0, 2);
    coefficients[ImageDimension + 2] = Weights(1, 2);
  }

  RegionType  region;
  IndexType   prod;
  SpacingType invSpacing;
  InternalSizeT
  OutsideBufferIndex() const
  {
    return NumericTraits<InternalSizeT>::max();
  }
};


template <typename TImage, typename TScalar>
void
LinearAnisotropicDiffusionLBRImageFilter<TImage, TScalar>::GenerateStencils()
{
  // Stencil type is a pair type because itk::UnaryFunctorImage filter
  // only produces one output
  //        using SpacingType = typename TensorImageType::SpacingType;
  const RegionType region = GetRequestedRegion();

  using FunctorFilterType = UnaryFunctorWithIndexImageFilter<TensorImageType, StencilImageType, StencilFunctor>;
  typename FunctorFilterType::Pointer filter = FunctorFilterType::New();
  filter->SetInput(GetInputTensor());
  filter->GetFunctor().Initialize(region, GetInputTensor()->GetSpacing());
  filter->Update();
  m_StencilImage = filter->GetOutput();


  // setup diagonal coefficients. Cannot be parallelized due to non-local modifications of diagBuffer.

  m_DiagonalCoefficients = ScalarImageType::New();
  m_DiagonalCoefficients->CopyInformation(GetInputTensor());
  m_DiagonalCoefficients->SetRegions(GetRequestedRegion());
  m_DiagonalCoefficients->Allocate();
  m_DiagonalCoefficients->FillBuffer(ScalarType(0));

  ImageRegionConstIterator<StencilImageType> stencilIt(m_StencilImage, region);
  ImageRegionIterator<ScalarImageType>       diagIt(m_DiagonalCoefficients, region);
  ScalarType *                               diagBuffer = m_DiagonalCoefficients->GetBufferPointer();

  for (stencilIt.GoToBegin(), diagIt.GoToBegin(); !stencilIt.IsAtEnd(); ++stencilIt, ++diagIt)
  {
    for (unsigned int i = 0; i < StencilSize; ++i)
    {
      const InternalSizeT yIndex = stencilIt.Value().first[i];
      if (yIndex != OutsideBufferIndex())
      {
        const ScalarType coefficient = stencilIt.Value().second[i / 2];
        diagIt.Value() += coefficient;
        diagBuffer[yIndex] += coefficient;
      } // if y
    } // for i
  } // for stencilIt, diagIt
}


template <typename TImage, typename TScalar>
typename LinearAnisotropicDiffusionLBRImageFilter<TImage, TScalar>::ScalarType
LinearAnisotropicDiffusionLBRImageFilter<TImage, TScalar>::MaxStableTimeStep()
{
  using MaxCalculatorType = MinimumMaximumImageCalculator<ScalarImageType>;
  typename MaxCalculatorType::Pointer maximumCalculator = MaxCalculatorType::New();
  maximumCalculator->SetImage(m_DiagonalCoefficients);
  maximumCalculator->SetRegion(GetRequestedRegion());
  maximumCalculator->ComputeMaximum();
  return 1. / maximumCalculator->GetMaximum();
}


template <typename TImage, typename TScalar>
void
LinearAnisotropicDiffusionLBRImageFilter<TImage, TScalar>::SetMaxDiffusionTime(ScalarType time)
{
  if (time < 0)
  {
    itkExceptionMacro("diffusion time must be finite and positive");
  }
  m_DiffusionTime = time;
}


template <typename TImage, typename TScalar>
void
LinearAnisotropicDiffusionLBRImageFilter<TImage, TScalar>::SetRatioToMaxStableTimeStep(ScalarType ratio)
{
  if (ratio <= 0 || ratio > 1)
  {
    itkExceptionMacro("Ratio to max time step " << ratio << "should be within ]0,1]");
  }
  m_RatioToMaxStableTimeStep = ratio;
}


template <typename TImage, typename TScalar>
void
LinearAnisotropicDiffusionLBRImageFilter<TImage, TScalar>::SetMaxNumberOfTimeSteps(int n)
{
  if (n <= 0)
  {
    itkExceptionMacro("Max number of time steps must be positive");
  }
  m_MaxNumberOfTimeSteps = n;
}


template <typename TImage, typename TScalar>
void
LinearAnisotropicDiffusionLBRImageFilter<TImage, TScalar>::ImageUpdateLoop()
{
  ScalarType delta = MaxStableTimeStep() * m_RatioToMaxStableTimeStep;
  int        n = ceil(m_DiffusionTime / delta);
  if (n > m_MaxNumberOfTimeSteps)
  {
    n = m_MaxNumberOfTimeSteps;
    m_EffectiveDiffusionTime = n * delta;
  }
  else
  {
    delta = m_DiffusionTime / n;
    m_EffectiveDiffusionTime = m_DiffusionTime;
  }
  m_EffectiveNumberOfTimeSteps = n;

  // Extraction of the region of interest is required for image buffer access.
  using InputCasterType = ExtractImageFilter<ImageType, ImageType>;
  typename InputCasterType::Pointer inputCaster = InputCasterType::New();
  inputCaster->SetInput(GetInputImage());
  inputCaster->SetExtractionRegion(GetRequestedRegion());
  inputCaster->SetDirectionCollapseToIdentity();
  inputCaster->Update();
  m_PreviousImage = inputCaster->GetOutput();

  m_NextImage = ImageType::New();
  m_NextImage->CopyInformation(m_PreviousImage);
  m_NextImage->SetRegions(m_PreviousImage->GetBufferedRegion());
  m_NextImage->Allocate();

  for (auto k = 0; k < n; ++k)
  {
    ImageUpdate(delta);
    std::swap(m_PreviousImage, m_NextImage);
    this->UpdateProgress(0.5 + 0.5 * k / float(n));
  }
  this->GraftOutput(m_PreviousImage);
}


template <typename TImage, typename TScalar>
struct LinearAnisotropicDiffusionLBRImageFilter<TImage, TScalar>::FunctorType
{
  ScalarType delta;
  PixelType
  operator()(PixelType output, PixelType input, ScalarType diag)
  {
    return output * this->delta + input * (ScalarType(1) - this->delta * diag);
  }
};


template <typename TImage, typename TScalar>
void
LinearAnisotropicDiffusionLBRImageFilter<TImage, TScalar>::ImageUpdate(ScalarType delta)
{
  // Setting up iterators
  ImageRegion<ImageDimension> region = GetRequestedRegion();

  ImageRegionConstIterator<ImageType> inputIt(m_PreviousImage, region);
  ImageRegionIterator<ImageType>      outputIt(m_NextImage, region);

  const PixelType * inputBuffer = m_PreviousImage->GetBufferPointer();
  PixelType *       outputBuffer = m_NextImage->GetBufferPointer();

  ImageRegionConstIterator<ScalarImageType>  diagIt(m_DiagonalCoefficients, region);
  ImageRegionConstIterator<StencilImageType> stencilIt(m_StencilImage, region);

  // Rest of function is a hand-made (sparse matrix)*vector product.
  m_NextImage->FillBuffer({});

  // Taking care of Off-Diagonal matrix elements. Cannot be parallelized due to non-local modifications of outputBuffer
  for (inputIt.GoToBegin(), outputIt.GoToBegin(), stencilIt.GoToBegin(); !inputIt.IsAtEnd();
       ++inputIt, ++outputIt, ++stencilIt)
  {
    for (unsigned int i = 0; i < StencilSize; ++i)
    {
      const InternalSizeT yIndex = stencilIt.Value().first[i];
      if (yIndex != OutsideBufferIndex())
      {
        const ScalarType coefficient = stencilIt.Value().second[i / 2];
        outputIt.Value() += coefficient * (inputBuffer[yIndex]);
        outputBuffer[yIndex] += coefficient * inputIt.Value();
      }
    }
  }

  using ImageFunctorType = TernaryFunctorImageFilter<ImageType, ImageType, ScalarImageType, ImageType, FunctorType>;
  typename ImageFunctorType::Pointer imageFunctor = ImageFunctorType::New();
  imageFunctor->SetInput1(m_NextImage);
  imageFunctor->SetInput2(m_PreviousImage);
  imageFunctor->SetInput3(m_DiagonalCoefficients);
  imageFunctor->GetFunctor().delta = delta;

  assert(imageFunctor->CanRunInPlace());
  imageFunctor->InPlaceOn();
  imageFunctor->Update();
  m_NextImage = imageFunctor->GetOutput();

  /*
  // Old Serial version for diagonal elements
  for(inputIt.GoToBegin(), outputIt.GoToBegin(), diagIt.GoToBegin();
      !inputIt.IsAtEnd();
      ++inputIt, ++outputIt, ++diagIt)
      outputIt.Value() = delta*outputIt.Value() + (1-delta*diagIt.Value())*inputIt.Value();
  */
}

// **************************** subclass SSRT_Traits call method **************************

template <typename TImage, typename TScalar>
TScalar
LinearAnisotropicDiffusionLBRImageFilter<TImage, TScalar>::ScalarProduct(const TensorType & m,
                                                                         const VectorType & u,
                                                                         const VectorType & v)
{
  ScalarType result(0);
  for (ImageDimensionType i = 0; i < ImageDimension; ++i)
  {
    result += m(i, i) * u[i] * v[i];
  }
  for (ImageDimensionType i = 0; i < ImageDimension; ++i)
  {
    for (ImageDimensionType j = i + 1; j < ImageDimension; ++j)
    {
      result += m(i, j) * (u[i] * v[j] + u[j] * v[i]);
    }
  }
  return result;
}

} // end namespace itk

#endif
