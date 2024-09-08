/* Copyright (c) 2024 Otto Link. Distributed under the terms of the GNU General
 * Public License. The full license is in the file LICENSE, distributed with
 * this software. */
#include <QApplication>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>

#include "gnodegui/graphics_node.hpp"
#include "gnodegui/logger.hpp"
#include "gnodegui/style.hpp"

namespace gngui
{

GraphicsNode::GraphicsNode(NodeProxy *p_node_proxy, QGraphicsItem *parent)
    : QGraphicsRectItem(parent), p_node_proxy(p_node_proxy)
{
  this->setFlag(QGraphicsItem::ItemIsSelectable, true);
  this->setFlag(QGraphicsItem::ItemIsMovable, true);
  this->setFlag(QGraphicsItem::ItemDoesntPropagateOpacityToChildren, true);
  this->setFlag(QGraphicsItem::ItemIsFocusable, true);
  this->setFlag(QGraphicsItem::ItemClipsChildrenToShape, false);
  this->setAcceptHoverEvents(true);
  this->setOpacity(1);
  this->setZValue(0);

  this->geometry = GraphicsNodeGeometry(this->p_node_proxy);
  this->setRect(0.f, 0.f, this->geometry.full_width, this->geometry.full_height);
  this->is_port_hovered.resize(this->p_node_proxy->get_nports());
}

int GraphicsNode::get_hovered_port_index() const
{
  auto it = std::find(this->is_port_hovered.begin(), this->is_port_hovered.end(), true);
  return (it != this->is_port_hovered.end())
             ? std::distance(this->is_port_hovered.begin(), it)
             : -1;
}

void GraphicsNode::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
  this->is_node_hovered = true;
  this->update();

  QGraphicsRectItem::hoverEnterEvent(event);
}

void GraphicsNode::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
  this->is_node_hovered = false;
  this->update();

  QGraphicsRectItem::hoverLeaveEvent(event);
}

void GraphicsNode::hoverMoveEvent(QGraphicsSceneHoverEvent *event)
{
  QPointF pos = event->pos();
  QPointF scene_pos = this->mapToScene(pos);
  QPointF item_pos = scene_pos - this->scenePos();

  if (this->update_is_port_hovered(item_pos))
    this->update();

  QGraphicsRectItem::hoverMoveEvent(event);
}

void GraphicsNode::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
  if (event->button() == Qt::LeftButton)
  {
    int hovered_port_index = this->get_hovered_port_index();

    if (hovered_port_index >= 0)
    {
      SPDLOG->trace("connection_started {}:{}",
                    this->get_proxy_ref()->get_id(),
                    hovered_port_index);

      this->has_connection_started = true;
      this->setFlag(QGraphicsItem::ItemIsMovable, false);
      this->port_index_from = hovered_port_index;
      this->data_type_connecting = this->get_proxy_ref()->get_data_type(
          hovered_port_index);
      Q_EMIT connection_started(this, hovered_port_index);
      event->accept();
    }
  }

  else if (event->button() == Qt::RightButton)
    Q_EMIT this->right_clicked(this->get_proxy_ref()->get_id(), this->scenePos());

  QGraphicsRectItem::mousePressEvent(event);
}

void GraphicsNode::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
  if (event->button() == Qt::LeftButton)
  {
    if (this->has_connection_started)
    {
      // get all items at the mouse release position (in stacking order)
      QList<QGraphicsItem *> items_under_mouse = scene()->items(event->scenePos());
      bool                   is_dropped = true;

      for (QGraphicsItem *item : items_under_mouse)
        if (GraphicsNode *target_node = dynamic_cast<GraphicsNode *>(item))
        {
          // check if the new link indeed land on a port
          int hovered_port_index = target_node->get_hovered_port_index();

          if (hovered_port_index >= 0)
          {
            SPDLOG->trace("connection_finished {}:{}",
                          target_node->get_proxy_ref()->get_id(),
                          hovered_port_index);

            Q_EMIT connection_finished(this,
                                       this->port_index_from,
                                       target_node,
                                       hovered_port_index);
            is_dropped = false;
            break;
          }
          else
          {
            is_dropped = true;
            break;
          }
        }

      this->reset_is_port_hovered();
      this->update();

      if (is_dropped)
      {
        SPDLOG->trace("GraphicsNode::mouseReleaseEvent connection_dropped {}",
                      this->get_proxy_ref()->get_id());
        Q_EMIT connection_dropped(this, this->port_index_from, event->scenePos());
      }

      this->has_connection_started = false;

      // clean-up port color state
      for (QGraphicsItem *item : this->scene()->items())
        if (GraphicsNode *node = dynamic_cast<GraphicsNode *>(item))
        {
          node->data_type_connecting = "";
          node->update();
        }

      this->data_type_connecting = "";

      this->setFlag(QGraphicsItem::ItemIsMovable, true);
    }
  }
  QGraphicsRectItem::mouseReleaseEvent(event);
}

void GraphicsNode::paint(QPainter                       *painter,
                         const QStyleOptionGraphicsItem *option,
                         QWidget                        *widget)
{
  Q_UNUSED(option);
  Q_UNUSED(widget);

  // --- Background rectangle ---
  painter->setBrush(QBrush(style.node.color_bg));
  painter->setPen(Qt::NoPen);
  painter->drawRoundedRect(this->geometry.body_rect,
                           style.node.rounding_radius,
                           style.node.rounding_radius);

  // --- Caption ---
  // Set pen based on whether the node is selected or not
  painter->setPen(this->isSelected() ? style.node.color_selected
                                     : style.node.color_caption);
  painter->drawText(this->geometry.caption_pos,
                    this->get_proxy_ref()->get_caption().c_str());

  // --- Border ---
  painter->setBrush(Qt::NoBrush);
  if (this->isSelected())
    painter->setPen(QPen(style.node.color_selected, style.node.thickness_selected));
  else if (this->is_node_hovered)
    painter->setPen(QPen(style.node.color_border_hovered, style.node.thickness_hovered));
  else
    painter->setPen(QPen(style.node.color_border, style.node.thickness_border));

  painter->drawRoundedRect(this->geometry.body_rect,
                           style.node.rounding_radius,
                           style.node.rounding_radius);

  // --- Ports ---
  for (int k = 0; k < this->p_node_proxy->get_nports(); k++)
  {
    // Set alignment based on port type (IN/OUT)
    int align_flag = (this->get_proxy_ref()->get_port_type(k) == PortType::IN)
                         ? Qt::AlignLeft
                         : Qt::AlignRight;

    // Draw port labels
    painter->setPen(Qt::white); // Assuming labels are always white
    painter->drawText(this->geometry.port_label_rects[k],
                      align_flag,
                      this->get_proxy_ref()->get_port_caption(k).c_str());

    // Port appearance when hovered or not
    if (this->is_port_hovered[k])
      painter->setPen(QPen(style.node.color_port_hovered, style.node.thickness_selected));
    else if (this->is_node_hovered)
      painter->setPen(
          QPen(style.node.color_border_hovered, style.node.thickness_hovered));
    else
      painter->setPen(QPen(style.node.color_border, style.node.thickness_border));

    // Set port brush based on data type compatibility
    std::string data_type = this->get_proxy_ref()->get_data_type(k);
    float       port_radius = style.node.port_radius;

    if (!this->data_type_connecting.empty() && data_type != this->data_type_connecting)
    {
      painter->setBrush(style.node.color_port_not_selectable);
      port_radius = style.node.port_radius_not_selectable;
    }
    else
    {
      painter->setBrush(get_color_from_data_type(data_type));
    }

    // Draw the port as a circle (ellipse with equal width and height)
    painter->drawEllipse(this->geometry.port_rects[k].center(), port_radius, port_radius);
  }
}

void GraphicsNode::reset_is_port_hovered()
{
  this->is_port_hovered.assign(this->is_port_hovered.size(), false);
}

bool GraphicsNode::sceneEventFilter(QGraphicsItem *watched, QEvent *event)
{
  if (GraphicsNode *node = dynamic_cast<GraphicsNode *>(watched))
  {

    // LOOKING FOR A PORT TO CONNECT: mouse move + connection started
    // (from node) (watched is the node at beginning of the link and
    // this the node currently being hovered and possibly the end of
    // the link)

    if (event->type() == QEvent::GraphicsSceneMouseMove && node->has_connection_started)
    {
      QGraphicsSceneMouseEvent *mouse_event = static_cast<QGraphicsSceneMouseEvent *>(
          event);

      QPointF item_pos = mouse_event->scenePos() - this->scenePos();

      // update current data type of the building connection
      if (this->data_type_connecting != node->data_type_connecting)
      {
        this->data_type_connecting = node->data_type_connecting;
        this->update();
      }

      // update hovering port status
      if (this->update_is_port_hovered(item_pos))
      {
        // if a port is hovered, check that the port type (in/out)
        // and data type are compatible with the incoming link,
        // deactivate hovering for this port
        for (int k = 0; k < this->get_proxy_ref()->get_nports(); k++)
          if (this->is_port_hovered[k])
          {
            int from_pidx = node->port_index_from;

            PortType from_ptype = node->get_proxy_ref()->get_port_type(from_pidx);
            PortType to_ptype = this->get_proxy_ref()->get_port_type(k);

            std::string from_pdata = node->get_proxy_ref()->get_data_type(from_pidx);
            std::string to_pdata = this->get_proxy_ref()->get_data_type(k);

            if (from_ptype == to_ptype || from_pdata != to_pdata)
              this->is_port_hovered[k] = false;
          }
        this->update();
      }
    }
  }

  return QGraphicsRectItem::sceneEventFilter(watched, event);
}

bool GraphicsNode::update_is_port_hovered(QPointF item_pos)
{
  // set hover state
  for (size_t k = 0; k < this->geometry.port_rects.size(); k++)
    if (this->geometry.port_rects[k].contains(item_pos))
    {
      this->is_port_hovered[k] = true;
      return true;
    }

  // if we end up here and one the flag is still true, it means we
  // just left a hovered port
  for (size_t k = 0; k < this->geometry.port_rects.size(); k++)
    if (this->is_port_hovered[k])
    {
      this->is_port_hovered[k] = false;
      return true;
    }

  return false;
}

} // namespace gngui
