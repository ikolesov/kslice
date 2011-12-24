#include "itkImage.h"
#include "itkImageFileReader.h"
#include "itkImageFileWriter.h"

#include "itkResampleImageFilter.h"
#include "itkRecursiveGaussianImageFilter.h"



// Software Guide : BeginLatex
//
// The resampling filter will need a Transform in order to map point
// coordinates and will need an interpolator in order to compute intensity
// values for the new resampled image. In this particular case we use the
// \doxygen{IdentityTransform} because the image is going to be resampled by
// preserving the physical extent of the sampled region. The Linear
// interpolator is used as a common trade-off, although arguably we should use
// one type of interpolator for the in-plane subsampling process and another
// one for the inter-slice supersampling, but again, one should wonder why to
// enter into technical sophistication here, when what we are doing is to
// cover-up for an improper acquisition of medical data, and we are just trying
// to make it look as if it was correctly acquired.
//
// Software Guide : EndLatex 


// Software Guide : BeginCodeSnippet
#include "itkIdentityTransform.h"
#include "itkLinearInterpolateImageFunction.h"
// Software Guide : EndCodeSnippet




// Software Guide : BeginLatex
//
// Note that, as part of the preprocessing of the image, in this example we are
// also rescaling the range of intensities. This operation has already been
// described as Intensity Windowing. In a real clinical application, this step
// requires careful consideration of the range of intensities that contain
// information about the anatomical structures that are of interest for the
// current clinical application. It practice you may want to remove this step
// of intensity rescaling.
//
// Software Guide : EndLatex 

// Software Guide : BeginCodeSnippet
#include "itkIntensityWindowingImageFilter.h"
// Software Guide : EndCodeSnippet


int main( int argc, char * argv[] )
{
  if( argc < 5 )
    {
    std::cerr << "Usage: " << std::endl;
    std::cerr << argv[0] << "  inputImageFile  outputImageFile  lower upper " << std::endl; 
    return EXIT_FAILURE;
    }

// Software Guide : BeginLatex
//
// We made explicit now our choices for the pixel type and dimension of the
// input image to be processed, as well as the pixel type that we intend to use
// for the internal computation during the smoothing and resampling.
//
// Software Guide : EndLatex 


// Software Guide : BeginCodeSnippet
  const     unsigned int    Dimension = 3;

  typedef   unsigned short  InputPixelType;
  typedef   float           InternalPixelType;

  typedef itk::Image< InputPixelType,    Dimension >   InputImageType;
  typedef itk::Image< InternalPixelType, Dimension >   InternalImageType;
// Software Guide : EndCodeSnippet



  typedef itk::ImageFileReader< InputImageType  >  ReaderType;

  ReaderType::Pointer reader = ReaderType::New();

  reader->SetFileName( argv[1] );

  try 
    {
    reader->Update();
    }
  catch( itk::ExceptionObject & excep )
    {
    std::cerr << "Exception caught!" << std::endl;
    std::cerr << excep << std::endl;
    }

  typedef itk::IntensityWindowingImageFilter< 
                                  InputImageType, 
                                  InternalImageType >  IntensityFilterType;

  IntensityFilterType::Pointer intensityWindowing = IntensityFilterType::New();

  intensityWindowing->SetWindowMinimum( atoi( argv[3] ) );
  intensityWindowing->SetWindowMaximum( atoi( argv[4] ) );

  intensityWindowing->SetOutputMinimum(   0.0 );
  intensityWindowing->SetOutputMaximum( 255.0 * 255.0 ); // floats but in the range of shorts

  intensityWindowing->SetInput( reader->GetOutput() );


  
// Software Guide : BeginLatex
//
// We instantiate the smoothing filter that will be used on the preprocessing
// for subsampling the in-plane resolution of the dataset. 
//
// Software Guide : EndLatex 

// Software Guide : BeginCodeSnippet
  typedef itk::RecursiveGaussianImageFilter< 
                                InternalImageType,
                                InternalImageType > GaussianFilterType;
// Software Guide : EndCodeSnippet




// Software Guide : BeginLatex
//
// We create two instances of the smoothing filter, one will smooth along the
// $X$ direction while the other will smooth along the $Y$ direction. They are
// connected in a cascade in the pipeline, while taking their input from the
// intensity windowing filter. Note that you may want to skip the intensity
// windowing scale and simply take the input directly from the reader.
//
// Software Guide : EndLatex 

// Software Guide : BeginCodeSnippet
  GaussianFilterType::Pointer smootherX = GaussianFilterType::New();
  GaussianFilterType::Pointer smootherY = GaussianFilterType::New();

  smootherX->SetInput( intensityWindowing->GetOutput() );
  smootherY->SetInput( smootherX->GetOutput() );
// Software Guide : EndCodeSnippet


// Software Guide : BeginLatex
//
// We must now provide the settings for the resampling itself. This is done by
// searching for a value of isotropic resolution that will provide a trade-off
// between the evil of subsampling and the evil of supersampling. We advance
// here the conjecture that the geometrical mean between the in-plane and the
// inter-slice resolutions should be a convenient isotropic resolution to use.
// This conjecture is supported on nothing else than intuition and common
// sense. You can rightfully argue that this choice deserves a more technical
// consideration, but then, if you are so inclined to the technical correctness
// of the image sampling process, you should not be using this code, and should
// rather we talking about such technical correctness to the radiologist who
// acquired this ugly anisotropic dataset.
//
// We take the image from the input and then request its array of pixel spacing
// values.
//
// Software Guide : EndLatex 

// Software Guide : BeginCodeSnippet
  InputImageType::ConstPointer inputImage = reader->GetOutput();

  const InputImageType::SpacingType& inputSpacing = inputImage->GetSpacing();
// Software Guide : EndCodeSnippet




// Software Guide : BeginLatex
//
// and apply our ad-hoc conjecture that the correct anisotropic resolution
// to use is the geometrical mean of the in-plane and inter-slice resolutions.
// Then set this spacing as the Sigma value to be used for the Gaussian
// smoothing at the preprocessing stage.
//
// Software Guide : EndLatex 

// Software Guide : BeginCodeSnippet
  const double isoSpacing = inputSpacing[0] ;
  
  smootherX->SetSigma( isoSpacing * 0.1);
  smootherY->SetSigma( isoSpacing * 0.1);
// Software Guide : EndCodeSnippet




// Software Guide : BeginLatex
//
// We instruct the smoothing filters to act along the $X$ and $Y$ direction
// respectively. And define the settings for avoiding the loss of intensity as
// a result of the diffusion process that is inherited from the use of a
// Gaussian filter.
//
// \index{RecursiveGaussianImageFilter!SetNormalizeAcrossScale}
// Software Guide : EndLatex 

 
// Software Guide : BeginCodeSnippet
  smootherX->SetDirection( 0 );
  smootherY->SetDirection( 1 );

  smootherX->SetNormalizeAcrossScale( true );
  smootherY->SetNormalizeAcrossScale( true );
// Software Guide : EndCodeSnippet


// Software Guide : BeginLatex
//
// Now that we have taken care of the smoothing in-plane, we proceed to
// instantiate the resampling filter that will reconstruct an isotropic image.
// We start by declaring the pixel type to be use at the output of such filter,
// then instantiate the image type and the type for the resampling filter.
// Finally we construct an instantiation of such a filter.
//
// Software Guide : EndLatex 


// Software Guide : BeginCodeSnippet
  typedef   unsigned short   OutputPixelType;

  typedef itk::Image< OutputPixelType,   Dimension >   OutputImageType;

  typedef itk::ResampleImageFilter<
                InternalImageType, OutputImageType >  ResampleFilterType;

  ResampleFilterType::Pointer resampler = ResampleFilterType::New();
// Software Guide : EndCodeSnippet




// Software Guide : BeginLatex
//
// The resampling filter requires that we provide a Transform, that in this
// particular case can simply be an identity transform.
//
// Software Guide : EndLatex 

// Software Guide : BeginCodeSnippet
  typedef itk::IdentityTransform< double, Dimension >  TransformType;

  TransformType::Pointer transform = TransformType::New();
  transform->SetIdentity();

  resampler->SetTransform( transform );
// Software Guide : EndCodeSnippet




// Software Guide : BeginLatex
//
// The filter also requires an interpolator to be passed to it. In this case we
// chose to use a linear interpolator.
//
// Software Guide : EndLatex 

// Software Guide : BeginCodeSnippet
  typedef itk::LinearInterpolateImageFunction< 
                          InternalImageType, double >  InterpolatorType;

  InterpolatorType::Pointer interpolator = InterpolatorType::New();

  resampler->SetInterpolator( interpolator );
// Software Guide : EndCodeSnippet




  resampler->SetDefaultPixelValue( 0 ); // highlight regions without source




// Software Guide : BeginLatex
//
// The pixel spacing of the resampled dataset is loaded in a \code{SpacingType}
// and passed to the resampling filter.
//
// Software Guide : EndLatex 

// Software Guide : BeginCodeSnippet
  OutputImageType::SpacingType spacing;

  spacing[0] = isoSpacing*2;
  spacing[1] = isoSpacing*2;
  spacing[2] = isoSpacing*2;

  resampler->SetOutputSpacing( spacing );
// Software Guide : EndCodeSnippet


// Software Guide : BeginLatex
//
// The origin and orientation of the output image is maintained, since we
// decided to resample the image in the same physical extent of the input
// anisotropic image.
//
// Software Guide : EndLatex 
  // Software Guide : BeginCodeSnippet
    InputImageType::SizeType   inputSize =
                      inputImage->GetLargestPossibleRegion().GetSize();

    typedef InputImageType::SizeType::SizeValueType SizeValueType;

    const double dx = inputSize[0]/2 ;
    const double dy = inputSize[1]/2;
    const double dz = inputSize[0]/2;

// Software Guide : BeginCodeSnippet
  double pc[3];
  pc[0] = -(dx-1)/2.0;
  pc[1] = -(dy-1)/2.0;
  pc[2] = -(dz-1)/2.0;
  resampler->SetOutputOrigin( inputImage->GetOrigin() );
  resampler->SetOutputDirection( inputImage->GetDirection() );
// Software Guide : EndCodeSnippet




// Software Guide : BeginLatex
//
// The number of pixels to use along each dimension in the grid of the
// resampled image is computed using the ratio between the pixel spacings of the
// input image and those of the output image. Note that the computation of the
// number of pixels along the $Z$ direction is slightly different with the
// purpose of making sure that we don't attempt to compute pixels that are
// outside of the original anisotropic dataset.
//
// Software Guide : EndLatex 



// Software Guide : EndCodeSnippet



// Software Guide : BeginLatex
//
// Finally the values are stored in a \code{SizeType} and passed to the
// resampling filter. Note that this process requires a casting since the
// computation are performed in \code{double}, while the elements of the
// \code{SizeType} are integers.
//
// Software Guide : EndLatex 

// Software Guide : BeginCodeSnippet
  InputImageType::SizeType   size;

  size[0] = static_cast<SizeValueType>( dx );
  size[1] = static_cast<SizeValueType>( dy );
  size[2] = static_cast<SizeValueType>( dz );

  resampler->SetSize( size );
// Software Guide : EndCodeSnippet




// Software Guide : BeginLatex
//
// Our last action is to take the input for the resampling image filter from
// the output of the cascade of smoothing filters, and then to trigger the
// execution of the pipeline by invoking the \code{Update()} method on the
// resampling filter.
//
// Software Guide : EndLatex 


// Software Guide : BeginCodeSnippet
  resampler->SetInput( smootherY->GetOutput() );

  resampler->Update();
// Software Guide : EndCodeSnippet
  

// Software Guide : BeginLatex
//
// At this point we should take some minutes in silence to reflect on the
// circumstances that have lead us to accept to cover-up for the improper
// acquisition of medical data.
//
// Software Guide : EndLatex 




  
  typedef itk::ImageFileWriter< OutputImageType >  WriterType;

  WriterType::Pointer writer = WriterType::New();

  writer->SetFileName( argv[2] );
  writer->SetInput( resampler->GetOutput() );

  try 
    {
    writer->Update();
    }
  catch( itk::ExceptionObject & excep )
    {
    std::cerr << "Exception caught !" << std::endl;
    std::cerr << excep << std::endl;
    }


  return EXIT_SUCCESS;
}

