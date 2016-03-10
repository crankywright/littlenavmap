/*****************************************************************************
* Copyright 2015-2016 Alexander Barthel albar965@mailbox.org
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

#include "mapgui/mapquery.h"

#include "sql/sqlquery.h"
#include "geo/rect.h"
#include "geo/calculations.h"
#include "coordinateconverter.h"
#include "maplayer.h"

#include <algorithm>
#include <functional>
#include <QSqlRecord>
#include <marble/GeoDataLatLonBox.h>
#include <algorithm>

using namespace Marble;
using namespace atools::sql;
using namespace atools::geo;

#define RECT_INFLATION_FACTOR 0.3
#define RECT_INFLATION_ADD 0.1

template<typename TYPE>
void insertSortedByDistance(const CoordinateConverter& conv, QList<const TYPE *>& list, int xs, int ys,
                            const TYPE *type)
{
  auto it = std::lower_bound(list.begin(), list.end(), type,
                             [ = ](const TYPE * a1, const TYPE * a2)->bool
                             {
                               int x1, y1, x2, y2;
                               conv.wToS(a1->pos, x1, y1);
                               conv.wToS(a2->pos, x2, y2);
                               return atools::geo::manhattanDistance(x1, y1, xs, ys) <
                               atools::geo::manhattanDistance(x2, y2, xs, ys);
                             });
  list.insert(it, type);
}

void insertSortedByTowerDistance(const CoordinateConverter& conv, QList<const MapAirport *>& list, int xs,
                                 int ys,
                                 const MapAirport *type)
{
  auto it = std::lower_bound(list.begin(), list.end(), type,
                             [ = ](const MapAirport * a1, const MapAirport * a2)->bool
                             {
                               int x1, y1, x2, y2;
                               conv.wToS(a1->towerCoords, x1, y1);
                               conv.wToS(a2->towerCoords, x2, y2);
                               return atools::geo::manhattanDistance(x1, y1, xs, ys) <
                               atools::geo::manhattanDistance(x2, y2, xs, ys);
                             });
  list.insert(it, type);
}

MapQuery::MapQuery(atools::sql::SqlDatabase *sqlDb)
  : db(sqlDb)
{

}

MapQuery::~MapQuery()
{
  deInitQueries();
}

void MapQuery::getNearestObjects(const CoordinateConverter& conv, int xs, int ys, int screenDistance,
                                 MapSearchResult& result)
{
  for(int i = airports.size() - 1; i >= 0; i--)
  {
    const MapAirport& ap = airports.at(i);

    int x, y;
    bool visible = conv.wToS(ap.pos, x, y);

    if(visible)
      if((atools::geo::manhattanDistance(x, y, xs, ys)) < screenDistance)
        insertSortedByDistance(conv, result.airports, xs, ys, &ap);

    visible = conv.wToS(ap.towerCoords, x, y);
    if(visible)
      if((atools::geo::manhattanDistance(x, y, xs, ys)) < screenDistance)
        insertSortedByTowerDistance(conv, result.towers, xs, ys, &ap);
  }

  for(int id : parkingCache.keys())
  {
    QList<MapParking> *parkings = parkingCache.object(id);
    for(const MapParking& p : *parkings)
    {
      int x, y;
      bool visible = conv.wToS(p.pos, x, y);

      if(visible)
        if((atools::geo::manhattanDistance(x, y, xs, ys)) < screenDistance)
          insertSortedByDistance(conv, result.parkings, xs, ys, &p);
    }
  }

  for(int id : helipadCache.keys())
  {
    QList<MapHelipad> *helipads = helipadCache.object(id);
    for(const MapHelipad& p : *helipads)
    {
      int x, y;
      bool visible = conv.wToS(p.pos, x, y);

      if(visible)
        if((atools::geo::manhattanDistance(x, y, xs, ys)) < screenDistance)
          insertSortedByDistance(conv, result.helipads, xs, ys, &p);
    }
  }
}

void MapQuery::getAirports(const Marble::GeoDataLatLonBox& rect, const MapLayer *mapLayer,
                           QList<MapAirport>& airportList)
{
  GeoDataLatLonBox cur(curRect);
  inflateRect(cur, cur.width(GeoDataCoordinates::Degree) * RECT_INFLATION_FACTOR + RECT_INFLATION_ADD);

  if(curRect.isEmpty() || !cur.contains(rect) || curMapLayer == nullptr ||
     !curMapLayer->hasSameQueryParameters(mapLayer))
  {
    airports.clear();
    curRect = rect;
    curMapLayer = mapLayer;
    qDebug() << "MapQuery airports cache miss";
  }

  switch(mapLayer->getDataSource())
  {
    case layer::ALL:
      airportQuery->bindValue(":minlength", mapLayer->getMinRunwayLength());
      fetchAirports(rect, airportQuery, airportList);
      break;
    case layer::MEDIUM:
      fetchAirports(rect, airportMediumQuery, airportList);
      break;
    case layer::LARGE:
      fetchAirports(rect, airportLargeQuery, airportList);
      break;
  }
}

void MapQuery::fetchAirports(const Marble::GeoDataLatLonBox& rect, atools::sql::SqlQuery *query,
                             QList<MapAirport>& airportList)
{
  if(!airports.isEmpty())
    airportList = airports;
  else
    for(const GeoDataLatLonBox& r : splitAtAntiMeridian(rect))
    {
      bindCoordinateRect(r, query);
      query->exec();
      while(query->next())
      {
        MapAirport a = fillMapAirport(query);
        airports.append(a);
        airportList.append(a);
      }
    }
}

MapAirport MapQuery::fillMapAirport(const atools::sql::SqlQuery *query)
{
  MapAirport ap;
  QSqlRecord rec = query->record();

  ap.id = query->value("airport_id").toInt();
  ap.ident = query->value("ident").toString();
  ap.name = query->value("name").toString();
  ap.longestRunwayLength = query->value("longest_runway_length").toInt();
  ap.longestRunwayHeading = static_cast<int>(std::roundf(query->value("longest_runway_heading").toFloat()));

  if(rec.contains("has_tower_object"))
    ap.towerCoords = Pos(query->value("tower_lonx").toFloat(), query->value("tower_laty").toFloat());

  if(rec.contains("tower_frequency"))
    ap.towerFrequency = query->value("tower_frequency").toInt();
  if(rec.contains("atis_frequency"))
    ap.atisFrequency = query->value("atis_frequency").toInt();
  if(rec.contains("awos_frequency"))
    ap.awosFrequency = query->value("awos_frequency").toInt();
  if(rec.contains("asos_frequency"))
    ap.asosFrequency = query->value("asos_frequency").toInt();
  if(rec.contains("unicom_frequency"))
    ap.unicomFrequency = query->value("unicom_frequency").toInt();

  if(rec.contains("altitude"))
    ap.altitude = static_cast<int>(std::roundf(query->value("altitude").toFloat()));

  ap.flags = getFlags(query);
  ap.magvar = query->value("mag_var").toFloat();
  ap.pos = Pos(query->value("lonx").toFloat(), query->value("laty").toFloat());
  ap.bounding = Rect(query->value("left_lonx").toFloat(), query->value("top_laty").toFloat(),
                     query->value("right_lonx").toFloat(), query->value("bottom_laty").toFloat());

  ap.valid = true;
  return ap;
}

const QList<MapRunway> *MapQuery::getRunwaysForOverview(int airportId)
{
  if(runwayOverwiewCache.contains(airportId))
    return runwayOverwiewCache.object(airportId);
  else
  {
    qDebug() << "runwaysOverwiew cache miss";
    using atools::geo::Pos;

    runwayOverviewQuery->bindValue(":airportId", airportId);
    runwayOverviewQuery->exec();

    QList<MapRunway> *rws = new QList<MapRunway>;
    while(runwayOverviewQuery->next())
    {
      MapRunway r =
      {
        runwayOverviewQuery->value("length").toInt(),
        static_cast<int>(std::roundf(runwayOverviewQuery->value("heading").toFloat())),
        0,
        0,
        0,
        QString(),
        QString(),
        QString(),
        QString(),
        false,
        false,
        Pos(runwayOverviewQuery->value("lonx").toFloat(),
            runwayOverviewQuery->value("laty").toFloat()),
        Pos(runwayOverviewQuery->value("primary_lonx").toFloat(),
            runwayOverviewQuery->value("primary_laty").toFloat()),
        Pos(runwayOverviewQuery->value("secondary_lonx").toFloat(),
            runwayOverviewQuery->value("secondary_laty").toFloat())
      };
      rws->append(r);
    }
    runwayOverwiewCache.insert(airportId, rws);
    return rws;
  }
}

const QList<MapApron> *MapQuery::getAprons(int airportId)
{
  if(apronCache.contains(airportId))
    return apronCache.object(airportId);
  else
  {
    qDebug() << "aprons cache miss";
    apronQuery->bindValue(":airportId", airportId);
    apronQuery->exec();

    QList<MapApron> *aps = new QList<MapApron>;
    while(apronQuery->next())
    {
      MapApron ap;

      ap.drawSurface = apronQuery->value("is_draw_surface").toInt() > 0;
      ap.surface = apronQuery->value("surface").toString();

      QString vertices = apronQuery->value("vertices").toString();
      QStringList vertexList = vertices.split(",");
      for(QString vertex : vertexList)
      {
        QStringList ordinates = vertex.split(" ", QString::SkipEmptyParts);

        if(ordinates.size() == 2)
          ap.vertices.append(ordinates.at(0).toFloat(), ordinates.at(1).toFloat());
      }
      aps->append(ap);
    }
    apronCache.insert(airportId, aps);
    return aps;
  }
}

const QList<MapParking> *MapQuery::getParking(int airportId)
{
  if(parkingCache.contains(airportId))
    return parkingCache.object(airportId);
  else
  {
    qDebug() << "parkings cache miss";
    parkingQuery->bindValue(":airportId", airportId);
    parkingQuery->exec();

    QList<MapParking> *ps = new QList<MapParking>;
    while(parkingQuery->next())
    {
      MapParking p;

      QString type = parkingQuery->value("type").toString();
      if(type != "VEHICLES")
      {
        p.type = type;
        p.name = parkingQuery->value("name").toString();

        p.pos = Pos(parkingQuery->value("lonx").toFloat(), parkingQuery->value("laty").toFloat());
        p.jetway = parkingQuery->value("has_jetway").toInt() > 0;
        p.number = parkingQuery->value("number").toInt();

        p.heading = static_cast<int>(std::roundf(parkingQuery->value("heading").toFloat()));
        p.radius = static_cast<int>(std::roundf(parkingQuery->value("radius").toFloat()));

        ps->append(p);
      }
    }
    parkingCache.insert(airportId, ps);
    return ps;
  }
}

const QList<MapHelipad> *MapQuery::getHelipads(int airportId)
{
  if(helipadCache.contains(airportId))
    return helipadCache.object(airportId);
  else
  {
    qDebug() << "helipads cache miss";
    helipadQuery->bindValue(":airportId", airportId);
    helipadQuery->exec();

    QList<MapHelipad> *hs = new QList<MapHelipad>;
    while(helipadQuery->next())
    {
      MapHelipad hp;

      hp.pos = Pos(helipadQuery->value("lonx").toFloat(), helipadQuery->value("laty").toFloat()),
      hp.width = helipadQuery->value("width").toInt();
      hp.length = helipadQuery->value("length").toInt();
      hp.heading = static_cast<int>(std::roundf(helipadQuery->value("heading").toFloat()));
      hp.surface = helipadQuery->value("surface").toString();
      hp.type = helipadQuery->value("type").toString();
      hp.closed = helipadQuery->value("is_closed").toInt() > 0;

      hs->append(hp);
    }
    helipadCache.insert(airportId, hs);
    return hs;
  }
}

const QList<MapTaxiPath> *MapQuery::getTaxiPaths(int airportId)
{
  if(taxipathCache.contains(airportId))
    return taxipathCache.object(airportId);
  else
  {
    qDebug() << "taxipaths cache miss";
    taxiparthQuery->bindValue(":airportId", airportId);
    taxiparthQuery->exec();

    QList<MapTaxiPath> *tps = new QList<MapTaxiPath>;
    while(taxiparthQuery->next())
    {
      MapTaxiPath tp;
      QString type = taxiparthQuery->value("type").toString();
      if(type != "RUNWAY" && type != "VEHICLE")
      {
        tp.start = Pos(taxiparthQuery->value("start_lonx").toFloat(), taxiparthQuery->value(
                         "start_laty").toFloat()),
        tp.end = Pos(taxiparthQuery->value("end_lonx").toFloat(), taxiparthQuery->value("end_laty").toFloat()),
        tp.startType = taxiparthQuery->value("start_type").toString();
        tp.endType = taxiparthQuery->value("end_type").toString();
        tp.surface = taxiparthQuery->value("surface").toString();
        tp.name = taxiparthQuery->value("name").toString();
        tp.width = taxiparthQuery->value("width").toInt();
        tp.drawSurface = taxiparthQuery->value("is_draw_surface").toInt() > 0;

        tps->append(tp);
      }
    }
    taxipathCache.insert(airportId, tps);
    return tps;
  }
}

const QList<MapRunway> *MapQuery::getRunways(int airportId)
{
  if(runwayCache.contains(airportId))
    return runwayCache.object(airportId);
  else
  {
    qDebug() << "runways cache miss";
    runwaysQuery->bindValue(":airportId", airportId);
    runwaysQuery->exec();

    QList<MapRunway> *rs = new QList<MapRunway>;
    while(runwaysQuery->next())
    {
      MapRunway r =
      {
        runwaysQuery->value("length").toInt(),
        static_cast<int>(std::roundf(runwaysQuery->value("heading").toFloat())),
        runwaysQuery->value("width").toInt(),
        runwaysQuery->value("primary_offset_threshold").toInt(),
        runwaysQuery->value("secondary_offset_threshold").toInt(),
        runwaysQuery->value("surface").toString(),
        runwaysQuery->value("primary_name").toString(),
        runwaysQuery->value("secondary_name").toString(),
        runwaysQuery->value("edge_light").toString(),
        runwaysQuery->value("primary_closed_markings").toInt() > 0,
        runwaysQuery->value("secondary_closed_markings").toInt() > 0,
        Pos(runwaysQuery->value("lonx").toFloat(), runwaysQuery->value("laty").toFloat()),
        Pos(runwaysQuery->value("primary_lonx").toFloat(),
            runwaysQuery->value("primary_laty").toFloat()),
        Pos(runwaysQuery->value("secondary_lonx").toFloat(),
            runwaysQuery->value("secondary_laty").toFloat())
      };
      rs->append(r);
    }

    // Sort to draw the hard runways last
    using namespace std::placeholders;
    std::sort(rs->begin(), rs->end(), std::bind(&MapQuery::runwayCompare, this, _1, _2));

    runwayCache.insert(airportId, rs);
    return rs;
  }
}

bool MapQuery::runwayCompare(const MapRunway& r1, const MapRunway& r2)
{
  // The value returned indicates whether the element passed as first argument is
  // considered to go before the second
  if(r1.isHard() && r2.isHard())
    return r1.length < r2.length;
  else
    return r1.isSoft() && r2.isHard();
}

int MapQuery::flag(const atools::sql::SqlQuery *query, const QString& field, MapAirportFlags flag)
{
  if(!query->record().contains(field) || query->isNull(field))
    return NONE;
  else
    return query->value(field).toInt() > 0 ? flag : NONE;
}

void MapQuery::bindCoordinateRect(const Marble::GeoDataLatLonBox& rect, atools::sql::SqlQuery *query)
{
  query->bindValue(":leftx", rect.west(GeoDataCoordinates::Degree));
  query->bindValue(":rightx", rect.east(GeoDataCoordinates::Degree));
  query->bindValue(":bottomy", rect.south(GeoDataCoordinates::Degree));
  query->bindValue(":topy", rect.north(GeoDataCoordinates::Degree));
}

QList<Marble::GeoDataLatLonBox> MapQuery::splitAtAntiMeridian(const Marble::GeoDataLatLonBox& rect)
{
  GeoDataLatLonBox newRect = rect;
  inflateRect(newRect, newRect.width(GeoDataCoordinates::Degree) * RECT_INFLATION_FACTOR + RECT_INFLATION_ADD);

  if(newRect.crossesDateLine())
  {
    GeoDataLatLonBox westOf;
    westOf.setBoundaries(newRect.north(GeoDataCoordinates::Degree),
                         newRect.south(GeoDataCoordinates::Degree),
                         180.,
                         newRect.west(GeoDataCoordinates::Degree), GeoDataCoordinates::Degree);

    GeoDataLatLonBox eastOf;
    eastOf.setBoundaries(newRect.north(GeoDataCoordinates::Degree),
                         newRect.south(GeoDataCoordinates::Degree),
                         newRect.east(GeoDataCoordinates::Degree),
                         -180., GeoDataCoordinates::Degree);

    return QList<GeoDataLatLonBox>({westOf, eastOf});
  }
  else
    return QList<GeoDataLatLonBox>({newRect});
}

void MapQuery::inflateRect(Marble::GeoDataLatLonBox& rect, double degree)
{
  rect.setWest(rect.west(GeoDataCoordinates::Degree) - degree, GeoDataCoordinates::Degree);
  rect.setEast(rect.east(GeoDataCoordinates::Degree) + degree, GeoDataCoordinates::Degree);
  rect.setSouth(rect.south(GeoDataCoordinates::Degree) - degree, GeoDataCoordinates::Degree);
  rect.setNorth(rect.north(GeoDataCoordinates::Degree) + degree, GeoDataCoordinates::Degree);
}

int MapQuery::getFlags(const atools::sql::SqlQuery *query)
{
  int flags = 0;
  flags |= flag(query, "num_helipad", HELIPORT);
  flags |= flag(query, "rating", SCENERY);
  flags |= flag(query, "has_avgas", FUEL);
  flags |= flag(query, "has_jetfuel", FUEL);
  flags |= flag(query, "tower_frequency", TOWER);
  flags |= flag(query, "is_closed", CLOSED);
  flags |= flag(query, "is_military", MIL);
  flags |= flag(query, "is_addon", ADDON);
  flags |= flag(query, "num_approach", APPR);
  flags |= flag(query, "num_runway_hard", HARD);
  flags |= flag(query, "num_runway_soft", SOFT);
  flags |= flag(query, "num_runway_water", WATER);
  flags |= flag(query, "num_runway_light", LIGHT);
  return flags;
}

void MapQuery::initQueries()
{
  deInitQueries();
  airportQuery = new SqlQuery(db);
  airportQuery->prepare(
    "select airport_id, ident, name, rating, "
    "has_avgas, has_jetfuel, has_tower_object, "
    "tower_frequency, atis_frequency, awos_frequency, asos_frequency, unicom_frequency, "
    "is_closed, is_military, is_addon,"
    "num_approach, num_runway_hard, num_runway_soft, num_runway_water, num_runway_light, num_helipad, "
    "longest_runway_length, longest_runway_heading, mag_var, "
    "tower_lonx, tower_laty, altitude, lonx, laty, left_lonx, top_laty, right_lonx, bottom_laty "
    "from airport "
    "where lonx between :leftx and :rightx and laty between :bottomy and :topy and "
    "longest_runway_length >= :minlength "
    "order by rating asc, longest_runway_length");

  airportMediumQuery = new SqlQuery(db);
  airportMediumQuery->prepare(
    "select airport_id, ident, name, rating, "
    "has_avgas, has_jetfuel, "
    "tower_frequency, "
    "is_closed, is_military, is_addon,"
    "num_runway_hard, num_runway_soft, num_runway_water, num_helipad, "
    "longest_runway_length, longest_runway_heading, mag_var, "
    "lonx, laty, left_lonx, top_laty, right_lonx, bottom_laty "
    "from airport_medium "
    "where lonx between :leftx and :rightx and laty between :bottomy and :topy "
    "order by longest_runway_length");

  airportLargeQuery = new SqlQuery(db);
  airportLargeQuery->prepare(
    "select airport_id, ident, name, rating, "
    "has_avgas, has_jetfuel, "
    "tower_frequency, "
    "is_closed, is_military, is_addon,"
    "num_runway_hard, num_runway_soft, num_runway_water, "
    "longest_runway_length, longest_runway_heading, mag_var, "
    "lonx, laty, left_lonx, top_laty, right_lonx, bottom_laty "
    "from airport_large "
    "where lonx between :leftx and :rightx and laty between :bottomy and :topy");

  runwayOverviewQuery = new SqlQuery(db);
  runwayOverviewQuery->prepare(
    "select length, heading, lonx, laty, primary_lonx, primary_laty, secondary_lonx, secondary_laty "
    "from runway where airport_id = :airportId and length > 4000");

  apronQuery = new SqlQuery(db);
  apronQuery->prepare(
    "select surface, is_draw_surface, vertices "
    "from apron where airport_id = :airportId");

  parkingQuery = new SqlQuery(db);
  parkingQuery->prepare(
    "select type, name, number, radius, heading, has_jetway, lonx, laty "
    "from parking where airport_id = :airportId");

  helipadQuery = new SqlQuery(db);
  helipadQuery->prepare(
    "select surface, type, length, width, heading, is_closed, lonx, laty "
    "from helipad where airport_id = :airportId");

  taxiparthQuery = new SqlQuery(db);
  taxiparthQuery->prepare(
    "select type, surface, width, name, is_draw_surface, start_type, end_type, "
    "start_lonx, start_laty, end_lonx, end_laty "
    "from taxi_path where airport_id = :airportId");

  runwaysQuery = new SqlQuery(db);
  runwaysQuery->prepare(
    "select length, heading, width, surface, lonx, laty, p.name as primary_name, s.name as secondary_name, "
    "edge_light, "
    "p.offset_threshold as primary_offset_threshold,  p.has_closed_markings as primary_closed_markings, "
    "s.offset_threshold as secondary_offset_threshold,  s.has_closed_markings as secondary_closed_markings,"
    "primary_lonx, primary_laty, secondary_lonx, secondary_laty "
    "from runway "
    "join runway_end p on primary_end_id = p.runway_end_id "
    "join runway_end s on secondary_end_id = s.runway_end_id "
    "where airport_id = :airportId");

}

void MapQuery::deInitQueries()
{
  delete airportQuery;
  airportQuery = nullptr;
  delete airportMediumQuery;
  airportMediumQuery = nullptr;
  delete airportLargeQuery;
  airportLargeQuery = nullptr;

  delete runwayOverviewQuery;
  runwayOverviewQuery = nullptr;
  delete apronQuery;
  apronQuery = nullptr;
  delete parkingQuery;
  parkingQuery = nullptr;
  delete helipadQuery;
  helipadQuery = nullptr;
  delete taxiparthQuery;
  taxiparthQuery = nullptr;
  delete runwaysQuery;
  runwaysQuery = nullptr;
}
