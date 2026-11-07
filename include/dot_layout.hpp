#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace dot_layout {

enum class NodeShape {
  Box,
  Circle,
  Ellipse,
  Point,
};

struct Node {
  std::string id;
  std::string label;
  NodeShape shape = NodeShape::Box;
};

struct Edge {
  std::string from;
  std::string to;
  std::string label;
  int weight = 1;
  std::optional<std::string> tail_cluster;
  std::optional<std::string> head_cluster;
};

struct Cluster {
  std::string id;
  std::string label;
  std::vector<std::string> nodes;
  std::optional<std::string> parent;
};

struct DirectedGraph {
  std::vector<Node> nodes;
  std::vector<Edge> edges;
  std::vector<Cluster> clusters;
  bool empty() const {
      return nodes.empty() && edges.empty() && clusters.empty();
  }
};

enum class RankDirection {
  TopToBottom,
  LeftToRight,
  BottomToTop,
  RightToLeft,
};

enum class RatioMode {
  Fill,
  Compress,
  Expand,
};

struct Size {
  double width;
  double height;
};

struct LayoutOptions {
  RankDirection rank_direction = RankDirection::LeftToRight;
  double rank_separation = 0.5;
  double node_separation = 0.25;
  bool compound = false;
  std::optional<std::variant<RatioMode, double>> ratio;
  std::optional<Size> size;
};

struct Point {
  double x = 0.0;
  double y = 0.0;
};

struct NodeLayout {
  std::string id;
  Point center;
  double width = 0.0;
  double height = 0.0;
  NodeShape shape = NodeShape::Box;
};

struct EdgeLabelLayout {
  std::string text;
  Point center;
  double width = 0.0;
  double height = 0.0;
};

struct EdgeLayout {
  std::string from;
  std::string to;
  std::vector<std::vector<Point>> spline_points;
  std::optional<EdgeLabelLayout> label;
  std::vector<std::optional<Point>> arrow_tips;
};

struct Bounds {
  double min_x = 0.0;
  double min_y = 0.0;
  double max_x = 0.0;
  double max_y = 0.0;
};

struct ClusterLayout {
  std::string id;
  Bounds bounds;
  std::optional<EdgeLabelLayout> label;
};

struct LayoutResult {
  std::vector<NodeLayout> nodes;
  std::vector<EdgeLayout> edges;
  Bounds bounds;
  std::vector<ClusterLayout> clusters;
};

class DotLayout {
public:
  LayoutResult layout(const DirectedGraph& graph,
                      const LayoutOptions& options = {}) const;
};

} // namespace dot_layout
