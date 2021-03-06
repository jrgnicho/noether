/*
 * Copyright (c) 2016, Southwest Research Institute
 * All rights reserved.
 *
 */

#ifndef TOOL_PATH_PLANNER_H
#define TOOL_PATH_PLANNER_H

#include <vtkSmartPointer.h>
#include <vtkPolyData.h>
#include <vtkParametricSpline.h>

#include <pcl/PolygonMesh.h>

#include <vtk_viewer/vtk_viewer.h>

namespace tool_path_planner
{
  struct ProcessPath
  {
    vtkSmartPointer<vtkPolyData> line; // sequence of points and a normal defining the locations and z-axis orientation of the tool along the path
    vtkSmartPointer<vtkParametricSpline> spline; // spline used to generate the line lamda goes from 0 to 1 as the line goes from start to finish
    vtkSmartPointer<vtkPolyData> derivatives; // derivatives are the direction of motion along the spline
    vtkSmartPointer<vtkPolyData> intersection_plane; // May belong here, ok to return empty{}, used by the raster_tool_path_planner and returned for display
  };

  struct ProcessTool
  {
    double pt_spacing; // requried spacing between path points
    double line_spacing; // offset between two rasters
    double tool_offset; // how far off the surface the tool needs to be
    double intersecting_plane_height; // Used by the raster_tool_path_planner when offsetting to an adjacent path, a new plane has to be formed, but not too big
    int nearest_neighbors; // how many neighbors are used to compute local normals
    double min_hole_size; // A path may pass over holes smaller than this, but must be broken when larger holes are encounterd. 
    bool use_ransac_normal_estimation; // set to use ransac to determine normals, otherwise, average normals of nearby mesh vertices
    double plane_fit_threhold; // how much deviation from the plane is acceptable for it to be an inlier (ransac normal estiamtion)
    double min_segment_size; // the minimum segment size to allow when finding intersections; small segments will be discarded
  };

  class ToolPathPlanner
  {
  public:

    //ToolPathPlanner()
    virtual ~ToolPathPlanner(){}

    /**
     * @brief planPaths plans a set of paths for all meshes in a given list
     * @param meshes A vector of meshes to plan paths for
     * @param paths The resulting path data generated
     */
    virtual void planPaths(const vtkSmartPointer<vtkPolyData> mesh, std::vector<ProcessPath>& paths)=0;
    virtual void planPaths(const std::vector<vtkSmartPointer<vtkPolyData> > meshes, std::vector< std::vector<ProcessPath> >& paths)=0;
    virtual void planPaths(const std::vector<pcl::PolygonMesh>& meshes, std::vector< std::vector<ProcessPath> >& paths)=0;
    virtual void planPaths(const pcl::PolygonMesh& mesh, std::vector<ProcessPath>& paths)=0;

    /**
     * @brief setInputMesh Sets the input mesh to generate paths
     * @param mesh The input mesh to be operated on
     */
    virtual void setInputMesh(vtkSmartPointer<vtkPolyData> mesh)=0;

    /**
     * @brief getInputMesh Gets the input mesh used for generating paths
     * @return The stored input mesh
     */
    virtual vtkSmartPointer<vtkPolyData> getInputMesh()=0;

    /**
     * @brief setTool Sets the tool parameters used during path generation
     * @param tool The tool object with all of the parameters necessary for path generation
     */
    virtual void setTool(ProcessTool tool)=0;

    /**
     * @brief getTool Gets the tool parameters used during path generation
     * @return The set of tool parameters
     */
    virtual ProcessTool getTool()=0;

    /**
     * @brief getNextPath Creates the next path offset from the current path
     * @param this_path The current path, from which to create an offset path
     * @param next_path The next path returned after calling the function
     * @param dist The distance to offset the next path from the current
     * * @param test_self_intersection Disables check to see if new path intersects with any previously generated paths
     * @return True if the next path is successfully created, False if no path can be generated
     */
    virtual bool getNextPath(const ProcessPath this_path, ProcessPath& next_path, double dist = 0.0, bool test_self_intersection = true)=0;

    /**
     * @brief computePaths Will create and store all paths possible from the given mesh and starting path
     * @return True if paths were generated, False if the first path is not available (nothing to start from)
     */
    virtual bool computePaths()=0;

    /**
     * @brief getPaths Gets all of the paths generated
     * @return The paths generated from the computePaths() function
     */
    virtual std::vector<ProcessPath> getPaths()=0;

    /**
     * @brief setDebugModeOn Turn on debug mode to visualize every step of the path planning process
     * @param debug Turns on debug if true, turns off debug if false
     */
    virtual void setDebugMode(bool debug)=0;
  };

  double squared_distance(std::vector<double>& pt1, std::vector<double>& pt2);

  /**
   * @brief flipPointOrder Inverts a path, points, normals, and derivatives (not necessarily the spline)
   * @param path The input path to invert
   */
  void flipPointOrder(ProcessPath& path);

  /**
   * @brief findClosestPoint Finds the closest point in a list to a target point
   * @param pt The target point
   * @param pts The list of points to search for the closest point
   * @return The index of the closest point
   */
  int findClosestPoint(std::vector<double>& pt,  std::vector<std::vector<double> >& pts);

}

#endif // PATH_PLANNER_H
