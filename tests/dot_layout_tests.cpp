#include "dot_layout.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename F>
void requireInvalidArgument(F action, const char* message) {
  try {
    action();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error(message);
}

void testShapesAndCompatibility() {
  using namespace dot_layout;
  const DirectedGraph graph{
      {{"default", "Long node label"},
       {"box", "Long node label", NodeShape::Box},
       {"circle", "Long node label", NodeShape::Circle},
       {"ellipse", "Long node label", NodeShape::Ellipse},
       {"point", "Long node label", NodeShape::Point}},
      {{"default", "box", "default weight"},
       {"box", "circle", "minimum weight", 1},
       {"box", "circle", "maximum weight", 1000},
       {"circle", "circle", "self loop", 7}}};

  const auto result = DotLayout{}.layout(graph);
  require(result.nodes.size() == graph.nodes.size(), "Missing nodes");
  require(result.edges.size() == graph.edges.size(), "Missing edges");
  require(result.nodes[0].shape == NodeShape::Box, "Default shape is not box");
  for (std::size_t i = 0; i < graph.nodes.size(); ++i) {
    require(result.nodes[i].id == graph.nodes[i].id, "Node order changed");
    require(result.nodes[i].shape == graph.nodes[i].shape, "Shape not returned");
    require(result.nodes[i].width > 0 && result.nodes[i].height > 0,
            "Node dimensions must be positive");
  }
  require(std::abs(result.nodes[2].width - result.nodes[2].height) < 0.1,
          "Circle layout is not circular");
  require(result.nodes[3].width > result.nodes[1].width,
          "Ellipse layout does not use ellipse shape");
  require(result.nodes[4].width < result.nodes[1].width &&
              result.nodes[4].height < result.nodes[1].height,
          "Point layout does not use point shape");
  for (std::size_t i = 0; i < graph.edges.size(); ++i) {
    require(result.edges[i].from == graph.edges[i].from &&
                result.edges[i].to == graph.edges[i].to,
            "Edge order or endpoints changed");
    require(!result.edges[i].spline_points.empty(), "Missing edge geometry");
    require(result.edges[i].label && result.edges[i].label->text == graph.edges[i].label,
            "Edge label changed");
  }
}

void testValidation() {
  using namespace dot_layout;
  DirectedGraph graph{{{"a", "A"}, {"b", "B"}}, {{"a", "b", ""}}};
  for (const int weight : {0, -1, 1001}) {
    graph.edges[0].weight = weight;
    requireInvalidArgument([&] { DotLayout{}.layout(graph); },
                           "Invalid weight was accepted");
  }
  graph.edges[0].weight = 1;
  graph.nodes[0].shape = static_cast<NodeShape>(-1);
  requireInvalidArgument([&] { DotLayout{}.layout(graph); },
                         "Invalid shape was accepted");
}

void testWeightAffectsLayout() {
  using namespace dot_layout;
  DirectedGraph graph{
      {{"a", "a"}, {"b", "b"}, {"c", "c"}, {"d", "d"}},
      {{"a", "c", "", 100}, {"a", "d", "", 1},
       {"b", "c", "", 1}, {"b", "d", "", 1}}};
  LayoutOptions options;
  options.rank_direction = RankDirection::TopToBottom;
  const auto first = DotLayout{}.layout(graph, options);
  graph.edges[0].weight = 1;
  graph.edges[1].weight = 100;
  const auto second = DotLayout{}.layout(graph, options);
  const double first_ac = std::abs(first.nodes[0].center.x - first.nodes[2].center.x);
  const double second_ac = std::abs(second.nodes[0].center.x - second.nodes[2].center.x);
  const double first_ad = std::abs(first.nodes[0].center.x - first.nodes[3].center.x);
  const double second_ad = std::abs(second.nodes[0].center.x - second.nodes[3].center.x);
  require(first_ac < second_ac && second_ad < first_ad,
          "Changing edge weights did not change preferred alignment");
}

void testDefaultWeight() {
  using namespace dot_layout;
  DirectedGraph graph{
      {{"a", "a"}, {"b", "b"}, {"c", "c"}, {"d", "d"}},
      {{"a", "c", ""}, {"a", "d", ""}, {"b", "c", ""}, {"b", "d", ""}}};
  const auto defaults = DotLayout{}.layout(graph);
  for (auto& edge : graph.edges) {
    edge.weight = 1;
  }
  const auto explicit_ones = DotLayout{}.layout(graph);
  for (std::size_t i = 0; i < defaults.nodes.size(); ++i) {
    require(defaults.nodes[i].center.x == explicit_ones.nodes[i].center.x &&
                defaults.nodes[i].center.y == explicit_ones.nodes[i].center.y,
            "Default edge weight differs from one");
  }
  for (const auto& edge : defaults.edges) {
    require(!edge.label, "Unlabeled edge gained a label");
  }
}

void testArrowTipsOnNodeBoundaries() {
  using namespace dot_layout;
  for (const auto shape : {NodeShape::Box, NodeShape::Circle,
                           NodeShape::Ellipse, NodeShape::Point}) {
    for (const auto direction : {RankDirection::TopToBottom,
                                 RankDirection::LeftToRight,
                                 RankDirection::BottomToTop,
                                 RankDirection::RightToLeft}) {
      const DirectedGraph graph{
          {{"source", "Source"}, {"target", "Target", shape}},
          {{"source", "target", ""}, {"source", "target", "parallel"},
           {"target", "target", "loop"}}};
      LayoutOptions options;
      options.rank_direction = direction;
      const auto result = DotLayout{}.layout(graph, options);
      const auto& node = result.nodes[1];
      for (const auto& edge : result.edges) {
        require(edge.arrow_tips.size() == edge.spline_points.size(),
                "Arrow tips do not match spline groups");
        bool found_tip = false;
        for (std::size_t i = 0; i < edge.arrow_tips.size(); ++i) {
          if (!edge.arrow_tips[i])
            continue;
          found_tip = true;
          const auto tip = *edge.arrow_tips[i];
          const double dx = (tip.x - node.center.x) / (node.width / 2.0);
          const double dy = (tip.y - node.center.y) / (node.height / 2.0);
          const double boundary = shape == NodeShape::Box
              ? std::max(std::abs(dx), std::abs(dy)) : std::hypot(dx, dy);
          require(std::abs(boundary - 1.0) < 1e-9,
                  "Arrow tip is not on the target node boundary");
          require(!edge.spline_points[i].empty(), "Arrow has no spline");
          const auto end = edge.spline_points[i].back();
          require(std::hypot(tip.x - end.x, tip.y - end.y) > 1e-6,
                  "Arrow has no direction");
          require(tip.x >= result.bounds.min_x && tip.x <= result.bounds.max_x &&
                      tip.y >= result.bounds.min_y && tip.y <= result.bounds.max_y,
                  "Arrow tip is outside layout bounds");
        }
        require(found_tip, "Directed edge is missing its arrow tip");
      }
    }
  }
}

bool nearBoundary(dot_layout::Point p, const dot_layout::Bounds& b, double tolerance) {
  return p.x >= b.min_x - tolerance && p.x <= b.max_x + tolerance &&
         p.y >= b.min_y - tolerance && p.y <= b.max_y + tolerance &&
         (std::abs(p.x - b.min_x) <= tolerance ||
          std::abs(p.x - b.max_x) <= tolerance ||
          std::abs(p.y - b.min_y) <= tolerance ||
          std::abs(p.y - b.max_y) <= tolerance);
}

void testClusters() {
  using namespace dot_layout;
  DirectedGraph graph{
      {{"a", "A"}, {"b", "B"}, {"c", "C"}},
      {{"a", "b", "", 1, "inner", "right"}, {"b", "c", ""}},
      {{"inner", "Inner", {"a"}, "outer"},
       {"right", "Right", {"b"}, std::nullopt},
       {"outer", "Outer", {"c"}, std::nullopt}}};
  LayoutOptions options;
  options.compound = true;
  for (const auto direction : {RankDirection::TopToBottom, RankDirection::LeftToRight,
                               RankDirection::BottomToTop, RankDirection::RightToLeft}) {
    for (const bool use_ratio : {false, true}) {
      options.rank_direction = direction;
      options.ratio = use_ratio ? std::optional<std::variant<RatioMode, double>>(1.0)
                                : std::nullopt;
      const auto result = DotLayout{}.layout(graph, options);
      require(result.clusters.size() == 3, "Missing clusters");
      for (std::size_t i = 0; i < result.clusters.size(); ++i) {
        const auto& cluster = result.clusters[i];
        require(cluster.id == graph.clusters[i].id && cluster.label &&
                    cluster.label->text == graph.clusters[i].label,
                "Missing cluster label");
        require(cluster.label->center.x >= cluster.bounds.min_x &&
                    cluster.label->center.x <= cluster.bounds.max_x &&
                    cluster.label->center.y >= cluster.bounds.min_y &&
                    cluster.label->center.y <= cluster.bounds.max_y &&
                    cluster.label->width > 0 && cluster.label->height > 0,
                "Invalid cluster label geometry");
      }
      const auto inner = result.clusters[0].bounds;
      const auto right = result.clusters[1].bounds;
      const auto outer = result.clusters[2].bounds;
      require(inner.min_x >= outer.min_x && inner.max_x <= outer.max_x &&
                  inner.min_y >= outer.min_y && inner.max_y <= outer.max_y,
              "Nested cluster is outside parent");
      require(result.bounds.min_x <= outer.min_x && result.bounds.max_x >= outer.max_x &&
                  result.bounds.min_y <= outer.min_y && result.bounds.max_y >= outer.max_y,
              "Layout bounds omit cluster");
      const auto& edge = result.edges[0];
      require(!edge.arrow_tips.empty() && edge.arrow_tips[0].has_value(),
              "Missing compound arrow tip");
      require(nearBoundary(*edge.arrow_tips[0], right, 1.0),
              "Compound arrow tip is not on head cluster");
      require(!edge.spline_points.empty() && !edge.spline_points[0].empty() &&
                  nearBoundary(edge.spline_points[0].front(), inner, 1.0),
              "Compound spline does not start on tail cluster");
    }
  }
  graph.edges[0].tail_cluster.reset();
  auto head_only = DotLayout{}.layout(graph, options);
  require(nearBoundary(*head_only.edges[0].arrow_tips[0], head_only.clusters[1].bounds, 1.0),
          "Head-only cluster clipping failed");
  graph.edges[0].tail_cluster = "inner";
  graph.edges[0].head_cluster.reset();
  auto tail_only = DotLayout{}.layout(graph, options);
  require(nearBoundary(tail_only.edges[0].spline_points[0].front(), tail_only.clusters[0].bounds, 1.0),
          "Tail-only cluster clipping failed");
  graph.edges[0].head_cluster = "right";
  options.compound = false;
  requireInvalidArgument([&] { DotLayout{}.layout(graph, options); },
                         "Cluster endpoints accepted without compound");
  options.compound = true;
  graph.edges[0].head_cluster = "outer";
  requireInvalidArgument([&] { DotLayout{}.layout(graph, options); },
                         "Invalid head cluster accepted");
  graph.edges[0].head_cluster = "missing";
  requireInvalidArgument([&] { DotLayout{}.layout(graph, options); },
                         "Unknown cluster accepted");
  graph.edges[0].head_cluster = "right";
  graph.clusters[2].parent = "inner";
  requireInvalidArgument([&] { DotLayout{}.layout(graph, options); },
                         "Cluster cycle accepted");
  graph.clusters[2].parent.reset();
  graph.clusters[2].nodes.push_back("a");
  requireInvalidArgument([&] { DotLayout{}.layout(graph, options); },
                         "Duplicate cluster membership accepted");
  graph.clusters[2].nodes.pop_back();
  graph.clusters[2].nodes.push_back("missing");
  requireInvalidArgument([&] { DotLayout{}.layout(graph, options); },
                         "Unknown cluster node accepted");
  graph.clusters[2].nodes.pop_back();
  graph.clusters.push_back({"empty", "", {}});
  requireInvalidArgument([&] { DotLayout{}.layout(graph, options); },
                         "Empty cluster accepted");
  graph.clusters.pop_back();
  graph.clusters[0].label.clear();
  require(!DotLayout{}.layout(graph, options).clusters[0].label,
          "Empty cluster label should not be returned");
}

void testRatio() {
  using namespace dot_layout;
  const DirectedGraph graph{{{"a", "A"}, {"b", "B"}, {"c", "C"}, {"d", "D"}},
                            {{"a", "b", ""}, {"a", "c", ""}, {"b", "d", ""}}};
  LayoutOptions options;
  const auto baseline = DotLayout{}.layout(graph, options);
  options.ratio = 1.0;
  const auto square = DotLayout{}.layout(graph, options);
  const double width = square.bounds.max_x - square.bounds.min_x;
  const double height = square.bounds.max_y - square.bounds.min_y;
  const double baseline_ratio = (baseline.bounds.max_x - baseline.bounds.min_x) /
                                (baseline.bounds.max_y - baseline.bounds.min_y);
  require(std::abs(width / height - 1.0) < std::abs(baseline_ratio - 1.0),
          "Numeric ratio did not improve aspect ratio");
  for (const auto mode : {RatioMode::Fill, RatioMode::Compress, RatioMode::Expand}) {
    options.ratio = mode;
    requireInvalidArgument([&] { DotLayout{}.layout(graph, options); },
                           "Named ratio without size accepted");
    options.size = Size{8.0, 6.0};
    const auto result = DotLayout{}.layout(graph, options);
    require(result.nodes.size() == graph.nodes.size(), "Named ratio layout failed");
    if (mode == RatioMode::Fill) {
      const auto w = result.bounds.max_x - result.bounds.min_x;
      const auto h = result.bounds.max_y - result.bounds.min_y;
      require(std::abs(w / h - 8.0 / 6.0) < std::abs(baseline_ratio - 8.0 / 6.0),
              "Fill ratio did not improve aspect ratio");
    }
    if (mode == RatioMode::Expand) {
      require(result.bounds.max_x - result.bounds.min_x >
                  baseline.bounds.max_x - baseline.bounds.min_x,
              "Expand ratio did not enlarge layout");
    }
    options.size.reset();
  }
  for (double value : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()}) {
    options.ratio = value;
    requireInvalidArgument([&] { DotLayout{}.layout(graph, options); },
                           "Invalid ratio accepted");
  }
  options.ratio.reset();
  options.size = Size{0, 1};
  requireInvalidArgument([&] { DotLayout{}.layout(graph, options); },
                         "Invalid size accepted");
  options.size.reset();
  options.ratio = static_cast<RatioMode>(-1);
  requireInvalidArgument([&] { DotLayout{}.layout(graph, options); },
                         "Invalid ratio mode accepted");
}

} // namespace

int main() {
  try {
    testShapesAndCompatibility();
    testValidation();
    testWeightAffectsLayout();
    testDefaultWeight();
    testArrowTipsOnNodeBoundaries();
    testClusters();
    testRatio();
  } catch (const std::exception& error) {
    std::cerr << "dot_layout_tests: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
