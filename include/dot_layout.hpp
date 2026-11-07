#pragma once

#include <optional>
#include <string>
#include <vector>

namespace dot_layout {

struct Node {
  std::string id;
  std::string label;
};

struct Edge {
  std::string from;
  std::string to;
  std::string label;
};

struct DirectedGraph {
  std::vector<Node> nodes;
  std::vector<Edge> edges;
};

enum class RankDirection {
  TopToBottom,
  LeftToRight,
  BottomToTop,
  RightToLeft,
};

struct LayoutOptions {
  RankDirection rank_direction = RankDirection::LeftToRight;
  double rank_separation = 0.5;
  double node_separation = 0.25;
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
};

struct Bounds {
  double min_x = 0.0;
  double min_y = 0.0;
  double max_x = 0.0;
  double max_y = 0.0;
};

struct LayoutResult {
  std::vector<NodeLayout> nodes;
  std::vector<EdgeLayout> edges;
  Bounds bounds;
};

class DotLayout {
public:
  LayoutResult layout(const DirectedGraph& graph,
                      const LayoutOptions& options = {}) const;
};

} // namespace dot_layout
