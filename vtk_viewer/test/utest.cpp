/*
 * Copyright (c) 2016, Southwest Research Institute
 * All rights reserved.
 *
 */

#include <vtk_viewer/vtk_utils.h>
#include <vtk_viewer/vtk_viewer.h>
#include <vtkPointData.h>
#include <gtest/gtest.h>

// This test shows the results of meshing on a square grid that has a sinusoidal
// variability in the z axis.  Red arrows show the surface normal for each triangle
// in the mesh, and cyan boxes show the points used to seed the mesh creation algorithm

TEST(ViewerTest, TestCase1)
{
  vtkSmartPointer<vtkPoints> points = vtk_viewer::createPlane();

  vtkSmartPointer<vtkPolyData> data = vtkSmartPointer<vtkPolyData>::New();
  data = vtk_viewer::createMesh(points, 0.5, 5);

  vtk_viewer::generateNormals(data);

  vtkSmartPointer<vtkPoints> points2 = vtkSmartPointer<vtkPoints>::New();
  double pt1[3] = {3.0, 3.0, 0.0};
  double pt2[3] = {4.0, 2.0, 0.0};
  double pt3[3] = {5.0, 3.0, 0.0};
  double pt4[3] = {4.0, 4.0, 0.0};

  points2->InsertNextPoint(pt1);
  points2->InsertNextPoint(pt2);
  points2->InsertNextPoint(pt3);
  points2->InsertNextPoint(pt4);
  
  vtkSmartPointer<vtkPolyData> cut = vtkSmartPointer<vtkPolyData>::New();
  cut = vtk_viewer::cutMesh(data, points2, false);

  cout << "cutter points: " << cut->GetPoints()->GetNumberOfPoints() << "\n";
  cout << "cutter lines: " << cut->GetNumberOfLines() << "\n";



  vtk_viewer::VTKViewer viz;
  std::vector<float> color(3);

  // Display mesh results
  color[0] = 0.2;
  color[1] = 0.9;
  color[2] = 0.9;

  viz.addPointDataDisplay(points, color);

  // Display mesh results
  color[0] = 0.2;
  color[1] = 0.2;
  color[2] = 0.9;
  //viz.addPolyDataDisplay(data, color);

  color[0] = 0.1;
  color[1] = 0.9;
  color[2] = 0.1;
  //viz.addCellNormalDisplay(data, color, 1.0);

  viz.addPolyDataDisplay(cut,color);
  viz.addPointDataDisplay(cut->GetPoints(), color);

  viz.renderDisplay();

}

// Run all the tests that were declared with TEST()
int main(int argc, char **argv)
{
  //ros::init(argc, argv, "test");  // some tests need ROS framework
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
