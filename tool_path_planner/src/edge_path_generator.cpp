/*
 * edge_path_generator.cpp
 *
 *  Created on: Nov 9, 2019
 *      Author: jrgnicho
 */

#include <boost/make_shared.hpp>
#include <pcl/conversions.h>
#include <pcl/common/centroid.h>
#include <pcl/filters/conditional_removal.h>
#include <pcl/kdtree/kdtree.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/segmentation/extract_clusters.h>
#include <Eigen/Geometry>
#include <Eigen/Eigenvalues>
#include <noether_conversions/noether_conversions.h>
#include <console_bridge/console.h>
#include <eigen_conversions/eigen_msg.h>
#include <numeric>
#include "tool_path_planner/edge_path_generator.h"


bool filterEdges(pcl::PointCloud<pcl::PointXYZHSV>::ConstPtr input, pcl::PointIndices& indices, double percent_threshold = 0.05)
{
  using namespace pcl;
  using PType = std::remove_reference<decltype(*input)>::type::PointType;
  using CondType = pcl::ConditionAnd<PType>;

  double min, max;
  // ======================= Determine max in v channel ==========================
  max = (*std::max_element(input->begin(),input->end(),[](PType p1, PType p2){
    return p1.v < p2.v;
  })).v;
  min = max  * ( 1 - percent_threshold );
  CONSOLE_BRIDGE_logInform("\tFound v channel max: (%f), removing points with v < %f", max, min);

  // ======================= Filtering v channel ============================

  // build the range condition
  CondType::Ptr range_cond  = boost::make_shared<CondType>();
  range_cond->addComparison(boost::make_shared< FieldComparison<PType> >("v", ComparisonOps::LT,min));

  // build the filter
  ConditionalRemoval<PType> cond_filter(true);
  cond_filter.setInputCloud(input);
  cond_filter.setCondition(range_cond);
  cond_filter.setKeepOrganized(false);

  pcl::PointCloud<PType> filtered_cloud;
  cond_filter.filter(filtered_cloud);

  cond_filter.getRemovedIndices(indices);
  CONSOLE_BRIDGE_logInform("\tRetained %lu edge points out of %lu",indices.indices.size(),input->size());
  return !indices.indices.empty();
}

bool reorder(const tool_path_planner::EdgePathConfig& config,
             pcl::PointCloud<pcl::PointXYZ>::ConstPtr points ,const pcl::PointIndices& cluster_indices,
             std::vector<int>& rearranged_indices)
{
  using namespace pcl;
  const int k_points = 1;
  pcl::IndicesPtr active_indices = boost::make_shared<decltype(cluster_indices.indices)>(cluster_indices.indices);

  // downsampling first
  if(cluster_indices.indices.size() > config.edge_cluster_min)
  {
    pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_downsampled = boost::make_shared<PointCloud<PointXYZ>>();
    pcl::VoxelGrid<PointXYZ> voxelgrid;
    voxelgrid.setInputCloud(points);
    voxelgrid.setIndices(active_indices);
    voxelgrid.setLeafSize(config.voxel_size, config.voxel_size, config.voxel_size);
    voxelgrid.filter(*cloud_downsampled);
    CONSOLE_BRIDGE_logInform("\tDownsampled edge cluster to %lu points from %lu", cloud_downsampled->size(), active_indices->size());

    // using kd tree to find closest points from original cloud
    std::vector<int> retained_ind;
    pcl::KdTreeFLANN<pcl::PointXYZ> recover_indices_kdtree;
    recover_indices_kdtree.setEpsilon(config.voxel_size);
    recover_indices_kdtree.setInputCloud(points,active_indices);
    for(std::size_t i = 0; i < cloud_downsampled->size(); i++)
    {
      // find closest
      std::vector<int> k_indices(k_points);
      std::vector<float> k_sqr_distances(k_points);
      PointXYZ query_point = (*cloud_downsampled)[i];
      if(recover_indices_kdtree.nearestKSearch(query_point,k_points, k_indices, k_sqr_distances) < k_points)
      {
        CONSOLE_BRIDGE_logError("Nearest K Search failed to find a point during indices recovery stage");
        return false;
      }
      retained_ind.push_back(k_indices[0]);
    }

    // now assign to active points
    active_indices->assign(retained_ind.begin(), retained_ind.end());
    CONSOLE_BRIDGE_logInform("\tRetained %lu unique edge cluster points ",active_indices->size());
  }

  // removing repeated points
  std::sort(active_indices->begin(), active_indices->end());
  std::remove_reference< decltype(*active_indices) >::type::iterator last_iter = std::unique(active_indices->begin(), active_indices->end());
  active_indices->erase(last_iter, active_indices->end());

  // build reordering kd tree
  pcl::KdTreeFLANN<pcl::PointXYZ> reorder_kdtree;
  reorder_kdtree.setEpsilon(config.kdtree_epsilon);

  // now reorder based on proximity
  auto& cloud = *points;
  std::vector<int> visited_indices;
  visited_indices.reserve(active_indices->size());
  int search_point_idx = active_indices->front();
  visited_indices.push_back(search_point_idx); // insert first point

  const int max_iters = active_indices->size();
  PointXYZ search_point = cloud[search_point_idx];
  PointXYZ start_point = search_point;
  PointXYZ closest_point;
  int iter_count = 0;

  while(iter_count <= max_iters)
  {
    iter_count++;

    // remove from active
    active_indices->erase(std::remove(active_indices->begin(), active_indices->end(),search_point_idx));

    if(active_indices->empty())
    {
      break;
    }

    // set tree inputs;
    reorder_kdtree.setInputCloud(points,active_indices);

    // find next point
    std::vector<int> k_indices(k_points);
    std::vector<float> k_sqr_distances(k_points);
    int points_found = reorder_kdtree.nearestKSearch(search_point,k_points, k_indices, k_sqr_distances);
    if(points_found < k_points)
    {
      CONSOLE_BRIDGE_logError("Nearest K Search failed to find a point during reordering stage");
      return false;
    }

    // insert new point if it has not been visited
    if(std::find(visited_indices.begin(), visited_indices.end(),k_indices[0]) != visited_indices.end())
    {
      // there should be no more points to add
      CONSOLE_BRIDGE_logWarn("\tFound repeated point during reordering stage, should not happen but proceeding");
      continue;
    }

    // check if found point is further away than the start point
    closest_point = cloud[k_indices[0]];
    Eigen::Vector3d diff = (start_point.getArray3fMap() - closest_point.getArray3fMap()).cast<double>();
    if(diff.norm() < std::sqrt(k_sqr_distances[0]))
    {
      // reversing will allow adding point from the other end
      std::reverse(visited_indices.begin(),visited_indices.end());
      start_point = cloud[visited_indices[0]];
    }

    // set next search point variables
    search_point_idx = k_indices[0];
    search_point = closest_point;

    // add to visited
    visited_indices.push_back(k_indices[0]);
  }

  if(visited_indices.size() < max_iters)
  {
    CONSOLE_BRIDGE_logError("Failed to include all points in the downsampled group, only got %lu out of %lu",
                            visited_indices.size(), max_iters);
    return false;
  }
  std::copy(visited_indices.begin(), visited_indices.end(),std::back_inserter(rearranged_indices));
  return true;
}

bool createPoseArray(const pcl::PointCloud<pcl::PointNormal>& cloud_normals, const std::vector<int>& indices, geometry_msgs::PoseArray& poses)
{
  using namespace pcl;
  using namespace Eigen;

  geometry_msgs::Pose pose_msg;
  Isometry3d pose;
  Vector3d x_dir, z_dir, y_dir;

  auto point_to_vec = [](const PointNormal& p) -> Vector3d
  {
    return Vector3d(p.x, p.y, p.z);
  };

  auto to_rotation_mat = [](const Vector3d& vx, const Vector3d& vy, const Vector3d& vz) -> Matrix3d
  {
    Matrix3d rot;
    rot.block(0,0,1,3) = Vector3d(vx.x(), vy.x(), vz.x()).array().transpose();
    rot.block(1,0,1,3) = Vector3d(vx.y(), vy.y(), vz.y()).array().transpose();
    rot.block(2,0,1,3) = Vector3d(vx.z(), vy.z(), vz.z()).array().transpose();
    return rot;
  };

  std::vector<int> cloud_indices;
  if(indices.empty())
  {
    cloud_indices.resize(cloud_normals.size());
    std::iota(cloud_indices.begin(), cloud_indices.end(), 0);
  }
  else
  {
    cloud_indices.assign(indices.begin(), indices.end());
  }

  for(std::size_t i = 0; i < cloud_indices.size() - 1; i++)
  {
    std::size_t idx_current = cloud_indices[i];
    std::size_t idx_next = cloud_indices[i+1];
    if(idx_current >= cloud_normals.size() || idx_next >= cloud_normals.size())
    {
      CONSOLE_BRIDGE_logError("Invalid indices (current: %lu, next: %lu) for point cloud were passed",
                              idx_current, idx_next);
      return false;
    }
    const PointNormal& p1 = cloud_normals[idx_current];
    const PointNormal& p2 = cloud_normals[idx_next];
    x_dir = (point_to_vec(p2) - point_to_vec(p1)).normalized().cast<double>();
    z_dir = Vector3d(p1.normal_x, p1.normal_y, p1.normal_z).normalized();
    y_dir = z_dir.cross(x_dir).normalized();

    pose = Translation3d(point_to_vec(p1));
    pose.matrix().block<3,3>(0,0) = to_rotation_mat(x_dir, y_dir, z_dir);
    tf::poseEigenToMsg(pose,pose_msg);

    poses.poses.push_back(pose_msg);
  }

  // last pose
  pose_msg = poses.poses.back();
  pose_msg.position.x = cloud_normals[cloud_indices.back()].x;
  pose_msg.position.y = cloud_normals[cloud_indices.back()].y;
  pose_msg.position.z = cloud_normals[cloud_indices.back()].z;
  poses.poses.push_back(pose_msg);

  return true;
}

namespace tool_path_planner
{
EdgePathGenerator::EdgePathGenerator()
{

}

EdgePathGenerator::~EdgePathGenerator()
{

}

boost::optional< std::vector<geometry_msgs::PoseArray >> EdgePathGenerator::generate(const tool_path_planner::EdgePathConfig& config)
{
  using namespace pcl;

  if(!input_points_ || !input_cloud_ || !mesh_)
  {
    CONSOLE_BRIDGE_logError("No input mesh data has been set");
    return boost::none;
  }

  // initializing octree
  octree_search_ = boost::make_shared< octree::OctreePointCloudSearch<PointXYZ> >(config.octree_res);
  octree_search_->setInputCloud (input_points_);
  octree_search_->addPointsFromInputCloud ();

  // initializing edge points
  PointCloud<PointXYZHSV>::Ptr edge_results = boost::make_shared<PointCloud<PointXYZHSV>>();
  pcl::copyPointCloud(*input_points_, *edge_results);

  // identify high v values
  const int num_threads = config.num_threads;
  long cnt = 0;
  long percentage = 0;
  long num_points = static_cast<long>(input_points_->size());
  CONSOLE_BRIDGE_logInform("Computing Eigen Values");
#pragma omp parallel for num_threads(num_threads)
  // TODO avoid doing this for every points and used the returned indices 'point_idx' to
  //      remove the points that have already been visited
  for (std::size_t idx = 0; idx < input_points_->points.size(); ++idx)
  {
    std::vector<int> point_idx;
    std::vector<float> point_squared_distance;
    octree_search_->radiusSearch (static_cast<int>(idx), config.search_radius, point_idx, point_squared_distance);

    Eigen::Vector4d centroid;
    Eigen::Matrix3d covariance_matrix;

    pcl::computeMeanAndCovarianceMatrix (*input_points_,
                                         point_idx,
                                         covariance_matrix,
                                         centroid);

    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver;
    solver.compute(covariance_matrix, false);
    auto eigen_values = solver.eigenvalues();
    double sigma1 = eigen_values(0)/(eigen_values(0) + eigen_values(1) + eigen_values(2));
    double sigma2 = eigen_values(1)/(eigen_values(0) + eigen_values(1) + eigen_values(2));
    double sigma3 = eigen_values(2)/(eigen_values(0) + eigen_values(1) + eigen_values(2));
    edge_results->points[idx].h = static_cast<float>(sigma1);
    edge_results->points[idx].s = static_cast<float>(sigma2);
    edge_results->points[idx].v = static_cast<float>(sigma3); // will use this value for edge detection

#pragma omp critical
    {
      ++cnt;
      if (static_cast<long>(100.0f * static_cast<float>(cnt)/num_points) > percentage)
      {
        percentage = static_cast<long>(100.0f * static_cast<float>(cnt)/num_points);
        std::stringstream ss;
        std::cout << "\r\tPoints Processed: " << percentage << "% of " << num_points;
      }
    }
  }
  std::cout<<std::endl;

  pcl::PointIndices edge_indices;
  if(!filterEdges(edge_results, edge_indices, config.neighbor_tol))
  {
    CONSOLE_BRIDGE_logError("Failed to filter edge points");
    return boost::none;
  }
  CONSOLE_BRIDGE_logInform("Found %lu edge points", edge_indices.indices.size());

  // TODO: only using one cluster at the moment, reassess whether it's necessary to have more than one.  Examples
  //        may involve running Euclidean clustering on the edge indices that could produce to multiple "edge clusters"
  std::vector<PointIndices> clusters_indices;
  clusters_indices.push_back(edge_indices);

  // deleting edge points from octree
  for(std::size_t c = 0; c < clusters_indices.size(); c++)
  {
    for(std::size_t i = 0; i < clusters_indices[c].indices.size(); i++)
    {
      int idx = clusters_indices[c].indices[i];
      octree_search_->deleteVoxelAtPoint((*input_points_)[idx]);
    }
  }

  // generating paths from the segments
  std::vector<geometry_msgs::PoseArray> edge_paths;
  for(std::size_t c = 0; c < clusters_indices.size(); c++)
  {
    PointIndices& cluster  = clusters_indices[c];

    // rearrange so that the points follow a consistent order
    PointIndices rearranged_cluster_indices;
    CONSOLE_BRIDGE_logInform("Reordering edge cluster %lu", c);
    if(!reorder(config, input_points_,cluster, rearranged_cluster_indices.indices))
    {
      return boost::none;
    }

    // split into segments using octree
    decltype(clusters_indices) segments_indices;
    splitEdgeSegments(config, rearranged_cluster_indices, segments_indices);
    CONSOLE_BRIDGE_logInform("Splitted edge cluster into %i segments", segments_indices.size());

    // converting the rearranged point segments into poses
    for(std::size_t s = 0; s < segments_indices.size(); s++)
    {
      auto& segment = segments_indices[s];

      // merging
      pcl::PointCloud<pcl::PointNormal> merged_points;
      int merged_point_count = mergePoints(config, segment,merged_points);
      CONSOLE_BRIDGE_logInform("\tMerged %i points", merged_point_count);

      geometry_msgs::PoseArray path_poses;
      CONSOLE_BRIDGE_logInform("\tConverting edge segment %lu of cluster %lu to poses", s, c);
/*      if(!createPoseArray(*input_cloud_,segment.indices, path_poses))
      {
        return boost::none;
      }*/

      if(!createPoseArray(merged_points, {}, path_poses))
      {
        return boost::none;
      }
      edge_paths.push_back(path_poses);
    }
  }
  return edge_paths;
}

void EdgePathGenerator::setInput(pcl::PolygonMesh::ConstPtr mesh)
{
  using namespace pcl;
  mesh_ = mesh;
  input_cloud_ = boost::make_shared<PointCloud<PointNormal>>();
  input_points_ = boost::make_shared<PointCloud<PointXYZ>>();
  noether_conversions::convertToPointNormals(*mesh, *input_cloud_);
  pcl::copyPointCloud(*input_cloud_, *input_points_ );
}

void EdgePathGenerator::setInput(const shape_msgs::Mesh& mesh)
{
  pcl::PolygonMesh::Ptr pcl_mesh = boost::make_shared<pcl::PolygonMesh>();
  noether_conversions::convertToPCLMesh(mesh,*pcl_mesh);
  setInput(pcl_mesh);
}

boost::optional<std::vector<geometry_msgs::PoseArray>>
EdgePathGenerator::generate(const shape_msgs::Mesh& mesh, const tool_path_planner::EdgePathConfig& config)
{
  setInput(mesh);
  return generate(config);
}

boost::optional<std::vector<geometry_msgs::PoseArray>>
EdgePathGenerator::generate(pcl::PolygonMesh::ConstPtr mesh, const tool_path_planner::EdgePathConfig& config)
{
  setInput(mesh);
  return generate(config);
}

int EdgePathGenerator::mergePoints(const tool_path_planner::EdgePathConfig& config,const pcl::PointIndices& edge_segment,
                                    pcl::PointCloud<pcl::PointNormal>& merged_points)
{
  using namespace Eigen;
  using namespace pcl;
  using PType = std::remove_reference<decltype(merged_points)>::type::PointType;
  decltype(edge_segment.indices) merged_indices;
  Vector3f dir;
  PType mid_point;


  auto compute_dist = [&](const PType& p1, const PType& p2) -> double
  {
    dir = p2.getVector3fMap() - p1.getVector3fMap();
    return dir.norm();
  };


  for(std::size_t i = 0; i < edge_segment.indices.size()-1; i++)
  {
    int idx_current = edge_segment.indices[i];
    int idx_next = edge_segment.indices[i+1];

    if(std::find(merged_indices.begin(), merged_indices.end(), idx_current) != merged_indices.end() )
    {
      // already merged so continue
      continue;
    }

    const PType& p1 = (*input_cloud_)[idx_current];
    const PType& p2 = (*input_cloud_)[idx_next];
    dir = p2.getVector3fMap() - p1.getVector3fMap();
    if(compute_dist(p1, p2) >  config.merge_dist)
    {
      // adding current point and continue
      merged_points.push_back(p1);
      continue;
    }

    mid_point.getVector3fMap() = p1.getVector3fMap() + 0.5 * dir;

    mid_point.getNormalVector3fMap() = (p2.getNormalVector3fMap() + p1.getNormalVector3fMap()).normalized();
    merged_points.push_back(mid_point);
    merged_indices.push_back(idx_next);
  }

  // checking first and last point
  const PType& p0 = (*input_cloud_)[edge_segment.indices.front()];
  const PType& pf = (*input_cloud_)[edge_segment.indices.back()];
  if(compute_dist(p0, merged_points.front()) > 1e-6)
  {
    merged_points.insert(merged_points.begin(),p0 );
  }

  if(compute_dist(pf, merged_points.back()) > 1e-6)
  {
    merged_points.push_back(pf);
  }

  return merged_indices.size();
}

bool EdgePathGenerator::splitEdgeSegments(const tool_path_planner::EdgePathConfig& config,
                                          const pcl::PointIndices& edge_indices,
                                          std::vector<pcl::PointIndices>& segments)
{
  using namespace Eigen;
  using namespace pcl;
  using AlignedPointTVector = std::remove_reference<decltype(*this->octree_search_)>::type::AlignedPointTVector;
  using PType = std::remove_reference<decltype(*this->input_cloud_)>::type::PointType;
  struct Ray
  {
    PType p;
    Vector3f d;
  };

  // checks for surfaces between the points and returns > 0 (num of voxels_encountered) when there's one
  auto check_surface_intersection = [&](const PType& p1, const PType& p2) -> int
  {
    // create the projection vectors and points
    PType p1_proj, p2_proj;
    Vector3d v1 = (p2.getVector3fMap() - p1.getVector3fMap()).cast<double>();

    Vector3d n_mid = (p2.getNormalVector3fMap() + p1.getNormalVector3fMap()).cast<double>();
    n_mid.normalize();

    Vector3f v1_proj = v1.dot(n_mid) * n_mid.cast<float>();
    if(v1_proj.norm() <  config.min_projection_dist)
    {
      v1_proj = config.min_projection_dist * v1_proj.normalized();
    }

    p1_proj.getVector3fMap() = v1_proj + Vector3f(p1.getVector3fMap());
    p2_proj.getVector3fMap() = (-1.0 * v1_proj) + Vector3f(p2.getVector3fMap());

    // creating the Rays
    std::vector<Ray> rays;
    rays.push_back(Ray{.p = p1_proj, .d = p2_proj.getVector3fMap() - p1_proj.getVector3fMap()}); // from p1_proj to p2_proj

    int voxels_encountered = 0;
    for(std::size_t i =0; i < rays.size(); i++)
    {
      const Ray& r = rays[i];
      AlignedPointTVector voxels;
      octree_search_->getIntersectedVoxelCenters(r.p.getVector3fMap(),r.d, voxels, 0);
      // comparing distances from origin point
      for(auto& p : voxels)
      {
        Vector3f dv = p.getVector3fMap() - r.p.getVector3fMap();
        voxels_encountered += (dv.norm() <= r.d.norm() ? 1 : 0);
      }

      if(voxels_encountered > config.max_intersecting_voxels)
      {
        return voxels_encountered;
      }
    }
    return 0;
  };

  pcl::PointIndices current_segment_indices= edge_indices;
  bool split = false;
  int split_idx = -1;
  for(std::size_t i =0 ; i < current_segment_indices.indices.size() -1; i++)
  {
    const PType& p1 = (*input_cloud_)[current_segment_indices.indices[i]];
    const PType& p2 = (*input_cloud_)[current_segment_indices.indices[i+1]];

    int voxels_encountered = check_surface_intersection(p1,p2);
    if(voxels_encountered > 0 && i > 0)
    {
      // splitting current segment into two
      split = true;
      split_idx = i;
      CONSOLE_BRIDGE_logInform("Splitting at index %i, encountered %i voxels > %i ",
                               current_segment_indices.indices[split_idx], voxels_encountered, config.max_intersecting_voxels);
      break;
    }
  }

  if(split)
  {
    pcl::PointIndices next_segment_indices;
    auto next_segment_start_iter = std::next(current_segment_indices.indices.begin(),split_idx + 1);
    next_segment_indices.indices.assign(next_segment_start_iter,current_segment_indices.indices.end());
    current_segment_indices.indices.erase(next_segment_start_iter,current_segment_indices.indices.end());
    segments.push_back(current_segment_indices);

    if(next_segment_indices.indices.size() < 2)
    {
      CONSOLE_BRIDGE_logWarn("Only one point was found in the next segment, will ignore");
      return true;
    }

    return splitEdgeSegments(config, next_segment_indices, segments);
  }

  segments.push_back(current_segment_indices);
  return true;
}
} /* namespace tool_path_planner */

