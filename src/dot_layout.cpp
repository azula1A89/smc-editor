#include "dot_layout.hpp"

#include <cgraph/cgraph.h>
#include <common/types.h>
#include <gvc/gvc.h>
#include <gvc/gvplugin.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace dot_layout {
namespace {

struct ContextDeleter {
  void operator()(GVC_t* context) const noexcept {
    if (context != nullptr) {
      gvFreeContext(context);
    }
  }
};

struct GraphDeleter {
  void operator()(Agraph_t* graph) const noexcept {
    if (graph != nullptr) {
      agclose(graph);
    }
  }
};

class LayoutGuard {
public:
  LayoutGuard(GVC_t* context, Agraph_t* graph)
      : context_(context), graph_(graph) {}

  ~LayoutGuard() {
    gvFreeLayout(context_, graph_);
  }

  LayoutGuard(const LayoutGuard&) = delete;
  LayoutGuard& operator=(const LayoutGuard&) = delete;

private:
  GVC_t* context_;
  Agraph_t* graph_;
};

const char* rankDirectionName(RankDirection direction) {
  switch (direction) {
  case RankDirection::TopToBottom:
    return "TB";
  case RankDirection::LeftToRight:
    return "LR";
  case RankDirection::BottomToTop:
    return "BT";
  case RankDirection::RightToLeft:
    return "RL";
  }
  throw std::invalid_argument("Unknown rank direction");
}

const char* nodeShapeName(NodeShape shape) {
  switch (shape) {
  case NodeShape::Box:
    return "box";
  case NodeShape::Circle:
    return "circle";
  case NodeShape::Ellipse:
    return "ellipse";
  case NodeShape::Point:
    return "point";
  }
  throw std::invalid_argument("Unknown node shape");
}

void setAttribute(void* object, const char* name, const std::string& value) {
  std::string attribute_name(name);
  if (agsafeset(object, attribute_name.data(), value.c_str(), "") != 0) {
    throw std::runtime_error(std::string("Could not set Graphviz attribute: ") +
                             name);
  }
}

double parseNumber(const char*& cursor, const char* description) {
  char* end = nullptr;
  const double value = std::strtod(cursor, &end);
  if (end == cursor || !std::isfinite(value)) {
    throw std::runtime_error(std::string("Invalid Graphviz ") + description);
  }
  cursor = end;
  return value;
}

Point parsePosition(const char* value) {
  if (value == nullptr) {
    throw std::runtime_error("Graphviz did not return a node position");
  }
  const char* cursor = value;
  const double x = parseNumber(cursor, "node x coordinate");
  if (*cursor != ',') {
    throw std::runtime_error("Invalid Graphviz node position");
  }
  ++cursor;
  const double y = parseNumber(cursor, "node y coordinate");
  if (*cursor != '\0') {
    throw std::runtime_error("Unexpected data in Graphviz node position");
  }
  return {x, y};
}

double parseDimension(const char* value, const char* description) {
  if (value == nullptr) {
    throw std::runtime_error(std::string("Graphviz did not return node ") +
                             description);
  }
  const char* cursor = value;
  const double dimension_inches = parseNumber(cursor, description);
  if (*cursor != '\0' || dimension_inches < 0.0) {
    throw std::runtime_error(std::string("Invalid Graphviz node ") +
                             description);
  }
  return dimension_inches * 72.0;
}

Point nodeBoundaryPoint(const NodeLayout& node, Point point) {
  const double dx = point.x - node.center.x;
  const double dy = point.y - node.center.y;
  const double rx = node.width / 2.0;
  const double ry = node.height / 2.0;
  if (rx <= 0.0 || ry <= 0.0 || (dx == 0.0 && dy == 0.0)) {
    throw std::runtime_error("Cannot determine arrow intersection with node");
  }
  const double factor = node.shape == NodeShape::Box
      ? 1.0 / std::max(std::abs(dx) / rx, std::abs(dy) / ry)
      : 1.0 / std::hypot(dx / rx, dy / ry);
  return {node.center.x + dx * factor, node.center.y + dy * factor};
}

} // namespace

LayoutResult DotLayout::layout(const DirectedGraph& input,
                               const LayoutOptions& options) const {
  if (!std::isfinite(options.rank_separation) ||
      options.rank_separation <= 0.0 ||
      !std::isfinite(options.node_separation) ||
      options.node_separation <= 0.0) {
    throw std::invalid_argument("Layout separations must be positive and finite");
  }

  std::unordered_map<std::string, std::size_t> node_indices;
  node_indices.reserve(input.nodes.size());
  for (std::size_t i = 0; i < input.nodes.size(); ++i) {
    const auto& id = input.nodes[i].id;
    if (id.empty() || !node_indices.emplace(id, i).second) {
      throw std::invalid_argument("Node IDs must be non-empty and unique");
    }
    nodeShapeName(input.nodes[i].shape);
  }
  for (const auto& edge : input.edges) {
    if (node_indices.find(edge.from) == node_indices.end() ||
        node_indices.find(edge.to) == node_indices.end()) {
      throw std::invalid_argument("Every edge endpoint must name an existing node");
    }
    if (edge.weight < 1 || edge.weight > 1000) {
      throw std::invalid_argument("Edge weight must be between 1 and 1000");
    }
  }

  if (input.nodes.empty()) {
    return {};
  }

  std::unique_ptr<GVC_t, ContextDeleter> context(
      gvContextPlugins(lt_preloaded_symbols, 0));
  if (!context) {
    throw std::runtime_error("Could not create Graphviz context");
  }

  char graph_name[] = "G";
  std::unique_ptr<Agraph_t, GraphDeleter> graph(
      agopen(graph_name, Agdirected, nullptr));
  if (!graph) {
    throw std::runtime_error("Could not create Graphviz graph");
  }

  setAttribute(graph.get(), "rankdir",
               rankDirectionName(options.rank_direction));
  setAttribute(graph.get(), "ranksep", std::to_string(options.rank_separation));
  setAttribute(graph.get(), "nodesep", std::to_string(options.node_separation));

  std::vector<Agnode_t*> graph_nodes;
  graph_nodes.reserve(input.nodes.size());
  for (std::size_t i = 0; i < input.nodes.size(); ++i) {
    std::string safe_id = "n" + std::to_string(i);
    Agnode_t* node = agnode(graph.get(), safe_id.data(), 1);
    if (node == nullptr) {
      throw std::runtime_error("Could not add a Graphviz node");
    }
    setAttribute(node, "label", input.nodes[i].label);
    setAttribute(node, "shape", nodeShapeName(input.nodes[i].shape));
    graph_nodes.push_back(node);
  }

  std::vector<Agedge_t*> graph_edges;
  graph_edges.reserve(input.edges.size());
  for (const auto& edge : input.edges) {
    const auto tail = graph_nodes[node_indices.at(edge.from)];
    const auto head = graph_nodes[node_indices.at(edge.to)];
    Agedge_t* graph_edge = agedge(graph.get(), tail, head, nullptr, 1);
    if (graph_edge == nullptr) {
      throw std::runtime_error("Could not add a Graphviz edge");
    }
    if (!edge.label.empty()) {
      setAttribute(graph_edge, "label", edge.label);
    }
    setAttribute(graph_edge, "weight", std::to_string(edge.weight));
    graph_edges.push_back(graph_edge);
  }

  if (gvLayout(context.get(), graph.get(), "dot") != 0) {
    throw std::runtime_error("Graphviz dot layout failed");
  }
  LayoutGuard layout_guard(context.get(), graph.get());
  attach_attrs(graph.get());

  LayoutResult result;
  result.nodes.reserve(input.nodes.size());
  bool first_node = true;
  char position_attribute[] = "pos";
  char width_attribute[] = "width";
  char height_attribute[] = "height";
  for (std::size_t i = 0; i < input.nodes.size(); ++i) {
    const Point center =
        parsePosition(agget(graph_nodes[i], position_attribute));
    const double width =
        parseDimension(agget(graph_nodes[i], width_attribute), "width");
    const double height =
        parseDimension(agget(graph_nodes[i], height_attribute), "height");
    result.nodes.push_back(
        {input.nodes[i].id, center, width, height, input.nodes[i].shape});

    const double min_x = center.x - width / 2.0;
    const double min_y = center.y - height / 2.0;
    const double max_x = center.x + width / 2.0;
    const double max_y = center.y + height / 2.0;
    if (first_node) {
      result.bounds = {min_x, min_y, max_x, max_y};
      first_node = false;
    } else {
      result.bounds.min_x = std::min(result.bounds.min_x, min_x);
      result.bounds.min_y = std::min(result.bounds.min_y, min_y);
      result.bounds.max_x = std::max(result.bounds.max_x, max_x);
      result.bounds.max_y = std::max(result.bounds.max_y, max_y);
    }
  }

  result.edges.reserve(input.edges.size());
  for (std::size_t i = 0; i < input.edges.size(); ++i) {
    const auto& input_edge = input.edges[i];
    Agedge_t* graph_edge = graph_edges[i];
    EdgeLayout edge_layout{input_edge.from, input_edge.to, {}, std::nullopt, {}};

    if (const splines* edge_splines = ED_spl(graph_edge)) {
      edge_layout.spline_points.reserve(edge_splines->size);
      edge_layout.arrow_tips.reserve(edge_splines->size);
      result.bounds.min_x = std::min(result.bounds.min_x, edge_splines->bb.LL.x);
      result.bounds.min_y = std::min(result.bounds.min_y, edge_splines->bb.LL.y);
      result.bounds.max_x = std::max(result.bounds.max_x, edge_splines->bb.UR.x);
      result.bounds.max_y = std::max(result.bounds.max_y, edge_splines->bb.UR.y);
      for (std::size_t spline_index = 0; spline_index < edge_splines->size;
           ++spline_index) {
        const bezier& spline = edge_splines->list[spline_index];
        std::vector<Point> points;
        points.reserve(spline.size);
        for (std::size_t point_index = 0; point_index < spline.size;
             ++point_index) {
          points.push_back(
              {spline.list[point_index].x, spline.list[point_index].y});
        }
        edge_layout.spline_points.push_back(std::move(points));
        if (spline.eflag != 0) {
          const auto& target = result.nodes[node_indices.at(input_edge.to)];
          const Point tip = nodeBoundaryPoint(target, {spline.ep.x, spline.ep.y});
          edge_layout.arrow_tips.push_back(tip);
          result.bounds.min_x = std::min(result.bounds.min_x, tip.x);
          result.bounds.min_y = std::min(result.bounds.min_y, tip.y);
          result.bounds.max_x = std::max(result.bounds.max_x, tip.x);
          result.bounds.max_y = std::max(result.bounds.max_y, tip.y);
        } else {
          edge_layout.arrow_tips.push_back(std::nullopt);
        }
      }
    }

    if (const textlabel_t* edge_label = ED_label(graph_edge);
        edge_label != nullptr) {
      edge_layout.label = EdgeLabelLayout{
          input_edge.label,
          {edge_label->pos.x, edge_label->pos.y},
          edge_label->dimen.x,
          edge_label->dimen.y,
      };
      result.bounds.min_x = std::min(result.bounds.min_x, edge_label->pos.x - edge_label->dimen.x / 2.0);
      result.bounds.min_y = std::min(result.bounds.min_y, edge_label->pos.y - edge_label->dimen.y / 2.0);
      result.bounds.max_x = std::max(result.bounds.max_x, edge_label->pos.x + edge_label->dimen.x / 2.0);
      result.bounds.max_y = std::max(result.bounds.max_y, edge_label->pos.y + edge_label->dimen.y / 2.0);
    }
    result.edges.push_back(std::move(edge_layout));
  }
  return result;
}

} // namespace dot_layout
