#include "fly_by_featuresCLP.h"
#include "vtkLinearSubdivisionFilter2.h"

#include "vtkOFFReader.h"

#include <vtkSmartPointer.h>
#include <vtkSphereSource.h>
#include <vtkPoints.h>
#include <vtkIdList.h>
#include <vtkPolyData.h>
#include <vtkPointData.h>
#include <vtkLine.h>
#include <vtkVertex.h>
#include <vtkLineSource.h>
#include <vtkCellLocator.h>
#include <vtkPlatonicSolidSource.h>
#include <vtkPlaneSource.h>
#include <vtkPolyDataNormals.h>
#include <vtkPolyDataReader.h>
#include <vtkOBJReader.h>
#include <vtkPolyDataMapper.h>
#include <vtkOpenGLPolyDataMapper.h>
#include <vtkDataSetMapper.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#ifdef VTK_OPENGL_HAS_EGL
  #include <vtkEGLRenderWindow.h>
#endif
#include <vtkRenderer.h>
#include <vtkCamera.h>
#include <vtkShaderProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkWindowToImageFilter.h>
#include <vtkCurvatures.h>
#include <vtkTubeFilter.h>
#include <vtkSelectionNode.h>
#include <vtkSelection.h>
#include <vtkExtractSelection.h>
#include <vtkGeometryFilter.h>
#include <vtkCenterOfMass.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <vtkDoubleArray.h>
#include <vtkIdTypeArray.h>
#include <vtkCommand.h>
#include <vtkImageShiftScale.h>
#include <vtkAutoInit.h>

#include <vnl/vnl_vector.h>
#include <vnl/vnl_cross.h>
#include <vnl/vnl_random.h>

#include <itkVectorImage.h>
#include <itkImageFileWriter.h>
#include <itkImageRegionIterator.h>
#include <itkComposeImageFilter.h>
#include <itkVTKImageToImageFilter.h>
#include <itksys/SystemTools.hxx>
#include <itkMath.h>

#include <math.h>
#include <chrono>

VTK_MODULE_INIT(vtkRenderingOpenGL2) 
VTK_MODULE_INIT(vtkRenderingFreeType) 
VTK_MODULE_INIT(vtkInteractionStyle) 

typedef double VectorImagePixelType;
typedef itk::VectorImage<VectorImagePixelType, 2> VectorImageType;  
typedef VectorImageType::Pointer VectorImagePointerType;  
typedef itk::ImageRegionIterator<VectorImageType> VectorImageIteratorType;
typedef itk::ImageFileWriter<VectorImageType> VectorImageFileWriterType;

typedef itk::VectorImage<VectorImagePixelType, 3> VectorImageComposeType; 
typedef itk::ImageRegionIterator<VectorImageComposeType> VectorImageComposeIteratorType;
typedef itk::ImageFileWriter<VectorImageComposeType> VectorImageComposeFileWriterType; 

typedef unsigned char PixelComponentType;
typedef itk::RGBPixel<PixelComponentType> RGBPixelType;
typedef itk::Image<RGBPixelType, 2> ImageType;
typedef itk::VTKImageToImageFilter<ImageType> VTKImageToImageFilterType;
typedef itk::ImageRegionIterator<ImageType> ImageIteratorType;

typedef itk::Image<PixelComponentType, 2> ZImageType;
typedef itk::VTKImageToImageFilter<ZImageType> ZVTKImageToImageFilterType;
typedef itk::ImageRegionIterator<ZImageType> ZImageIteratorType;

using namespace std;
using namespace itksys;

class vtkTimerCallback2 : public vtkCommand
{
public:
  vtkTimerCallback2() = default;

  static vtkTimerCallback2* New()
  {
    vtkTimerCallback2* cb = new vtkTimerCallback2;
    cb->sphere_i = -1;
    return cb;
  }

  virtual void Execute(vtkObject* caller, unsigned long eventId,
                       void* vtkNotUsed(callData))
  {
    if (vtkCommand::TimerEvent == eventId)
    {
      this->sphere_i++;
    }
    
    if (this->sphere_i < this->spherePoints->GetNumberOfPoints())
    {

      vtkRenderWindowInteractor* iren = dynamic_cast<vtkRenderWindowInteractor*>(caller);
      vtkRenderWindow* renderWindow = iren->GetRenderWindow();

      double sphere_point[3];
      this->spherePoints->GetPoint(this->sphere_i, sphere_point);

      if(renderWindow->GetOffScreenRendering()){
        this->camera->SetViewUp(0, 0, -1);  
      }else{
        this->camera->SetViewUp(0, 0, 1);
      }
      
      this->camera->SetPosition(sphere_point[0], sphere_point[1], sphere_point[2]);
      this->camera->SetFocalPoint(0, 0, 0);

      this->renderer->ResetCameraClippingRange();

      renderWindow->Render();

      vtkSmartPointer<vtkWindowToImageFilter> windowFilterNormals = vtkSmartPointer<vtkWindowToImageFilter>::New();
      windowFilterNormals->SetInput(renderWindow);
      windowFilterNormals->SetInputBufferTypeToRGB(); //also record the alpha (transparency) channel
      windowFilterNormals->Update();

      vtkSmartPointer<vtkWindowToImageFilter> windowFilterZ = vtkSmartPointer<vtkWindowToImageFilter>::New();
      windowFilterZ->SetInput(renderWindow);
      windowFilterZ->SetInputBufferTypeToZBuffer(); //also record the alpha (transparency) channel
      windowFilterZ->Update();

      vtkSmartPointer<vtkImageShiftScale> scale = vtkSmartPointer<vtkImageShiftScale>::New();
      scale->SetOutputScalarTypeToUnsignedChar();
      scale->SetInputData(windowFilterZ->GetOutput());
      scale->SetShift(0);
      scale->SetScale(-255);
      scale->Update();

      VTKImageToImageFilterType::Pointer convert_image = VTKImageToImageFilterType::New();
      convert_image->SetInput(windowFilterNormals->GetOutput());
      convert_image->Update();
      ImageType::Pointer vtk_img = convert_image->GetOutput();

      ZVTKImageToImageFilterType::Pointer convert_image_z = ZVTKImageToImageFilterType::New();
      convert_image_z->SetInput(scale->GetOutput());
      convert_image_z->Update();
      ZImageType::Pointer z_img = convert_image_z->GetOutput();
      

      VectorImagePointerType out_image_feat = VectorImageType::New();
      VectorImageType::SizeType size;
      size[0] = this->planeResolution;
      size[1] = this->planeResolution;
      VectorImageType::RegionType region;
      region.SetSize(size);
      
      out_image_feat->SetRegions(region);
      out_image_feat->SetVectorLength(4);
      out_image_feat->Allocate();
      VectorImageType::PixelType out_pix(4);
      out_pix.Fill(0);
      out_image_feat->FillBuffer(out_pix);

      VectorImageIteratorType out_it = VectorImageIteratorType(out_image_feat, out_image_feat->GetLargestPossibleRegion());
      out_it.GoToBegin();

      ImageIteratorType it = ImageIteratorType(vtk_img, vtk_img->GetLargestPossibleRegion());
      it.GoToBegin();

      ZImageIteratorType zit = ZImageIteratorType(z_img, z_img->GetLargestPossibleRegion());
      zit.GoToBegin();

      while(!out_it.IsAtEnd() && !it.IsAtEnd() && !zit.IsAtEnd()){

        out_pix[0] = it.Get()[0];
        out_pix[1] = it.Get()[1];
        out_pix[2] = it.Get()[2];
        out_pix[3] = zit.Get();
        out_it.Set(out_pix);

        ++out_it;
        ++it;
        ++zit;
      }

      this->compose_v.push_back(out_image_feat);
    }
    else
    {
      vtkRenderWindowInteractor* iren = dynamic_cast<vtkRenderWindowInteractor*>(caller);
      if (this->timerId > -1)
      {
        iren->DestroyTimer(this->timerId);
        iren->TerminateApp();
      }
    }
  }

public:
  vtkRenderer* renderer;
  vtkCamera* camera = nullptr;
  int sphere_i = -1;
  vtkPoints* spherePoints = nullptr;
  int planeResolution = 0;
  vector<VectorImageType::Pointer> compose_v;
  int timerId = 0;
};

int main(int argc, char * argv[])
{
  
  PARSE_ARGS;

  int numFeatures = 4;
  bool createRegionLabels = regionLabels.compare("") != 0;

  chrono::steady_clock::time_point begin_fly_by = chrono::steady_clock::now();

  //Spherical sampling
  vtkSmartPointer<vtkPolyData> sphere;

  //Icosahedron subdivision
  if(numberOfSubdivisions > 0){
    vtkSmartPointer<vtkPlatonicSolidSource> icosahedron_source = vtkSmartPointer<vtkPlatonicSolidSource>::New();
    icosahedron_source->SetSolidTypeToIcosahedron();
    icosahedron_source->Update();

    vtkSmartPointer<vtkLinearSubdivisionFilter2> subdivision = vtkSmartPointer<vtkLinearSubdivisionFilter2>::New();
    subdivision->SetInputData(icosahedron_source->GetOutput());
    subdivision->SetNumberOfSubdivisions(numberOfSubdivisions);
    subdivision->Update();
    sphere = subdivision->GetOutput();
    cout<<"Number of fly by samples: "<<sphere->GetNumberOfPoints()<<endl;;

    vtkSmartPointer<vtkCellArray> vertices = vtkSmartPointer<vtkCellArray>::New();
    
    for(unsigned i = 0; i < sphere->GetNumberOfPoints(); i++){
      double point[3];
      sphere->GetPoints()->GetPoint(i, point);
      vnl_vector<double> v = vnl_vector<double>(point, 3);
      v = v.normalize()*sphereRadius;
      sphere->GetPoints()->SetPoint(i, v.data_block());

      vtkSmartPointer<vtkVertex> vertex = vtkSmartPointer<vtkVertex>::New();
      vertex->GetPointIds()->SetId(0, i);
      vertices->InsertNextCell(vertex);
    }
    sphere->SetVerts(vertices);  
  }else if(numberOfSpiralSamples > 0){
    //Spiral sampling
    sphere = vtkSmartPointer<vtkPolyData>::New();
    vtkSmartPointer<vtkPoints> sphere_points = vtkSmartPointer<vtkPoints>::New();
    vtkSmartPointer<vtkCellArray> lines = vtkSmartPointer<vtkCellArray>::New();
    vtkSmartPointer<vtkCellArray> vertices = vtkSmartPointer<vtkCellArray>::New();

    double c = 2.0*numberOfSpiralTurns;
    vtkIdType prevPid = -1;

    for(int i = 0; i < numberOfSpiralSamples; i++){
      double p[3];
      //angle = i * 180.0/numberOfSpiralSamples * M_PI/180.0
      double angle = i*M_PI/numberOfSpiralSamples;
      p[0] = sphereRadius * sin(angle)*cos(c*angle);
      p[1] = sphereRadius * sin(angle)*sin(c*angle);
      p[2] = sphereRadius * cos(angle);

      vtkIdType pid = sphere_points->InsertNextPoint(p);
      
      if(prevPid != -1){
        vtkSmartPointer<vtkLine> line = vtkSmartPointer<vtkLine>::New();
        line->GetPointIds()->SetId(0, prevPid);
        line->GetPointIds()->SetId(1, pid);
        lines->InsertNextCell(line);   
      }

      prevPid = pid;

      vtkSmartPointer<vtkVertex> vertex = vtkSmartPointer<vtkVertex>::New();
      vertex->GetPointIds()->SetId(0, pid);

      vertices->InsertNextCell(vertex);
    }
    
    sphere->SetVerts(vertices);
    sphere->SetLines(lines);
    sphere->SetPoints(sphere_points);

  }else{
    cerr<<"Please set subdivision or spiral to generate the samples on the sphere."<<endl;
    return EXIT_FAILURE;
  }


  //Spherical sampling finish
  
  //Read input mesh
  cout<<"Reading: "<<inputSurface<<endl;

  string extension = inputSurface.substr(inputSurface.find_last_of("."));

  vtkSmartPointer<vtkPolyData> input_mesh;

  if(extension.compare(".off") == 0 || extension.compare(".OFF") == 0){
    vtkSmartPointer<vtkOFFReader> reader = vtkSmartPointer<vtkOFFReader>::New();
    reader->SetFileName(inputSurface.c_str());
    reader->Update();
    input_mesh = reader->GetOutput();  
  }else if(extension.compare(".obj") == 0 || extension.compare(".OBJ") == 0){
    vtkSmartPointer<vtkOBJReader> reader = vtkSmartPointer<vtkOBJReader>::New();
    reader->SetFileName(inputSurface.c_str());
    reader->Update();
    input_mesh = reader->GetOutput();  
  }else{
    vtkSmartPointer<vtkPolyDataReader> reader = vtkSmartPointer<vtkPolyDataReader>::New();
    reader->SetFileName(inputSurface.c_str());
    reader->Update();
    input_mesh = reader->GetOutput();  
  }

  //Finish reading

  //Apply random rotation?

  if(randomRotation){

    vnl_random rand_gen = vnl_random();

    vnl_vector<double> rot_vector = vnl_vector<double>(3, 0);
    for(unsigned i = 0; i < rot_vector.size(); i++){
      rot_vector[i] = rand_gen.normal();
    }
    rot_vector.normalize();

    rotationVector.clear();
    for(unsigned i = 0; i < rot_vector.size(); i++){
      rotationVector.push_back(rot_vector[i]);
    }
    rotationAngle = rand_gen.drand32()*360.0;

    cout<<"Random rotation!"<<endl;
  }

  if(applyRotation || randomRotation){

    cout<<"Apply rotation: angle: "<<rotationAngle<<", vector: "<<rotationVector[0]<<","<<rotationVector[1]<<","<<rotationVector[2]<<endl;

    vtkSmartPointer<vtkTransform> transform = vtkSmartPointer<vtkTransform>::New();

    transform->RotateWXYZ(rotationAngle, rotationVector[0], rotationVector[1], rotationVector[2]);

    vtkSmartPointer<vtkTransformPolyDataFilter> transformFilter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
    transformFilter->SetTransform(transform);
    transformFilter->SetInputData(input_mesh);
    transformFilter->Update();
    input_mesh = transformFilter->GetOutput();
  }

  vector<vtkSmartPointer<vtkPolyData>> input_mesh_v;

  //If the input mesh is a fiber bundle, generate fibers around it
  //Each fiber is treated a seprate mesh

  if(fiberBundle){
    cout<<"Generating tubes around fibers..."<<endl;
    for(unsigned i_cell = 0; i_cell < input_mesh->GetNumberOfCells(); i_cell++){
      vtkSmartPointer<vtkIdTypeArray> ids = vtkSmartPointer<vtkIdTypeArray>::New();
      ids->SetNumberOfComponents(1);
      ids->InsertNextValue(i_cell);

      vtkSmartPointer<vtkSelectionNode> selectionNode = vtkSmartPointer<vtkSelectionNode>::New();
      selectionNode->SetFieldType(vtkSelectionNode::CELL);
      selectionNode->SetContentType(vtkSelectionNode::INDICES);
      selectionNode->SetSelectionList(ids);
      selectionNode->GetProperties()->Set(vtkSelectionNode::CONTAINING_CELLS(), 1);

      vtkSmartPointer<vtkSelection> selection = vtkSmartPointer<vtkSelection>::New();
      selection->AddNode(selectionNode);

      vtkSmartPointer<vtkExtractSelection> extractSelection = vtkSmartPointer<vtkExtractSelection>::New();
      extractSelection->SetInputData(0, input_mesh);
      extractSelection->SetInputData(1, selection);
      extractSelection->Update();

      vtkSmartPointer<vtkGeometryFilter> geometryFilter = vtkSmartPointer<vtkGeometryFilter>::New();
      geometryFilter->SetInputData(extractSelection->GetOutput());
      geometryFilter->Update();

      vtkSmartPointer<vtkTubeFilter> tubeFilter = vtkSmartPointer<vtkTubeFilter>::New();
      tubeFilter->SetNumberOfSides(45);
      tubeFilter->SetInputData(geometryFilter->GetOutput());
      tubeFilter->Update();
      input_mesh_v.push_back(tubeFilter->GetOutput());
    }
  }else{
    input_mesh_v.push_back(input_mesh);
  }

  for(unsigned i_mesh = 0; i_mesh < input_mesh_v.size(); i_mesh++){

    input_mesh = input_mesh_v[i_mesh];

    vnl_vector<double> mean_v = vnl_vector<double>(3, 0);

    //Center the shape starts
    if(centerOfMass){
      vtkSmartPointer<vtkCenterOfMass> centerOfMassFilter = vtkSmartPointer<vtkCenterOfMass>::New();
      centerOfMassFilter->SetInputData(input_mesh);
      centerOfMassFilter->SetUseScalarsAsWeights(false);
      centerOfMassFilter->Update();
      
      centerOfMassFilter->GetCenter(mean_v.data_block());

    }else{
      for(unsigned i = 0; i < input_mesh->GetNumberOfPoints(); i++){
        double point[3];
        input_mesh->GetPoints()->GetPoint(i, point);
        vnl_vector<double> v = vnl_vector<double>(point, 3);
        mean_v += v;
      }

      mean_v /= input_mesh->GetNumberOfPoints();
    }

    for(unsigned i = 0; i < input_mesh->GetNumberOfPoints(); i++){
      double point[3];
      input_mesh->GetPoints()->GetPoint(i, point);
      vnl_vector<double> v = vnl_vector<double>(point, 3);
      v -= mean_v;
      input_mesh->GetPoints()->SetPoint(i, v.data_block());
    }

    //Center shape finishes

    //Scaling the shape to unit sphere. This may also be a parameter if scaling for a population
    if(maxMagnitude == -1){
      maxMagnitude = 0;
      for(unsigned i = 0; i < input_mesh->GetNumberOfPoints(); i++){
        double point[3];
        input_mesh->GetPoints()->GetPoint(i, point);
        vnl_vector<double> v = vnl_vector<double>(point, 3);
        maxMagnitude = max(maxMagnitude, v.magnitude());
      }
    }

    for(unsigned i = 0; i < input_mesh->GetNumberOfPoints(); i++){
      double point[3];
      input_mesh->GetPoints()->GetPoint(i, point);
      vnl_vector<double> v = vnl_vector<double>(point, 3);
      v /= maxMagnitude;
      input_mesh->GetPoints()->SetPoint(i, v.data_block());
    }

    //Scaling finishes

    //Use an adaptive subdivision filter to further subdivide your shape. 
    //When triangles are too big, the cell locator will have trouble finding the correct triangle/intersection
    
    vector<VectorImageType::Pointer> compose_v;
    
    //Generate normals from the polydata. These are features used.
    if(curvature){
      //The curvature filter is not used as feature but may be used for visualization
      vtkSmartPointer<vtkCurvatures> curvaturesFilter = vtkSmartPointer<vtkCurvatures>::New();
      curvaturesFilter->SetInputData(input_mesh);
      curvaturesFilter->SetCurvatureTypeToMinimum();
      curvaturesFilter->SetCurvatureTypeToMaximum();
      curvaturesFilter->SetCurvatureTypeToGaussian();
      curvaturesFilter->SetCurvatureTypeToMean();
      curvaturesFilter->Update();
      input_mesh = curvaturesFilter->GetOutput();
    }

    vtkSmartPointer<vtkPolyDataNormals> normalGenerator = vtkSmartPointer<vtkPolyDataNormals>::New();
    normalGenerator->SetInputData(input_mesh);
    normalGenerator->ComputeCellNormalsOff();
    normalGenerator->ComputePointNormalsOn();
    normalGenerator->SplittingOff();
    normalGenerator->Update();

    input_mesh = normalGenerator->GetOutput();

    //Generate OBB tree to quickly locate cells from intersections

    if(useOctree){

      vtkSmartPointer<vtkCellLocator> tree = vtkSmartPointer<vtkCellLocator>::New();
      tree->SetDataSet(input_mesh);
      tree->BuildLocator();

      vtkSmartPointer<vtkRenderer> renderer;
      vtkSmartPointer<vtkRenderWindow> renderWindow;
      vtkSmartPointer<vtkRenderWindowInteractor> renderWindowInteractor;

      //Visualization stuff for screenshots
      if(visualize){
        vtkSmartPointer<vtkPolyDataMapper> inputMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        inputMapper->SetInputData(input_mesh);
        vtkSmartPointer<vtkActor> inputActor = vtkSmartPointer<vtkActor>::New();
        inputActor->SetMapper(inputMapper);

        vtkSmartPointer<vtkPolyDataMapper> sphereMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        sphereMapper->SetInputData(sphere);
        vtkSmartPointer<vtkActor> sphereActor = vtkSmartPointer<vtkActor>::New();
        sphereActor->SetMapper(sphereMapper);
        sphereActor->GetProperty()->SetRepresentationToWireframe();
        sphereActor->GetProperty()->SetColor(0.8,0,0.8);
        sphereActor->GetProperty()->SetLineWidth(10.0);

        vtkSmartPointer<vtkActor> spherePointsActor = vtkSmartPointer<vtkActor>::New();
        spherePointsActor->SetMapper(sphereMapper);
        spherePointsActor->GetProperty()->SetRepresentationToPoints();
        spherePointsActor->GetProperty()->SetColor(1.0, 0, 1.0);
        spherePointsActor->GetProperty()->SetPointSize(20);

        renderer = vtkSmartPointer<vtkRenderer>::New();
        renderWindow = vtkSmartPointer<vtkRenderWindow>::New();
        renderWindow->SetSize(1900, 1900);
        renderWindow->AddRenderer(renderer);
        renderWindowInteractor = vtkSmartPointer<vtkRenderWindowInteractor>::New();
        renderWindowInteractor->SetRenderWindow(renderWindow);

        renderer->AddActor(inputActor);

        if(visualizeTree){

          inputActor->GetProperty()->SetColor(1.0, 153/255.0, 51/255.0);

          double color_0[3] = {0.0, 1.0, 0.0};
          vnl_vector<double> v_color_0 = vnl_vector<double>(color_0, 3);

          double color_1[3] = {0.0, 0.0, 1.0};
          vnl_vector<double> v_color_1 = vnl_vector<double>(color_1, 3);

          vnl_vector<double> v_step = (v_color_1 - v_color_0)/(visualizeTreeLevel + 1.0);

          for(int level = 0; level < visualizeTreeLevel; level++){

            vnl_vector<double> v_color = v_color_0 + v_step*level;

            vtkSmartPointer<vtkPolyData> tree_poly = vtkSmartPointer<vtkPolyData>::New();
            tree->GenerateRepresentation(level, tree_poly);

            vtkSmartPointer<vtkPolyDataMapper> tree_mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
            tree_mapper->SetInputData(tree_poly);
            vtkSmartPointer<vtkActor> tree_actor = vtkSmartPointer<vtkActor>::New();
            tree_actor->SetMapper(tree_mapper);

            tree_actor->GetProperty()->SetInterpolationToFlat();
            tree_actor->GetProperty()->SetRepresentationToWireframe();
            tree_actor->GetProperty()->SetLineWidth(16.0/(level + 1.0));
            tree_actor->GetProperty()->SetColor(v_color.data_block());
            tree_actor->GetProperty()->SetOpacity(0.5);
            renderer->AddActor(tree_actor);
          }
          
        }else{
          
          renderer->AddActor(sphereActor);
          renderer->AddActor(spherePointsActor);  

          if(numberOfSpiralSamples > 0){
              
              vtkSmartPointer<vtkPlatonicSolidSource> icosahedron_source = vtkSmartPointer<vtkPlatonicSolidSource>::New();
              icosahedron_source->SetSolidTypeToIcosahedron();
              icosahedron_source->Update();

              vtkSmartPointer<vtkLinearSubdivisionFilter2> subdivision = vtkSmartPointer<vtkLinearSubdivisionFilter2>::New();
              subdivision->SetInputData(icosahedron_source->GetOutput());
              subdivision->SetNumberOfSubdivisions(10);
              subdivision->Update();
              sphere = subdivision->GetOutput();
              
              for(unsigned i = 0; i < sphere->GetNumberOfPoints(); i++){
                double point[3];
                sphere->GetPoints()->GetPoint(i, point);
                vnl_vector<double> v = vnl_vector<double>(point, 3);
                v = v.normalize()*sphereRadius;
                sphere->GetPoints()->SetPoint(i, v.data_block());
              }

              vtkSmartPointer<vtkPolyDataMapper> sphereMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
              sphereMapper->SetInputData(sphere);
              vtkSmartPointer<vtkActor> sphereActor = vtkSmartPointer<vtkActor>::New();
              sphereActor->SetMapper(sphereMapper);
              sphereActor->GetProperty()->SetRepresentationToWireframe();
              sphereActor->GetProperty()->SetColor(0.8,0,0.8);
              renderer->AddActor(sphereActor);
        
            }
        }

        renderer->SetBackground(1, 1, 1);
        renderWindow->Render();
      }

      vtkSmartPointer<vtkPoints> spherePoints = sphere->GetPoints();

      for(unsigned i = 0; i < spherePoints->GetNumberOfPoints(); i++){

        //Plane orientation. All planes are oriented using the north of the sphere. 
        //We calculate the 3 points that span the plane to capture the image
        double sphere_point[3];
        spherePoints->GetPoint(i, sphere_point);
        vnl_vector<double> sphere_point_v = vnl_vector<double>(sphere_point, 3);
        vnl_vector<double> sphere_point_delta_v = sphere_point_v - sphere_point_v*planeSpacing;
        vnl_vector<double> sphere_point_normal_v = sphere_point_v;
        sphere_point_normal_v.normalize();

        vnl_vector<double> sphere_north_v = vnl_vector<double>(3);
        sphere_north_v[0] = 0;
        sphere_north_v[1] = 0;
        sphere_north_v[2] = 1;

        vnl_vector<double> sphere_south_v = vnl_vector<double>(3);
        sphere_south_v[0] = 0;
        sphere_south_v[1] = 0;
        sphere_south_v[2] = -1;

        vnl_vector<double> plane_orient_x_v;
        vnl_vector<double> plane_orient_y_v;
        if(sphere_point_normal_v.is_equal(sphere_north_v, 1e-8)){
          plane_orient_x_v = vnl_vector<double>(3, 0);
          plane_orient_x_v[0] = 1;
          plane_orient_y_v = vnl_vector<double>(3, 0);  
          plane_orient_y_v[1] = 1;
        }else if(sphere_point_normal_v.is_equal(sphere_south_v, 1e-8)){
          plane_orient_x_v = vnl_vector<double>(3, 0);
          plane_orient_x_v[0] = -1;
          plane_orient_y_v = vnl_vector<double>(3, 0);  
          plane_orient_y_v[1] = -1;
        }else{
          plane_orient_x_v = vnl_cross_3d(sphere_point_normal_v, sphere_north_v).normalize();
          plane_orient_y_v = vnl_cross_3d(sphere_point_normal_v, plane_orient_x_v).normalize();  
        }
        

        vnl_vector<double> plane_point_origin_v = sphere_point_v - plane_orient_x_v*0.5*planeScaleFactor - plane_orient_y_v*0.5*planeScaleFactor;
        vnl_vector<double> plane_point_1_v = plane_point_origin_v + plane_orient_x_v*planeScaleFactor;
        vnl_vector<double> plane_point_2_v = plane_point_origin_v + plane_orient_y_v*planeScaleFactor;

        //Using the calculated points create the plane
        vtkSmartPointer<vtkPlaneSource> planeSource = vtkSmartPointer<vtkPlaneSource>::New();
        planeSource->SetOrigin(plane_point_origin_v.data_block());
        planeSource->SetPoint1(plane_point_1_v.data_block());
        planeSource->SetPoint2(plane_point_2_v.data_block());
        planeSource->SetResolution(planeResolution - 1, planeResolution - 1);
        planeSource->Update();
        vtkSmartPointer<vtkPolyData> planeMesh = planeSource->GetOutput();

        //Create the image that will hold the features
        VectorImagePointerType out_image_feat = VectorImageType::New();
        VectorImageType::SizeType size;
        size[0] = planeResolution;
        size[1] = planeResolution;
        VectorImageType::RegionType region;
        region.SetSize(size);
        
        out_image_feat->SetRegions(region);
        out_image_feat->SetVectorLength(numFeatures);
        out_image_feat->Allocate();
        VectorImageType::PixelType out_pix(numFeatures);
        out_pix.Fill(-2);
        out_image_feat->FillBuffer(out_pix);

        VectorImageIteratorType out_it = VectorImageIteratorType(out_image_feat, out_image_feat->GetLargestPossibleRegion());
        out_it.GoToBegin();

        VectorImagePointerType out_image_label;
        VectorImageIteratorType out_it_label; 

        //Create the label image if we are extracting labeled regions
        if(createRegionLabels){
          out_image_label = VectorImageType::New();

          out_image_label->SetRegions(region);
          out_image_label->SetVectorLength(1);
          out_image_label->Allocate();
          VectorImageType::PixelType out_pix_label(1);
          out_pix_label.Fill(0);
          out_image_label->FillBuffer(out_pix_label);

          out_it_label = VectorImageIteratorType(out_image_label, out_image_label->GetLargestPossibleRegion());
          out_it_label.GoToBegin();  
        }
        
        bool writeImage = false;

        //For each point in the plane. Intersect with mesh centered at 0 and scaled to fit unit sphere
        for(unsigned j = 0; j < planeMesh->GetNumberOfPoints(); j++){
          double point_plane[3];
          planeMesh->GetPoints()->GetPoint(j, point_plane);
          vnl_vector<double> point_plane_v = vnl_vector<double>(point_plane, 3);
          point_plane_v = point_plane_v*planeSpacing + sphere_point_delta_v;

          vnl_vector<double> point_end_v = point_plane_v - sphere_point_normal_v*sphereRadius*2.0;

          double tol = 1.e-10;
          double t;
          double x[3];
          double pcoords[3];
          int subId;
          vtkIdType cellId = -1;
          //Intersect line with OBB tree
          //x is the intersection point at the cell
          if(tree->IntersectWithLine(point_plane_v.data_block(), point_end_v.data_block(), tol, t, x, pcoords, subId, cellId)){

            writeImage = true;

            VectorImageType::PixelType out_pix = out_it.Get();

            vnl_vector<double> x_v = vnl_vector<double>(x, 3);

            vtkSmartPointer<vtkIdList> cellPointsIds = vtkSmartPointer<vtkIdList>::New();
            input_mesh->GetCellPoints(cellId, cellPointsIds);
            
            vnl_vector<double> wavg_normal_v(3, 0);  
            
            vtkIdType min_pointId = cellPointsIds->GetId(0);
            double min_distance = 999999999;

            double closestPoint[3];
            double dist2;
            double weights[3];
            //Get the weights for each point in the cell. The weights add up to 1
            input_mesh->GetCell(cellId)->EvaluatePosition(x, closestPoint, subId, pcoords, dist2, weights);

            //Weighted average of the normal
            for(unsigned npid = 0; npid < cellPointsIds->GetNumberOfIds(); npid++){

              vtkIdType pointId = cellPointsIds->GetId(npid);

              double point_mesh[3];
              input_mesh->GetPoint(pointId, point_mesh);
              vnl_vector<double> point_mesh_v(point_mesh, 3);  

              //Here we extract the property we want. For example the normals
              double* normal = input_mesh->GetPointData()->GetArray("Normals")->GetTuple(pointId);
              vnl_vector<double> normal_v(normal, 3);
              wavg_normal_v += normal_v*weights[npid];

              double distance = (point_mesh_v - x_v).magnitude();
              if(distance < min_distance){
                min_distance = distance;
                min_pointId = cellPointsIds->GetId(pointId);
              }
            }
            
            out_pix[0] = wavg_normal_v[0];
            out_pix[1] = wavg_normal_v[1];
            out_pix[2] = wavg_normal_v[2];  
            
            //Distance from plane to mesh (depth map)
            out_pix[3] = (point_plane_v - x_v).magnitude();
            out_it.Set(out_pix);

            if(createRegionLabels){
              VectorImageType::PixelType out_pix_label = out_it_label.Get();
              out_pix_label[0] = input_mesh->GetPointData()->GetArray(regionLabels.c_str())->GetTuple(min_pointId)[0] + 1;
              out_it_label.Set(out_pix_label);
            }
            
          }

          ++out_it;
          if(createRegionLabels){
            ++out_it_label;  
          }

          //More visualization stuff
          if(visualize && i == visualizeIndexStopCriteria){
            vtkSmartPointer<vtkLineSource> lineSource = vtkSmartPointer<vtkLineSource>::New();
            lineSource->SetPoint1(point_plane_v.data_block());
            lineSource->SetPoint2(point_end_v.data_block());

            vtkSmartPointer<vtkPolyDataMapper> lineMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
            lineMapper->SetInputConnection(lineSource->GetOutputPort());
            vtkSmartPointer<vtkActor> lineActor = vtkSmartPointer<vtkActor>::New();
            lineActor->SetMapper(lineMapper);
            lineActor->GetProperty()->SetLineWidth(8.0);
            lineActor->GetProperty()->SetColor(0.4, 0, 1.0);

            vtkSmartPointer<vtkPolyDataMapper> planeMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
            planeMapper->SetInputData(planeMesh);
            vtkSmartPointer<vtkActor> planeActor = vtkSmartPointer<vtkActor>::New();
            planeActor->SetMapper(planeMapper);
            planeActor->GetProperty()->SetRepresentationToWireframe();
            planeActor->GetProperty()->SetColor(0, 0.9, 1.0);
            
            renderer->AddActor(lineActor);
            renderer->AddActor(planeActor);

            renderWindowInteractor->Start();
          }
          
        }

        //If it's a composition then push the image into the compose vector
        compose_v.push_back(out_image_feat);
        
      }
    }else{
      vtkSmartPointer<vtkRenderer> renderer = vtkSmartPointer<vtkRenderer>::New();
      vtkSmartPointer<vtkRenderWindow> renderWindow;

#ifdef VTK_OPENGL_HAS_EGL
      if(visualize){
        renderWindow = vtkSmartPointer<vtkRenderWindow>::New();
      }else{
        renderWindow = vtkSmartPointer<vtkEGLRenderWindow>::New();
      }
#else
      renderWindow = vtkSmartPointer<vtkRenderWindow>::New();
#endif
      
      renderWindow->SetSize(planeResolution, planeResolution);
      renderer->SetBackground(0, 0, 0);
      renderWindow->AddRenderer(renderer);

      vtkSmartPointer<vtkOpenGLPolyDataMapper> inputMapper = vtkSmartPointer<vtkOpenGLPolyDataMapper>::New();
      inputMapper->SetInputData(input_mesh);
      vtkSmartPointer<vtkActor> inputActor = vtkSmartPointer<vtkActor>::New();
      inputActor->SetMapper(inputMapper);
      inputActor->GetProperty()->SetInterpolationToPhong();
      renderer->AddActor(inputActor);

      vtkShaderProperty* sp = inputActor->GetShaderProperty();

      sp->AddVertexShaderReplacement(
          "//VTK::Normal::Dec",  // replace the normal block
          true,                  // before the standard replacements
          "//VTK::Normal::Dec\n" // we still want the default
          "  varying vec3 myNormalMCVSOutput;\n", // but we add this
          false                                   // only do it once
      );

      sp->AddVertexShaderReplacement(
          "//VTK::Normal::Impl",                // replace the normal block
          true,                                 // before the standard replacements
          "//VTK::Normal::Impl\n"               // we still want the default
          "  myNormalMCVSOutput = normalMC;\n", // but we add this
          false                                 // only do it once
      );

      sp->AddVertexShaderReplacement(
          "//VTK::Color::Impl", // dummy replacement for testing clear method
          true, "VTK::Color::Impl\n", false);

      sp->ClearVertexShaderReplacement("//VTK::Color::Impl", true);

      sp->AddFragmentShaderReplacement(
          "//VTK::Normal::Dec",  // replace the normal block
          true,                  // before the standard replacements
          "//VTK::Normal::Dec\n" // we still want the default
          "  varying vec3 myNormalMCVSOutput;\n", // but we add this
          false                                   // only do it once
      );

      sp->AddFragmentShaderReplacement(
          "//VTK::Normal::Impl",  // replace the normal block
          true,                   // before the standard replacements
          "//VTK::Normal::Impl\n" // we still want the default calc
          "  diffuseColor = myNormalMCVSOutput*0.5f + 1.0f;\n", // but we add this
          false                                          // only do it once
      );

      vtkSmartPointer<vtkCamera> camera = renderer->GetActiveCamera();
      vtkSmartPointer<vtkPoints> spherePoints = sphere->GetPoints();
      
      // vtkSmartPointer<vtkTimerCallback2> cb = vtkSmartPointer<vtkTimerCallback2>::New();
      // cb->camera = camera;
      // cb->spherePoints = spherePoints;
      // cb->planeResolution = planeResolution;
      // cb->renderer = renderer;

      // renderWindowInteractor->AddObserver(vtkCommand::TimerEvent, cb);

      // int timerId;

      // if(visualize){
      //   timerId = renderWindowInteractor->CreateRepeatingTimer(100);  
      // }else{
      //   timerId = renderWindowInteractor->CreateRepeatingTimer(1);
      // }
      
      // cb->timerId = timerId;
      
      // renderWindowInteractor->Start();
      // compose_v = cb->compose_v;

      for(int sphere_i = 0; sphere_i < spherePoints->GetNumberOfPoints(); sphere_i++){
        double sphere_point[3];
        spherePoints->GetPoint(sphere_i, sphere_point);
        
        vnl_vector<double> sphere_point_v = vnl_vector<double>(sphere_point, 3);
        sphere_point_v = sphere_point_v.normalize();
        if(abs(sphere_point_v[2]) != 1){
          camera->SetViewUp(0, 0, -1);  
        }else if(sphere_point_v[2] == 1){
          camera->SetViewUp(1, 0, 0);
        }else if(sphere_point_v[2] == -1){
          camera->SetViewUp(-1, 0, 0);
        }
        camera->SetPosition(sphere_point[0], sphere_point[1], sphere_point[2]);
        camera->SetFocalPoint(0, 0, 0);

        renderer->ResetCameraClippingRange();

        if(visualize){
          vtkSmartPointer<vtkRenderWindowInteractor> renderWindowInteractor = vtkSmartPointer<vtkRenderWindowInteractor>::New();
          renderWindowInteractor->SetRenderWindow(renderWindow);
          renderWindowInteractor->Start();
        }

        vtkSmartPointer<vtkWindowToImageFilter> windowFilterNormals = vtkSmartPointer<vtkWindowToImageFilter>::New();
        windowFilterNormals->SetInput(renderWindow);
        windowFilterNormals->SetInputBufferTypeToRGB(); //also record the alpha (transparency) channel
        windowFilterNormals->Update();

        vtkSmartPointer<vtkWindowToImageFilter> windowFilterZ = vtkSmartPointer<vtkWindowToImageFilter>::New();
        windowFilterZ->SetInput(renderWindow);
        windowFilterZ->SetInputBufferTypeToZBuffer(); //also record the alpha (transparency) channel
        windowFilterZ->Update();

        vtkSmartPointer<vtkImageShiftScale> scale = vtkSmartPointer<vtkImageShiftScale>::New();
        scale->SetOutputScalarTypeToUnsignedChar();
        scale->SetInputData(windowFilterZ->GetOutput());
        scale->SetShift(0);
        scale->SetScale(-255);
        scale->Update();

        VTKImageToImageFilterType::Pointer convert_image = VTKImageToImageFilterType::New();
        convert_image->SetInput(windowFilterNormals->GetOutput());
        convert_image->Update();
        ImageType::Pointer vtk_img = convert_image->GetOutput();

        ZVTKImageToImageFilterType::Pointer convert_image_z = ZVTKImageToImageFilterType::New();
        convert_image_z->SetInput(scale->GetOutput());
        convert_image_z->Update();
        ZImageType::Pointer z_img = convert_image_z->GetOutput();
        

        VectorImagePointerType out_image_feat = VectorImageType::New();
        VectorImageType::SizeType size;
        size[0] = planeResolution;
        size[1] = planeResolution;
        VectorImageType::RegionType region;
        region.SetSize(size);
        
        out_image_feat->SetRegions(region);
        out_image_feat->SetVectorLength(4);
        out_image_feat->Allocate();
        VectorImageType::PixelType out_pix(4);
        out_pix.Fill(0);
        out_image_feat->FillBuffer(out_pix);

        VectorImageIteratorType out_it = VectorImageIteratorType(out_image_feat, out_image_feat->GetLargestPossibleRegion());
        out_it.GoToBegin();

        ImageIteratorType it = ImageIteratorType(vtk_img, vtk_img->GetLargestPossibleRegion());
        it.GoToBegin();

        ZImageIteratorType zit = ZImageIteratorType(z_img, z_img->GetLargestPossibleRegion());
        zit.GoToBegin();

        while(!out_it.IsAtEnd() && !it.IsAtEnd() && !zit.IsAtEnd()){

          out_pix[0] = it.Get()[0];
          out_pix[1] = it.Get()[1];
          out_pix[2] = it.Get()[2];
          out_pix[3] = zit.Get();
          out_it.Set(out_pix);

          ++out_it;
          ++it;
          ++zit;
        }

        compose_v.push_back(out_image_feat);
      }
    }

    //Process the stack of images to generate a single volume
    if(compose_v.size() > 0){

      if(flyByCompose){

        VectorImageComposeType::Pointer out_compose = VectorImageComposeType::New();
        VectorImageComposeType::SizeType size;
        size[0] = planeResolution;
        size[1] = planeResolution;
        size[2] = compose_v.size();

        VectorImageComposeType::RegionType region;
        region.SetSize(size);
        
        out_compose->SetRegions(region);
        out_compose->SetVectorLength(numFeatures);
        out_compose->Allocate();
        VectorImageType::PixelType out_pix(numFeatures);
        out_pix.Fill(-1);
        out_compose->FillBuffer(out_pix);

        double out_spacing[3] = {planeSpacing, planeSpacing, 1};
        out_compose->SetSpacing(out_spacing);

        VectorImageComposeIteratorType out_c_it = VectorImageComposeIteratorType(out_compose, out_compose->GetLargestPossibleRegion());
        out_c_it.GoToBegin();

        for(unsigned i = 0; i < compose_v.size(); i++){
          VectorImageIteratorType out_it = VectorImageIteratorType(compose_v[i], compose_v[i]->GetLargestPossibleRegion());
          out_it.GoToBegin();

          while(!out_it.IsAtEnd()){
            out_c_it.Set(out_it.Get());
            ++out_it;
            ++out_c_it;  
          }
        }

        if(fiberBundle){
          char buf[50];
          sprintf(buf, "%d", i_mesh);
          string outputFileName = outputName + "/" + string(buf) + ".nrrd";
          cout<<"Writing: "<<outputFileName<<endl;
          VectorImageComposeFileWriterType::Pointer writer = VectorImageComposeFileWriterType::New();
          writer->SetFileName(outputFileName);
          writer->SetInput(out_compose);
          writer->UseCompressionOn();
          writer->Update();
        }else{
          cout<<"Writing: "<<outputName<<endl;
          VectorImageComposeFileWriterType::Pointer writer = VectorImageComposeFileWriterType::New();
          writer->SetFileName(outputName);
          writer->SetInput(out_compose);
          writer->UseCompressionOn();
          writer->Update();
        }

      }else{

        if(!SystemTools::PathExists(outputName) || !SystemTools::FileIsDirectory(outputName)){
          SystemTools::MakeDirectory(outputName);
        }
        
        for(unsigned i = 0; i < compose_v.size(); i++){
          char buf[50];
          sprintf(buf, "%d", i);

          string outputFileName = outputName + "/" + string(buf) + ".nrrd";
          string outputFileNameLabel = outputName + "/" + string(buf) + "_label.nrrd";

          if(fiberBundle){
            char buf_fb[50];
            sprintf(buf_fb, "%d", i_mesh);
            outputFileName = outputName + "/" + string(buf_fb) + "_" + string(buf) + ".nrrd";
            outputFileNameLabel = outputName + "/" + string(buf_fb) + "_" + string(buf) + "_label.nrrd";
          }

          cout<<"Writing: "<<outputFileName<<endl;

          VectorImageFileWriterType::Pointer writer = VectorImageFileWriterType::New();
          writer->SetFileName(outputFileName);
          writer->SetInput(compose_v[i]);
          writer->UseCompressionOn();
          writer->Update();

          if(createRegionLabels){
            // cout<<"Writing: "<<outputFileNameLabel<<endl;
            // writer->SetFileName(outputFileNameLabel);
            // writer->SetInput(out_image_label);
            // writer->UseCompressionOn();
            // writer->Update();
          }
        }
      }
    }
  }

  chrono::steady_clock::time_point end_fly_by = chrono::steady_clock::now();

  cout << "FlyBy time: " << chrono::duration_cast<chrono::milliseconds>(end_fly_by - begin_fly_by).count() <<" ms" << endl;
  return EXIT_SUCCESS;
}