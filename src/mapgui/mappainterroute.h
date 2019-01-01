/*****************************************************************************
* Copyright 2015-2019 Alexander Barthel albar965@mailbox.org
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#ifndef LITTLENAVMAP_MAPPAINTERROUTE_H
#define LITTLENAVMAP_MAPPAINTERROUTE_H

#include "mapgui/mappainter.h"

#include "geo/line.h"

namespace Marble {
class GeoDataLineString;
}

class MapWidget;
class RouteController;
class Route;

namespace proc {
struct MapProcedureLegs;

struct MapProcedureLeg;

}

namespace map {
struct MapAirport;

struct MapVor;

struct MapNdb;

struct MapWaypoint;

}

/*
 * Draws the flight plan line and all enroute navaid and departure and destination airports (airport symbols only).
 * Airport diagrams and route overview are drawn by the airport painter.
 */
class MapPainterRoute :
  public MapPainter
{
  Q_DECLARE_TR_FUNCTIONS(MapPainter)

public:
  MapPainterRoute(MapWidget *mapWidget, MapScale *mapScale, const Route *routeParam);
  virtual ~MapPainterRoute();

  virtual void render(PaintContext *context) override;

private:
  struct DrawText
  {
    /* Line for text placement */
    atools::geo::Line line;

    /* Draw distance and/or course information */
    bool distance, course;
  };

  void paintRoute(const PaintContext *context);

  void paintAirport(const PaintContext *context, int x, int y, const map::MapAirport& obj);
  void paintVor(const PaintContext *context, int x, int y, const map::MapVor& obj, bool preview);
  void paintNdb(const PaintContext *context, int x, int y, bool preview);
  void paintWaypoint(const PaintContext *context, const QColor& col, int x, int y, bool preview);
  void paintProcedure(proc::MapProcedureLeg& lastLegPoint, const PaintContext *context,
                      const proc::MapProcedureLegs& legs, int legsRouteOffset, const QColor& color, bool preview);
  void paintWaypointText(const PaintContext *context, int x, int y, const map::MapWaypoint& obj, bool drawAsRoute,
                         const QStringList *additionalText = nullptr);
  void paintNdbText(const PaintContext *context, int x, int y, const map::MapNdb& obj, bool drawAsRoute,
                    const QStringList *additionalText = nullptr);
  void paintVorText(const PaintContext *context, int x, int y, const map::MapVor& obj, bool drawAsRoute,
                    const QStringList *additionalText = nullptr);
  void paintAirportText(const PaintContext *context, int x, int y, bool drawAsRoute, const map::MapAirport& obj);
  void paintText(const PaintContext *context, const QColor& color, int x, int y, const QStringList& texts,
                 bool drawAsRoute);
  void paintUserpoint(const PaintContext *context, int x, int y, bool preview);
  void paintProcedurePoint(const PaintContext *context, int x, int y, bool preview);

  void paintProcedurePoint(proc::MapProcedureLeg& lastLegPoint, const PaintContext *context,
                           const proc::MapProcedureLegs& legs, int index, bool preview,
                           bool drawText);

  void drawSymbols(const PaintContext *context, const QBitArray& visibleStartPoints, const QList<QPointF>& startPoints,
                   bool preview);

  void drawRouteSymbolText(const PaintContext *context,
                           const QBitArray& visibleStartPoints, const QList<QPointF>& startPoints);

  void paintProcedureSegment(const PaintContext *context, const proc::MapProcedureLegs& legs,
                             int index, QVector<QLineF>& lastLines, QVector<DrawText> *drawTextLines, bool noText,
                             bool preview, bool draw);

  void paintTopOfDescentAndClimb(const PaintContext *context);

  QLineF paintProcedureTurn(QVector<QLineF>& lastLines, QLineF line, const proc::MapProcedureLeg& leg,
                            QPainter *painter, const QPointF& intersectPoint, bool draw);
  void paintProcedureBow(const proc::MapProcedureLeg *prevLeg, QVector<QLineF>& lastLines, QPainter *painter,
                         QLineF line, const proc::MapProcedureLeg& leg, const QPointF& intersectPoint, bool draw);

  /* Waypoint Underlays */
  void paintProcedureUnderlay(const PaintContext *context, const proc::MapProcedureLeg& leg, int x, int y, int size);

  void drawStartParking(const PaintContext *context);

  const Route *route;
};

#endif // LITTLENAVMAP_MAPPAINTERROUTE_H
