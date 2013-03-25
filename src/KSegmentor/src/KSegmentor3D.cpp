#include "KSegmentor3D.h"
#include "llist.h"
#include "sfm_local_chanvese_mex.h"
#include "interactive_kurvolver.h"
#include "vtkImageData.h"
#include "KSandbox.h"
#include <omp.h>
#include <string>
#include <sstream>
//#include <opencv2/highgui/highgui.hpp>
#include "vtkImageGaussianSmooth.h"
#include <cstring>
#include <cstdlib>
#include <ctime>
//#include <boost/shared_ptr.hpp>

using std::string;
//using cv::Mat;


KSegmentor3D* KSegmentor3D::CreateSegmentor(vtkImageData *image, 
                                            vtkImageData *label, 
                                            bool contInit)
{
    int currSlice=0;
    int numIts = 20;
    float distWeight=0.01;
    int brushRad = 5;
    KSegmentor3D* seg3d = 
          new KSegmentor3D(image,label,contInit,currSlice,numIts,distWeight,brushRad);
    return seg3d; 
}

KSegmentor3D::KSegmentor3D(vtkImageData* image, vtkImageData* label,
                           bool contInit, int currSlice, int numIts,
                           float distWeight, int brushRad) {
    m_EnergyName = GetSupportedEnergyNames()[1];
    this->InitializeVariables(image,label, contInit, currSlice, 
                                       numIts, distWeight, brushRad);

    if(contInit) {
        std::cout<<"Initializing user input using label data"<<std::endl;
        this->initializeUserInputImageWithContour();
    }
    this->initializeData();
    this->CreateLLs(this->LL3D);
    LL* Lztmp = this->LL3D.Lz; assert(Lztmp != NULL);
    this->intializeLevelSet3D();
}



void KSegmentor3D::accumulateCurrentUserInput( double value,const unsigned int element,
                                                            double weight /*=1.0 default */)
{
  double Umax  = 1.0;
  Umax         = this->GetUmax(); assert(Umax>0);
  double Ustep = weight * (Umax)/2.0;
  if( fabs(Ustep) < 0.01 ) {
    /*cout <<"whoa something is F'd, check Umax " << endl;*/ assert(1); }

  double user_input      = -Ustep * ( value > 0.5 ) +
                            Ustep * ( value <= 0.5 );

  this->ptrU_t_Image[element] = user_input;

  if( ptrIntegral_Image[element] < -Umax ) {
    ptrIntegral_Image[element] = -Umax;
  } else if( ptrIntegral_Image[element] > Umax ) {
    ptrIntegral_Image[element] = Umax;
  }

  this->OnUserPaintsLabel(); // Ivan: consider OnUserPaintsLabel the "on label changed" entry point
}

void KSegmentor3D::integrateUserInput()
{
  ptrIntegral_Image = static_cast<double*>(this->U_Integral_image->GetScalarPointer());
  ptrU_t_Image      = static_cast<double*>(this->U_t_image->GetScalarPointer());

  int pos=0;
  int Nelements=this->m_UpdateVector.size();
  cout << " Integrating:  KSegmentor3D::integrateUserInput(), N= "
       << Nelements << endl;
  for (int element=0;element<Nelements;element++)
  {
    pos=this->m_UpdateVector[element];
    this->ptrIntegral_Image[pos] += this->ptrU_t_Image[pos];
    this->ptrU_t_Image[pos]       = 0;
  }
}


void KSegmentor3D::UpdateArraysAfterTransform()
{
    this->mdims=U_t_image->GetDimensions();

    //Set dimensions
    dimz = (int)mdims[2];
    dimy = (int)mdims[1];
    dimx = (int)mdims[0];

    dims[2] = dimz;
    dims[1] = dimy;
    dims[0] = dimx;

    dims[3] = dims[0]*dims[1];
    dims[4] = dims[0]*dims[1]*dims[2];

    this->U_Integral_image->GetSpacing( m_Spacing_mm );

    cout <<  "; updated KSegmentor3D with dims[0,1,2] = "
            << dimx << "," << dimy << "," << dimz << endl;

    this->rad = 3.0 / std::max( m_Spacing_mm[0],m_Spacing_mm[1] ); // about 3mm in physical units
    this->rad = std::min(7.0,this->rad); // force non-huge radius if the spacing is retarded
    this->rad = std::max(3.0, this->rad); // force non-tiny radius if the spacing is retarded

    this->initializeData();

    this->CreateLLs(LL3D);

    this->intializeLevelSet3D();
}


void KSegmentor3D::initializeData()
{
    currSlice=0;
    prevSlice=-1; //this will never be true
    imageVol->GetScalarRange( imgRange );
    labelVol->GetScalarRange( labelRange );
    if( abs(labelRange[1]) < 1e-3 )
    { // empty label; so set the proper range
        labelRange[1] = KViewerOptions::getDefaultDrawLabelMaxVal();
    }
    //assert( 0 != imgRange[1] ); // what the, all black ?? impossible !

    this->imgRange=imgRange;
    ptrCurrImage = static_cast< short*>(imageVol->GetScalarPointer());
    ptrCurrLabel = static_cast< short*>(labelVol->GetScalarPointer());


    long elemNum=0;
    for (int k=0; k<=dimx-1; k++) {
        for (int j=0; j<=dimy-1; j++)  {
            for (int i=0; i<=dimz-1; i++,elemNum++) {
                this->mask[elemNum]=(double) ( 0 < ptrCurrLabel[elemNum] );
                this->img[elemNum] = (double) ptrCurrImage[elemNum];
            }
        }
    }
}


void KSegmentor3D::OnUserPaintsLabel() {
  this->prevSlice = -1;
}

namespace {
  std::vector<double> cache_phi(0);
}

void KSegmentor3D::Update2D(bool reInitFromMask)
{
    this->integrateUserInput();
    this->CreateLLs(LL2D);

    const char *imgType=imageVol->GetScalarTypeAsString(); //IKDebug

    std::cout<<sizeof(unsigned short*)<<std::endl;
    std::cout<<sizeof(short*)<<std::endl;

    ptrCurrImage        = static_cast< short*>(imageVol->GetScalarPointer());
    ptrCurrLabel        = static_cast< short*>(labelVol->GetScalarPointer());
    ptrIntegral_Image = static_cast<double*>(this->U_Integral_image->GetScalarPointer());

    size_t sz = mdims[0]*mdims[1];
    if( cache_phi.size() != sz )
      cache_phi.resize(sz);

    double* imgSlice          = new double[  mdims[0]*mdims[1] ];
    double* maskSlice       = new double[ mdims[0]*mdims[1] ];
    double* U_I_slice        = new double[ mdims[0]*mdims[1] ];
    double* phiSlice          = new double[ mdims[0]*mdims[1] ];
    double* labelSlice       = new double[ mdims[0]*mdims[1] ];

    unsigned int element3D;
    long elemNum=0;
    for (int j=0; j<=mdims[1]-1; j++)  {
        for (int i=0; i<=mdims[0]-1; i++) {
            // indexing definition:  ptr[k*mdims[1]*mdims[0] +j*mdims[0]+i];
            element3D=this->currSlice*mdims[1]*mdims[0] +j*mdims[0]+i;
            imgSlice[elemNum]        = (double) ptrCurrImage[element3D];
            maskSlice[elemNum]       = (double) ( 0 < ptrCurrLabel[element3D] );

            U_I_slice[elemNum] =this->ptrIntegral_Image[element3D];
            elemNum++;
        }
    }

    std::vector<long> dimsSlice(5);
    dimsSlice[0] = mdims[0];
    dimsSlice[1] = mdims[1];
    dimsSlice[2] = 1;
    dimsSlice[3] = dimsSlice[0]*dimsSlice[1];
    dimsSlice[4] = dimsSlice[0]*dimsSlice[1]*dimsSlice[2];


//    this->saveMatToPNG(imgSlice,"img.png");
//    this->saveMatToPNG(maskSlice,"lab.png");
//
//    std::ofstream imgFile("img.txt",std::ios_base::out);
//    for(int i=0;i< mdims[0]*mdims[1]; i++){
//        imgFile<<imgSlice[i]<<' ';
//    }
//    imgFile.close();
//
//    std::ofstream labFile("lab.txt",std::ios_base::out);
//    for(int i=0;i< mdims[0]*mdims[1]; i++){
//        labFile<<maskSlice[i]<<' ';
//    }
//    labFile.close();
//
//    std::cout<<"Writing test images"<<std::endl;

    ls_mask2phi3c(maskSlice,phiSlice,labelSlice,&(dimsSlice[0]),
                  LL2D.Lz,LL2D.Ln1,LL2D.Ln2,LL2D.Lp1,LL2D.Lp2);


    cout << "prevslice=" << prevSlice << ", " << "currslice= " << currSlice << endl;
    if( (prevSlice == this->currSlice) && !reInitFromMask ) {
      cout <<  "\033[01;32m\033]"
           << "using cached phi " << "\033[00m\033]" << endl;
      std::memcpy( &(phiSlice[0]),&(cache_phi[0]),sizeof(double)*mdims[0]*mdims[1] );
    } else {
      cout <<  "\033[01;42m\033]"
           << "first time on slice! " << "\033[00m\033]" << endl;
    }
    prevSlice=currSlice;



    interactive_rbchanvese(imgSlice,phiSlice,U_I_slice,labelSlice,&(dimsSlice[0]),
                           LL2D.Lz,LL2D.Ln1,LL2D.Lp1,LL2D.Ln2,LL2D.Lp2,LL2D.Lin2out,LL2D.Lout2in,
                           iter,rad,lambda*0.5,display);


    //threshold phi to find segmentation label, assign it to appropriate range of label!
    elemNum=0;
    for (int j=0; j<=mdims[1]-1; j++)  {
        for (int i=0; i<=mdims[0]-1; i++) {
            double phi_val = phiSlice[elemNum];
            double phi_out = (-phi_val + 3.0) / 6.0; // shift and scale from [-3,3] to [0,1]

            element3D=this->currSlice*mdims[1]*mdims[0] +j*mdims[0]+i;
//            ptrCurrLabel[element3D]= (unsigned short) ( ( (phi_out > 0.95)
//                                                          + (phi_out > 0.8) + (phi_out > 0.65)
//                                                          + (phi_out > 0.5) ) * labelRange[1] / 4.0 );

            ptrCurrLabel[element3D]=   ( (unsigned short) 0 >= phi_val )*labelRange[1];

            elemNum++;
        }
    }
    cout <<  "Lz size: "       << LL2D.Lz->length << endl;

    // store cached \phi level set func
    std::memcpy( &(cache_phi[0]),&(phiSlice[0]),sizeof(double)*sz );

    delete imgSlice;
    delete labelSlice;
    delete maskSlice;
    delete phiSlice;
    delete U_I_slice;

    m_UpdateVector.clear();
    m_CoordinatesVector.clear();


}


void KSegmentor3D::Update3D()
{
    bool bSmoothU = false;
    if( bSmoothU ) {
        cout << "smoothing integral image 3D " << endl;
        vtkSmartPointer<vtkImageGaussianSmooth> imageSmoother = vtkSmartPointer<vtkImageGaussianSmooth>::New();
        imageSmoother->SetDimensionality(3);
        imageSmoother->SetStandardDeviations(1.0,1.0,1.0);
        imageSmoother->SetInput(this->U_Integral_image);
        imageSmoother->Update();
        this->U_Integral_image->DeepCopy(imageSmoother->GetOutput());
    }


    cout << "integrating mask 3D " << endl;
    this->integrateUserInput();

    // The bug is as follows: the level-set evolution modifies Lchanged indices,
    // but afterwards they are not appearing in the list of modified coordinates!
    // Thus, the next time around the mask doesn't get to update them!

    cout << "updating mask 3D " << endl;
    this->UpdateMask(true);

    this->CreateLLs(LL3D); // TODO: fix the caching so that only uniques are kept

    // The bug is as follows: the level-set evolution modifies Lchanged indices,
    // but afterwards they are not appearing in the list of modified coordinates!
    // Thus, the next time around the mask doesn't get to update them!

    ll_init(LL3D.Lchanged);

    //        int Nelements=this->m_UpdateVector.size();
    //        for (int element=0;element<Nelements;element++)
    //        {
    //            unsigned int idx=this->m_UpdateVector[element];
    //            int found       = std::count( idx_used.begin(), idx_used.end(), idx );
    //            if( 0 < found ) {
    //                ll_pushnew(Lchanged,m_CoordinatesVector[element][0],m_CoordinatesVector[element][1],
    //                                                           m_CoordinatesVector[element][2],idx);
    //                idx_used.insert(idx);
    //            }
    //        }
    cout << "m_UpdateVector Size BEGIN: " << m_UpdateVector.size()
            <<  ", Lchanged size BEGIN: " << LL3D.Lchanged->length
            <<  ", Lz size BEGIN: "       << LL3D.Lz->length << endl;

    ptrCurrImage = static_cast< short*>(imageVol->GetScalarPointer());
    ptrCurrLabel = static_cast< short*>(labelVol->GetScalarPointer());
    ptrIntegral_Image = static_cast<double*>(this->U_Integral_image->GetScalarPointer());
    double u_in, u_out;
    // Crap, the linked lists will accumulate zillions of repeating entries ...
    // Must flush them and keep unique entries, or something (?)
    //if(this->m_UpdateVector.size()!=0)
    //        ls_mask2phi3c_update(this->m_UpdateVector,
    //                            this->m_CoordinatesVector,mask,phi,label,dims,
    //                            LL3D.Lz,LL3D.Ln1,LL3D.Ln2,LL3D.Lp1,LL3D.Lp2,LL3D.Lchanged);

    ls_mask2phi3c(mask,phi,label,dims,LL3D.Lz,LL3D.Ln1,LL3D.Ln2,LL3D.Lp1,LL3D.Lp2);
    //   ls_mask2phi3c_ext(mask,phi,label,dims,LL3D.Lz,LL3D.Ln1,LL3D.Ln2,LL3D.Lp1,LL3D.Lp2,LL3D.Lchanged);
    cout << "m_UpdateVector Size: " << m_UpdateVector.size()
            <<" m_CoordinatesVector Size: "<<this->m_CoordinatesVector.size()
            <<  ", Lchanged size: " << LL3D.Lchanged->length
            <<  ", Lz size: "       << LL3D.Lz->length << endl;

    if( 0 == m_EnergyName.compare("ChanVese") ) {
        interactive_chanvese_ext(img,phi,ptrIntegral_Image,label,dims,
                                 LL3D.Lz,LL3D.Ln1,LL3D.Lp1,LL3D.Ln2,LL3D.Lp2,LL3D.Lin2out,LL3D.Lout2in,LL3D.Lchanged,
                                 iter,0.5*lambda,display,this->m_PlaneNormalVector,
                                 this->m_PlaneCenter,this->m_DistWeight);
        bool bDisplayChanVeseCost = false;
        if( bDisplayChanVeseCost ) {
            double cv_cost = this->evalChanVeseCost(u_in,u_out);
            cout << "chan vese cost = " << cv_cost << endl;
        }
    }
    else if( 0 == m_EnergyName.compare("LocalCV") )
        interactive_rbchanvese_ext(img,phi,ptrIntegral_Image,label,dims,
                                   LL3D.Lz,LL3D.Ln1,LL3D.Lp1,LL3D.Ln2,LL3D.Lp2,LL3D.Lin2out,LL3D.Lout2in,LL3D.Lchanged,
                                   iter,rad,lambda,display,this->m_PlaneNormalVector,
                                   this->m_PlaneCenter,this->m_DistWeight);
    else
        cout << "Error, unsupported energy name! " << m_EnergyName << endl;


    this->evalChanVeseCost(u_in,u_out);
    double cutoff_thresh = (u_in - u_out)*this->m_ThreshWeight;
    cout << "uin, uout, cutoff=" << u_in << ", " << u_out << ", " << cutoff_thresh << endl;

    long elemNum=0;
    double mult=labelRange[1] / 4.0;
    elemNum=0;
    int x,y,z,x0,y0,z0,idx;

    double phi_val = 0;
    double phi_out = 0;
    double outputVal=0;

    // Caution: Lchanged can contain duplicate entries !!!
    ll_init(LL3D.Lchanged);
    // while(Lchanged->curr != NULL)
    double changeInLabel = 0;
    int Nelements = this->dimx * this->dimy * this->dimz;
    for (int idx=0;idx<Nelements;idx++)
    {

        // traverse through all modified Lz *and their neighbors!*
        // neighbors are where phi_out is something between 0 and 1
        //          x0 = Lchanged->curr->x;
        //          y0 = Lchanged->curr->y;
        //          z0 = Lchanged->curr->z;
        //          for (int dx = -2; dx < 2; dx ++ ) {
        //              x = x0 + dx;
        //              for (int dy = -2; dy < 2; dy ++ ) {
        //                  y = y0 + dy;
        //                  for (int dz = -2; dz < 2; dz ++ ) {
        //                      z = z0 + dz;
        //                      if( 0 < ( (x<0)+(y<0)+(z<0)+(x>=mdims[0])+(y>=mdims[1])+(z>=mdims[2]) ) )
        //                      { // the neighbor is out of bounds !
        //                          continue;
        //                      }
        //    idx = (z)*mdims[1]*mdims[0] +(y)*mdims[0]+(x);
        phi_val = phi[idx];
        if( (u_out < u_in) && ( img[idx] < (u_out+2*cutoff_thresh) ) )
        { // force it to be out
            phi_val = 3.0;
        }
        else if( (u_out < u_in) && ( img[idx] < (u_out+cutoff_thresh) ) )
        { // force it to be out
            phi_val = std::min( phi_val, 0.0 );
        }
        phi_out = (-phi_val + 3.0) / 6.0;
        outputVal=  (unsigned short) ( ( (phi_out > 0.95) +
                                         (phi_out > 0.8) +
                                         (phi_out > 0.65) +
                                         (phi_out > 0.5) )
                                       *mult);
        changeInLabel += fabs( (outputVal - ptrCurrLabel[idx]) > 1e-3 );
        ptrCurrLabel[idx] =outputVal;
        mask[idx]         =(double) ( 0 < ptrCurrLabel[idx] ); // d'oh, *update the mask!*

        //                  }
        //              }
        //          }
        //          ll_step(Lchanged);
    }
    cout << "m_UpdateVector Size: " << m_UpdateVector.size()
            <<  ", Lchanged size: " << LL3D.Lchanged->length
            <<  ", |change| in label: " << changeInLabel << endl;

    double spc[3];
    this->U_Integral_image->GetSpacing(spc);

    m_UpdateVector.clear();
    m_CoordinatesVector.clear();
}


KSegmentor3D::~KSegmentor3D(){

    //delete [] this->mdims;//Causes trouble! Haveto find out why!!
    delete [] this->img;
    delete [] this->mask;
    delete [] this->imgRange;
    delete [] this->labelRange;
    delete [] this->phi;
    delete [] this->label;



    LL *Lz, *Ln1, *Ln2, *Lp1, *Lp2;
    LL *Lin2out, *Lout2in,*Lchanged;


    Lz=LL3D.Lz;
    Ln1=LL3D.Ln1;
    Ln2=LL3D.Ln2;
    Lp1=LL3D.Lp1;
    Lp2=LL3D.Lp2;
    Lin2out=LL3D.Lin2out;
    Lout2in=LL3D.Lout2in;
    Lchanged=LL3D.Lchanged;

    ll_destroy(Lz);
    ll_destroy(Ln1);
    ll_destroy(Ln2);
    ll_destroy(Lp1);
    ll_destroy(Lp2);
    ll_destroy(Lin2out);
    ll_destroy(Lout2in);
    ll_destroy(Lchanged);

    Lz=LL2D.Lz;
    Ln1=LL2D.Ln1;
    Ln2=LL2D.Ln2;
    Lp1=LL2D.Lp1;
    Lp2=LL2D.Lp2;
    Lin2out=LL2D.Lin2out;
    Lout2in=LL2D.Lout2in;
    Lchanged=LL2D.Lchanged;

    ll_destroy(Lz);
    ll_destroy(Ln1);
    ll_destroy(Ln2);
    ll_destroy(Lp1);
    ll_destroy(Lp2);
    ll_destroy(Lin2out);
    ll_destroy(Lout2in);
    ll_destroy(Lchanged);
}



