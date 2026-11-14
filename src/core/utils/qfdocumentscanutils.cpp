/***************************************************************************
 qfdocumentscanutils.cpp

 ---------------------
 begin                : 14.11.2026
 copyright            : (C) 2026 by Boon Agriculture Pvt Ltd
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qfdocumentscanutils.h"

#include <QImage>
#include <QImageReader>
#include <QLineF>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPolygonF>
#include <QTransform>
#include <QVector>
#include <qgsmaplayer.h>

#include <algorithm>
#include <cmath>

namespace
{
  // Corners slightly in from the photo's edges: the starting point when no page is found
  QVariantList defaultCorners()
  {
    const double m = 0.05;
    return { QPointF( m, m ), QPointF( 1 - m, m ), QPointF( 1 - m, 1 - m ), QPointF( m, 1 - m ) };
  }

  double polygonArea( const QVector<QPointF> &p )
  {
    double a = 0;
    for ( int i = 0; i < p.size(); ++i )
    {
      const QPointF &c = p.at( i );
      const QPointF &n = p.at( ( i + 1 ) % p.size() );
      a += c.x() * n.y() - n.x() * c.y();
    }
    return std::abs( a ) / 2.0;
  }

  // Otsu's method: the grey level that best splits the picture into dark and light
  int otsuThreshold( const QVector<int> &histogram, int total )
  {
    double sum = 0;
    for ( int i = 0; i < 256; ++i )
      sum += i * histogram[i];
    double sumBackground = 0;
    int weightBackground = 0;
    double bestVariance = -1;
    int best = 128;
    for ( int t = 0; t < 256; ++t )
    {
      weightBackground += histogram[t];
      if ( weightBackground == 0 )
        continue;
      const int weightForeground = total - weightBackground;
      if ( weightForeground == 0 )
        break;
      sumBackground += t * histogram[t];
      const double meanBackground = sumBackground / weightBackground;
      const double meanForeground = ( sum - sumBackground ) / weightForeground;
      const double variance = static_cast<double>( weightBackground ) * weightForeground * ( meanBackground - meanForeground ) * ( meanBackground - meanForeground );
      if ( variance > bestVariance )
      {
        bestVariance = variance;
        best = t;
      }
    }
    return best;
  }

  QVector<QPointF> toPixels( const QVariantList &corners, const QSize &size )
  {
    QVector<QPointF> points;
    for ( const QVariant &v : corners )
    {
      QPointF p = v.toPointF();
      if ( v.typeId() == QMetaType::QVariantMap )
      {
        const QVariantMap m = v.toMap();
        p = QPointF( m.value( QStringLiteral( "x" ) ).toDouble(), m.value( QStringLiteral( "y" ) ).toDouble() );
      }
      points << QPointF( std::clamp( p.x(), 0.0, 1.0 ) * size.width(), std::clamp( p.y(), 0.0, 1.0 ) * size.height() );
    }
    return points;
  }
} // namespace

QfDocumentScanUtils::QfDocumentScanUtils( QObject *parent )
  : QObject( parent )
{
}

QVariantList QfDocumentScanUtils::detectPageCorners( const QString &imagePath )
{
  QImageReader reader( imagePath );
  reader.setAutoTransform( true );
  QSize size = reader.size();
  if ( !size.isValid() )
    return defaultCorners();

  // Work on a small copy: fast, and the shrinking smooths out paper texture and print
  const int maxSide = 640;
  if ( reader.transformation() & QImageIOHandler::TransformationRotate90 )
    size.transpose();
  QSize small = size.scaled( maxSide, maxSide, Qt::KeepAspectRatio );
  if ( reader.transformation() & QImageIOHandler::TransformationRotate90 )
    reader.setScaledSize( small.transposed() );
  else
    reader.setScaledSize( small );

  QImage image = reader.read();
  if ( image.isNull() )
    return defaultCorners();
  image = image.convertToFormat( QImage::Format_Grayscale8 );
  const int w = image.width();
  const int h = image.height();

  QVector<int> histogram( 256, 0 );
  for ( int y = 0; y < h; ++y )
  {
    const uchar *line = image.constScanLine( y );
    for ( int x = 0; x < w; ++x )
      histogram[line[x]]++;
  }
  const int threshold = otsuThreshold( histogram, w * h );

  // Light pixels are page candidates; keep only the largest connected light area
  QVector<int> label( w * h, 0 );
  int bestLabel = 0;
  int bestCount = 0;
  int nextLabel = 0;
  QVector<int> stack;
  for ( int y = 0; y < h; ++y )
  {
    for ( int x = 0; x < w; ++x )
    {
      const int idx = y * w + x;
      if ( label[idx] != 0 || image.constScanLine( y )[x] <= threshold )
        continue;
      ++nextLabel;
      int count = 0;
      stack.clear();
      stack << idx;
      label[idx] = nextLabel;
      while ( !stack.isEmpty() )
      {
        const int i = stack.takeLast();
        ++count;
        const int cx = i % w;
        const int cy = i / w;
        const int neighbours[4][2] = { { cx - 1, cy }, { cx + 1, cy }, { cx, cy - 1 }, { cx, cy + 1 } };
        for ( const auto &n : neighbours )
        {
          if ( n[0] < 0 || n[1] < 0 || n[0] >= w || n[1] >= h )
            continue;
          const int ni = n[1] * w + n[0];
          if ( label[ni] == 0 && image.constScanLine( n[1] )[n[0]] > threshold )
          {
            label[ni] = nextLabel;
            stack << ni;
          }
        }
      }
      if ( count > bestCount )
      {
        bestCount = count;
        bestLabel = nextLabel;
      }
    }
  }

  if ( bestLabel == 0 || bestCount < 0.15 * w * h )
    return defaultCorners();

  // The page's corners are the extreme points of the light area along the two diagonals
  QPointF topLeft, topRight, bottomRight, bottomLeft;
  double minSum = 1e9, maxSum = -1e9, minDiff = 1e9, maxDiff = -1e9;
  for ( int y = 0; y < h; ++y )
  {
    for ( int x = 0; x < w; ++x )
    {
      if ( label[y * w + x] != bestLabel )
        continue;
      const double s = x + y;
      const double d = x - y;
      if ( s < minSum )
      {
        minSum = s;
        topLeft = QPointF( x, y );
      }
      if ( s > maxSum )
      {
        maxSum = s;
        bottomRight = QPointF( x, y );
      }
      if ( d > maxDiff )
      {
        maxDiff = d;
        topRight = QPointF( x, y );
      }
      if ( d < minDiff )
      {
        minDiff = d;
        bottomLeft = QPointF( x, y );
      }
    }
  }

  const QVector<QPointF> quad { topLeft, topRight, bottomRight, bottomLeft };
  if ( polygonArea( quad ) < 0.15 * w * h )
    return defaultCorners();

  QVariantList result;
  for ( const QPointF &p : quad )
    result << QPointF( ( p.x() + 0.5 ) / w, ( p.y() + 0.5 ) / h );
  return result;
}

QVariantMap QfDocumentScanUtils::flattenPage( const QString &imagePath, const QVariantList &corners, const QString &outputPath )
{
  QVariantMap result;
  result[QStringLiteral( "ok" )] = false;

  if ( corners.size() != 4 )
  {
    result[QStringLiteral( "error" )] = tr( "Four corners are needed" );
    return result;
  }

  QImageReader reader( imagePath );
  reader.setAutoTransform( true );
  const QImage source = reader.read();
  if ( source.isNull() )
  {
    result[QStringLiteral( "error" )] = tr( "The photo could not be read" );
    return result;
  }

  const QVector<QPointF> p = toPixels( corners, source.size() );
  const double width = std::max( QLineF( p[0], p[1] ).length(), QLineF( p[3], p[2] ).length() );
  const double height = std::max( QLineF( p[0], p[3] ).length(), QLineF( p[1], p[2] ).length() );
  const int outWidth = std::clamp( static_cast<int>( std::round( width ) ), 1, source.width() * 2 );
  const int outHeight = std::clamp( static_cast<int>( std::round( height ) ), 1, source.height() * 2 );

  const QPolygonF sourceQuad( { p[0], p[1], p[2], p[3] } );
  const QPolygonF targetQuad( { QPointF( 0, 0 ), QPointF( outWidth, 0 ), QPointF( outWidth, outHeight ), QPointF( 0, outHeight ) } );
  QTransform transform;
  if ( !QTransform::quadToQuad( sourceQuad, targetQuad, transform ) )
  {
    result[QStringLiteral( "error" )] = tr( "The corners do not form a page shape" );
    return result;
  }

  QImage page( outWidth, outHeight, QImage::Format_RGB32 );
  page.fill( Qt::white );
  {
    QPainter painter( &page );
    painter.setRenderHint( QPainter::SmoothPixmapTransform, true );
    painter.setRenderHint( QPainter::Antialiasing, true );
    painter.setTransform( transform );
    painter.drawImage( QPointF( 0, 0 ), source );
  }

  if ( !page.save( outputPath, "JPG", 92 ) )
  {
    result[QStringLiteral( "error" )] = tr( "The page could not be saved" );
    return result;
  }

  result[QStringLiteral( "ok" )] = true;
  result[QStringLiteral( "width" )] = outWidth;
  result[QStringLiteral( "height" )] = outHeight;
  result[QStringLiteral( "readable" )] = std::min( outWidth, outHeight ) >= MINIMUM_READABLE_SIDE;
  result[QStringLiteral( "error" )] = QString();
  return result;
}

bool QfDocumentScanUtils::writePdf( const QStringList &imagePaths, const QString &outputPath )
{
  if ( imagePaths.isEmpty() )
    return false;

  const int dpi = 200;
  QPdfWriter writer( outputPath );
  writer.setCreator( QStringLiteral( "Scout document scan" ) );
  writer.setResolution( dpi );
  writer.setPageMargins( QMarginsF( 0, 0, 0, 0 ) );

  QPainter painter;
  bool first = true;
  for ( const QString &path : imagePaths )
  {
    QImageReader reader( path );
    reader.setAutoTransform( true );
    const QImage image = reader.read();
    if ( image.isNull() )
      return false;

    const QPageSize pageSize( QSizeF( image.width() / static_cast<double>( dpi ), image.height() / static_cast<double>( dpi ) ), QPageSize::Inch, QString(), QPageSize::ExactMatch );
    writer.setPageSize( pageSize );
    if ( first )
    {
      if ( !painter.begin( &writer ) )
        return false;
      first = false;
    }
    else
    {
      writer.newPage();
    }
    painter.drawImage( QRect( 0, 0, writer.width(), writer.height() ), image );
  }
  painter.end();
  return true;
}

QString QfDocumentScanUtils::captureMode( QObject *layer, const QString &fieldName )
{
  QgsMapLayer *mapLayer = qobject_cast<QgsMapLayer *>( layer );
  if ( !mapLayer || fieldName.isEmpty() )
    return QStringLiteral( "photo" );
  const QString mode = mapLayer->customProperty( QStringLiteral( "scout/capture_mode/%1" ).arg( fieldName ) ).toString().trimmed().toLower();
  return mode == QLatin1String( "scan" ) ? QStringLiteral( "scan" ) : QStringLiteral( "photo" );
}
