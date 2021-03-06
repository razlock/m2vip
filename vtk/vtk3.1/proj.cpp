#include <iostream>
#include <fstream>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkDataSetReader.h>
#include <vtkDataSetMapper.h>
#include <vtkActor.h>
#include <vtkLookupTable.h>
#include <vtkRectilinearGrid.h>
#include <vtkFloatArray.h>
#include <vtkCutter.h>
#include <vtkPlane.h>
#include <vtkPointData.h>
#include <vtkGraphicsFactory.h>
#include <vtkImagingFactory.h>
#include <vtkPNGWriter.h>
#include <vtkImageData.h>
#include <vtkCamera.h>
#include <vtkContourFilter.h>


int gridSize = 512;
int winSize = 768;

const char *prefix = "";

const char *location = "../../vtk3/sn_resamp512";

int passNum = 0;

using std::cerr;
using std::cout;
using std::endl;

// Function prototypes
vtkRectilinearGrid *ReadGrid(int zStart, int zEnd);
void WriteImage(const char *name, const float *rgba, int width, int height);
bool ComposeImageZbuffer(float *rgba_out, float *zbuffer,   int image_width, int image_height);


int main(int argc, char *argv[])
{
    int numPasses = 32;

    // Set up the pipeline.
    vtkContourFilter *cf = vtkContourFilter::New();
    cf->SetNumberOfContours(1);
    cf->SetValue(0, 5.0);

    vtkDataSetMapper *mapper = vtkDataSetMapper::New();
    mapper->SetInput(cf->GetOutput());

    vtkLookupTable *lut = vtkLookupTable::New();
    mapper->SetLookupTable(lut);
    mapper->SetScalarRange(0, numPasses);
    lut->Build();

    vtkActor *actor = vtkActor::New();
    actor->SetMapper(mapper);

    vtkRenderer *ren = vtkRenderer::New();
    ren->AddActor(actor);

    vtkCamera *cam = ren->GetActiveCamera();
    cam->SetFocalPoint(0.5, 0.5, 0.5);
    cam->SetPosition(1.5, 1.5, 1.5);

    vtkRenderWindow *renwin = vtkRenderWindow::New();
    
    // THIS DOESN'T WORK WITHOUT MESA
    renwin->OffScreenRenderingOn();
    renwin->SetSize(winSize, winSize);
    renwin->AddRenderer(ren);
    
    float *rebuilt = new float[4*winSize*winSize];
    float *zbuffertmp = new float[winSize*winSize];
    
    for(passNum=0; passNum < numPasses; ++passNum)
    {        
        printf("pass %d\n", passNum);
        
        // Read the data.
        int zStart = passNum * (511/numPasses);
        int zEnd = (passNum+1) * (511/numPasses);
        
        vtkRectilinearGrid *rg = ReadGrid(zStart, zEnd);
        cf->SetInput(rg);
        rg->Delete();

        // Force an update and set the parallel rank as the active scalars.
        cf->Update();
        cf->GetOutput()->GetPointData()->SetActiveScalars("pass_num");

        renwin->Render();

        float *rgba = renwin->GetRGBAPixelData(0, 0, winSize-1, winSize-1, 1);
        float *zbuffer = renwin->GetZbufferData(0, 0, winSize-1, winSize-1);
        
        if(passNum > 0)
        {
            for(int i=0; i<winSize*winSize; ++i)
            {
                if(zbuffertmp[i] == zbuffer[i])
                    memcpy(&rebuilt[i * 4], &rebuilt[i * 4], 4 * sizeof(float));
                else if(zbuffertmp[i] < zbuffer[i])
                    memcpy(&rebuilt[i * 4], &rebuilt[i * 4], 4 * sizeof(float));
                else
                {
                    memcpy(&rebuilt[i * 4], &rgba[i * 4], 4 * sizeof(float));
                    zbuffertmp[i] = zbuffer[i];
                }
            }
        }
        else
        {
            memcpy(rebuilt, rgba, 4*winSize*winSize*sizeof(float));
            memcpy(zbuffertmp, zbuffer, winSize*winSize*sizeof(float));
        }
        
        //memcpy(zbuffertmp, zbuffer, winSize*winSize*sizeof(float));
        
        char filename[32];
        sprintf(filename, "rebuilt[%d]", passNum);
        WriteImage(filename, rebuilt, winSize, winSize);
    }
    
    // Image 2
    /*zStart = 280;
    zEnd = 511;
    
    rg = ReadGrid(zStart, zEnd);
    cf->SetInput(rg);
    rg->Delete();

    // Force an update and set the parallel rank as the active scalars.
    cf->Update();
    cf->GetOutput()->GetPointData()->SetActiveScalars("pass_num");

    renwin->Render();

    float *rgba2 = renwin->GetRGBAPixelData(0, 0, winSize-1, winSize-1, 1);
    float *zbuffer2 = renwin->GetZbufferData(0, 0, winSize-1, winSize-1);

    WriteImage("image2.png", rgba2, winSize, winSize);
    
    float *zrgba2 = new float[4*winSize*winSize];
    
    didComposite = ComposeImageZbuffer(zrgba2, zbuffer2,  winSize, winSize);
 
    WriteImage("image2Z.png", zrgba2, winSize, winSize);*/
    
    // Recomposition
    /*float *rebuilt = new float[4*winSize*winSize];
    
    for(int y=0; y<winSize; ++y)
    {
        for(int x=0; x<winSize; ++x)
        {
            if(zbuffer[y * winSize + x] == zbuffer2[y * winSize + x])
                memcpy(&rebuilt[y * winSize * 4 + x * 4], &rgba2[y * winSize * 4 + x * 4], 4 * sizeof(float));
            else if(zbuffer[y * winSize + x] < zbuffer2[y * winSize + x])
                memcpy(&rebuilt[y * winSize * 4 + x * 4], &rgba[y * winSize * 4 + x * 4], 4 * sizeof(float));
            else
                memcpy(&rebuilt[y * winSize * 4 + x * 4], &rgba2[y * winSize * 4 + x * 4], 4 * sizeof(float));
        }
    }
    */
    printf("ok\n");
    WriteImage("final.png", rebuilt, winSize, winSize);
    
    delete[] zbuffertmp;
    delete[] rebuilt;
}

// You should not need to modify these routines.
vtkRectilinearGrid *ReadGrid(int zStart, int zEnd)
{
    int  i;

    if (zStart < 0 || zEnd < 0 || zStart >= gridSize || zEnd >= gridSize || zStart > zEnd)
    {
        cerr << prefix << "Invalid range: " << zStart << "-" << zEnd << endl;
        return NULL;
    }

    ifstream ifile(location);
    if (ifile.fail())
    {
        cerr << prefix << "Unable to open file: " << location << "!!" << endl;
    }

    cerr << prefix << "Reading from " << zStart << " to " << zEnd << endl;

    vtkRectilinearGrid *rg = vtkRectilinearGrid::New();
    vtkFloatArray *X = vtkFloatArray::New();
    X->SetNumberOfTuples(gridSize);
    for (i = 0 ; i < gridSize ; i++)
        X->SetTuple1(i, i/(gridSize-1.0));
    rg->SetXCoordinates(X);
    X->Delete();
    vtkFloatArray *Y = vtkFloatArray::New();
    Y->SetNumberOfTuples(gridSize);
    for (i = 0 ; i < gridSize ; i++)
        Y->SetTuple1(i, i/(gridSize-1.0));
    rg->SetYCoordinates(Y);
    Y->Delete();
    vtkFloatArray *Z = vtkFloatArray::New();
    int numSlicesToRead = zEnd-zStart+1;
    Z->SetNumberOfTuples(numSlicesToRead);
    for (i = zStart ; i <= zEnd ; i++)
        Z->SetTuple1(i-zStart, i/(gridSize-1.0));
    rg->SetZCoordinates(Z);
    Z->Delete();

    rg->SetDimensions(gridSize, gridSize, numSlicesToRead);

    int valuesPerSlice  = gridSize*gridSize;
    int bytesPerSlice   = 4*valuesPerSlice;
    int offset          = zStart * bytesPerSlice;
    int bytesToRead     = bytesPerSlice*numSlicesToRead;
    int valuesToRead    = valuesPerSlice*numSlicesToRead;

    vtkFloatArray *scalars = vtkFloatArray::New();
    scalars->SetNumberOfTuples(valuesToRead);
    float *arr = scalars->GetPointer(0);
    ifile.seekg(offset, ios::beg);
    ifile.read((char *)arr, bytesToRead);
    ifile.close();

    scalars->SetName("entropy");
    rg->GetPointData()->AddArray(scalars);
    scalars->Delete();

    vtkFloatArray *pr = vtkFloatArray::New();
    pr->SetNumberOfTuples(valuesToRead);
    for (int i = 0 ; i < valuesToRead ; i++)
        pr->SetTuple1(i, passNum);

    pr->SetName("pass_num");
    rg->GetPointData()->AddArray(pr);
    pr->Delete();

    rg->GetPointData()->SetActiveScalars("entropy");
    
    cerr << prefix << " Done reading" << endl;
    return rg;
}

void WriteImage(const char *name, const float *rgba, int width, int height)
{
    vtkImageData *img = vtkImageData::New();
    img->SetDimensions(width, height, 1);
    img->SetNumberOfScalarComponents(3);
    img->SetScalarTypeToUnsignedChar();
    //cout<< (unsigned int ) (255*rgba[4]);

    for (int i = 0 ; i < width ; i++)
    {
        for (int j = 0 ; j < height ; j++)
        {
             unsigned char *ptr = (unsigned char *) img->GetScalarPointer(i, j, 0);
             int idx = j*width + i;
             ptr[0] = (unsigned char) (255*rgba[4*idx + 0]);
             ptr[1] = (unsigned char) (255*rgba[4*idx + 1]);
             ptr[2] = (unsigned char) (255*rgba[4*idx + 2]);
            //  cout<< (rgba[4*idx + 0])<<" "<<  (rgba[4*idx + 1]) <<" "<<  (unsigned int ) (rgba[4*idx + 2])<<"-"; 
        }
    }

    cout << "ecriture" << " " << endl; 

    vtkPNGWriter *writer = vtkPNGWriter::New();
    writer->SetInput(img);
    writer->SetFileName(name);
    writer->Write();

    img->Delete();
    writer->Delete();
}

bool ComposeImageZbuffer(float *rgba_out, float *zbuffer,   int image_width, int image_height)
{
    int npixels = image_width*image_height;

    float min=1;
    float max=0;
    
    for (int i = 0 ; i < npixels ; i++)
    {
        if ((zbuffer[i]<min) && (zbuffer[i] != 0)) min=zbuffer[i];
        if ((zbuffer[i]>max) && (zbuffer[i] != 1)) max=zbuffer[i];
    }
    
    cout << "min:" << min << ", ";
    cout << "max:" << max << "  ";

    float coef = 1.0 / ((max-min));

    cout << "coef:" << coef << endl;

    for (int i = 0 ; i < npixels ; i++)
    {
        rgba_out[i*4] = rgba_out[i*4+1] = rgba_out[i*4+2] =(zbuffer[i]==1.0?0:coef*(zbuffer[i]-min));
        rgba_out[i*4+3] = 0.0;
    }

    return false;
}
