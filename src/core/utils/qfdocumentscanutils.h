/***************************************************************************
 qfdocumentscanutils.h

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

#ifndef QFDOCUMENTSCANUTILS_H
#define QFDOCUMENTSCANUTILS_H

#include "qfield_core_export.h"

#include <QObject>
#include <QPointF>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

/**
 * \ingroup core
 * \brief Document scanning helpers: find a paper page in a photo, flatten it,
 * and gather pages into a PDF. Plain Qt only, no outside libraries.
 *
 * Corner points are passed to and from QML as a list of four points with
 * x and y between 0 and 1 (relative to the photo), in the order
 * top-left, top-right, bottom-right, bottom-left.
 */
class QFIELD_CORE_EXPORT QfDocumentScanUtils : public QObject
{
    Q_OBJECT

  public:
    explicit QfDocumentScanUtils( QObject *parent = nullptr );

    //! Shortest page side, in pixels, that labs' small print usually needs to stay readable
    static constexpr int MINIMUM_READABLE_SIDE = 1500;

    /**
     * Guesses the four corners of the paper page in the photo at \a imagePath.
     * Works best with a light page on a darker surface. When no page is found it
     * returns corners set slightly in from the photo's edges, for the person to adjust.
     */
    Q_INVOKABLE static QVariantList detectPageCorners( const QString &imagePath );

    /**
     * Flattens the page outlined by \a corners in the photo at \a imagePath and saves it,
     * in colour and at full resolution, as a JPEG at \a outputPath.
     * Returns a map with "ok" (bool), "width" and "height" (pixels), "readable" (bool, the
     * short side reaches MINIMUM_READABLE_SIDE) and "error" (text, empty when ok).
     */
    Q_INVOKABLE static QVariantMap flattenPage( const QString &imagePath, const QVariantList &corners, const QString &outputPath );

    /**
     * Writes the page images in \a imagePaths, in order, as one PDF at \a outputPath,
     * one page per image, at the images' full resolution. Returns TRUE on success.
     */
    Q_INVOKABLE static bool writePdf( const QStringList &imagePaths, const QString &outputPath );

    /**
     * Returns the capture mode set for \a fieldName in the layer's custom property
     * "scout/capture_mode/<field name>": "scan" or "photo" ("photo" when not set).
     */
    Q_INVOKABLE static QString captureMode( QObject *layer, const QString &fieldName );
};

#endif // QFDOCUMENTSCANUTILS_H
