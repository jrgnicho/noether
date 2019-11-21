#include <noether_conversions/noether_conversions.h>
#include <type_traits>
#include <Eigen/Geometry>
#include <pcl/conversions.h>
#include <pcl/io/ply_io.h>
#include <pcl/point_types.h>
#include <vtkPointData.h>

#include <console_bridge/console.h>
#include <eigen_conversions/eigen_msg.h>
#include <shape_msgs/MeshTriangle.h>

namespace noether_conversions
{

bool convertToPCLMesh(const shape_msgs::Mesh& mesh_msg, pcl::PolygonMesh& mesh)
{
  // iterating over triangles
  pcl::PointCloud<pcl::PointXYZ> mesh_points;
  mesh.polygons.clear();
  for (auto& triangle : mesh_msg.triangles)
  {
    pcl::Vertices vertices;
    vertices.vertices.assign(triangle.vertex_indices.begin(),triangle.vertex_indices.end());
    mesh.polygons.push_back(vertices);
  }

  std::transform(mesh_msg.vertices.begin(),mesh_msg.vertices.end(),std::back_inserter(mesh_points.points),[](
      const geometry_msgs::Point& point){
    pcl::PointXYZ p;
    std::tie(p.x,p.y,p.z) = std::make_tuple(point.x,point.y,point.z);
    return p;
  });

  pcl::toPCLPointCloud2(mesh_points,mesh.cloud);
  return true;
}

bool convertToMeshMsg(const pcl::PolygonMesh& mesh, shape_msgs::Mesh& mesh_msg)
{
  if(mesh.polygons.empty())
  {
    CONSOLE_BRIDGE_logError("PolygonMesh has no polygons");
    return false;
  }


  pcl::PointCloud<pcl::PointXYZ> cloud;
  pcl::fromPCLPointCloud2(mesh.cloud,cloud);
  if(cloud.empty())
  {
    CONSOLE_BRIDGE_logError("PolygonMesh has no vertices data");
    return false;
  }

  // copying triangles
  mesh_msg.triangles.resize(mesh.polygons.size());
  for(std::size_t i = 0; i < mesh.polygons.size(); i++)
  {
    const pcl::Vertices& vertices = mesh.polygons[i];
    if(vertices.vertices.size() != 3)
    {
      CONSOLE_BRIDGE_logError("Vertex in PolygonMesh needs to have 3 elements only");
      return false;
    }

    boost::array<uint32_t, 3>& vertex = mesh_msg.triangles[i].vertex_indices;
    std::tie(vertex[0], vertex[1], vertex[2]) = std::make_tuple(vertices.vertices[0],
                                                                vertices.vertices[1],
                                                                vertices.vertices[2]);
  }

  // copying vertices
  mesh_msg.vertices.resize(cloud.size());
  std::transform(cloud.begin(),cloud.end(),mesh_msg.vertices.begin(),[](pcl::PointXYZ& v){
    geometry_msgs::Point p;
    std::tie(p.x, p.y, p.z) = std::make_tuple(v.x,v.y,v.z);
    return std::move(p);
  });
  return true;
}

bool savePLYFile(const std::string& filename, const shape_msgs::Mesh& mesh_msg)
{
  pcl::PolygonMesh mesh;
  return convertToPCLMesh(mesh_msg, mesh) && pcl::io::savePLYFile(filename,mesh) >= 0;
}

bool loadPLYFile(const std::string& filename, shape_msgs::Mesh& mesh_msg)
{
  pcl::PolygonMesh mesh;
  return (pcl::io::loadPLYFile(filename,mesh) >= 0) && convertToMeshMsg(mesh,mesh_msg);
}

void convertToPointNormals(const pcl::PolygonMesh& mesh, pcl::PointCloud<pcl::PointNormal>& cloud_normals, bool flip)
{
  using namespace pcl;
  using namespace Eigen;
  using PType = std::remove_reference<decltype(cloud_normals)>::type::PointType;
  PointCloud<PointXYZ> points;
  pcl::fromPCLPointCloud2<PointXYZ>(mesh.cloud, points);
  pcl::copyPointCloud(points, cloud_normals);

  // computing the normals by walking the vertices
  Vector3d a, b, c;
  Vector3d dir;
  for(std::size_t i = 0; i < mesh.polygons.size(); i++)
  {
    std::vector<uint32_t>  vert = mesh.polygons[i].vertices;
    a = points[vert[0]].getVector3fMap().cast<double>();
    b = points[vert[1]].getVector3fMap().cast<double>();
    c = points[vert[2]].getVector3fMap().cast<double>();
    dir = (b - a).cross((c -a)).normalized();
    dir = flip ? -1.0 * dir : dir;

    // assigning to points
    for(std::size_t v = 0; v < vert.size(); v++)
    {
      cloud_normals[v].normal_x = dir.x();
      cloud_normals[v].normal_y = dir.y();
      cloud_normals[v].normal_z = dir.z();
    }
  }
}

}
