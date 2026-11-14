import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.qfield.core
import org.qfield.gui

/**
 * \ingroup qml_gui
 * \brief Document scanner (Boon Agriculture, Scout).
 *
 * Shows the photo of a paper page with four corner handles placed on the page's edges
 * (found automatically, adjustable by dragging). Each accepted page is flattened at full
 * resolution and in colour. One page is returned as a JPG, several pages as one PDF.
 *
 * The caller takes the photos: it calls addPhoto(path) for each one, and takes another
 * photo when photoRequested(retake) is emitted.
 */
Popup {
  id: scanner

  //! Emitted with the final file (JPG for one page, PDF for several)
  signal finished(string path)
  //! Emitted when the person stops without keeping anything
  signal canceled
  //! Emitted when another photo is wanted; retake is TRUE when it replaces the current one
  signal photoRequested(bool retake)

  //! Full path, without extension, for the result and its working files
  property string outputBasePath: ""

  property string currentPhoto: ""
  property var corners: []
  property var pages: []
  property var sourcePhotos: []
  property bool busy: false

  parent: mainWindow.contentItem
  x: 0
  y: 0
  width: mainWindow.width
  height: mainWindow.height
  z: 10000
  padding: 0
  modal: true
  closePolicy: Popup.CloseOnEscape

  background: Rectangle {
    color: "#000000"
  }

  function addPhoto(path) {
    currentPhoto = path;
    sourcePhotos.push(path);
    corners = QfDocumentScanUtils.detectPageCorners(path);
    if (!opened) {
      open();
    }
  }

  function keepCurrentPage() {
    if (currentPhoto === "") {
      return true;
    }
    const pagePath = outputBasePath + "_page" + (pages.length + 1) + ".jpg";
    const result = QfDocumentScanUtils.flattenPage(currentPhoto, corners, pagePath);
    if (!result.ok) {
      mainWindow.displayToast(qsTr("The page could not be flattened: %1").arg(result.error), "error");
      return false;
    }
    if (!result.readable) {
      mainWindow.displayToast(qsTr("This page is small (%1 × %2 pixels) and may be hard to read. Move closer or retake it.").arg(result.width).arg(result.height), "warning");
    }
    pages.push(pagePath);
    currentPhoto = "";
    return true;
  }

  function finish() {
    if (!keepCurrentPage()) {
      return;
    }
    busy = true;
    let resultPath = "";
    if (pages.length === 1) {
      resultPath = outputBasePath + ".jpg";
      platformUtilities.renameFile(pages[0], resultPath);
    } else if (pages.length > 1) {
      resultPath = outputBasePath + ".pdf";
      if (!QfDocumentScanUtils.writePdf(pages, resultPath)) {
        busy = false;
        mainWindow.displayToast(qsTr("The pages could not be saved as a PDF"), "error");
        return;
      }
      QfFileUtils.deleteFiles(pages);
    }
    QfFileUtils.deleteFiles(sourcePhotos);
    busy = false;
    reset();
    close();
    if (resultPath !== "") {
      finished(resultPath);
    }
  }

  function cancelAll() {
    QfFileUtils.deleteFiles(pages.concat(sourcePhotos));
    reset();
    close();
    canceled();
  }

  function reset() {
    currentPhoto = "";
    corners = [];
    pages = [];
    sourcePhotos = [];
  }

  function setCorner(index, nx, ny) {
    const updated = corners.slice();
    updated[index] = Qt.point(Math.max(0, Math.min(1, nx)), Math.max(0, Math.min(1, ny)));
    corners = updated;
  }

  Item {
    id: photoArea
    anchors.top: parent.top
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.bottom: controls.top
    anchors.topMargin: mainWindow.sceneTopMargin + 8
    anchors.margins: 24

    Image {
      id: photo
      anchors.fill: parent
      source: scanner.currentPhoto !== "" ? QfUrlUtils.fromString(scanner.currentPhoto) : ""
      fillMode: Image.PreserveAspectFit
      autoTransform: true
      asynchronous: true
      cache: false
      sourceSize.width: 1600
      sourceSize.height: 1600
    }

    // Area actually covered by the photo inside the Image item
    readonly property real paintedX: (photo.width - photo.paintedWidth) / 2
    readonly property real paintedY: (photo.height - photo.paintedHeight) / 2

    Canvas {
      id: outline
      anchors.fill: parent
      visible: scanner.corners.length === 4 && photo.status === Image.Ready
      onVisibleChanged: requestPaint()
      onPaint: {
        const ctx = getContext("2d");
        ctx.reset();
        if (scanner.corners.length !== 4) {
          return;
        }
        ctx.lineWidth = 3;
        ctx.strokeStyle = QfTheme.mainColor;
        ctx.fillStyle = Qt.rgba(1, 1, 1, 0.12);
        ctx.beginPath();
        for (let i = 0; i < 4; i++) {
          const px = photoArea.paintedX + scanner.corners[i].x * photo.paintedWidth;
          const py = photoArea.paintedY + scanner.corners[i].y * photo.paintedHeight;
          if (i === 0) {
            ctx.moveTo(px, py);
          } else {
            ctx.lineTo(px, py);
          }
        }
        ctx.closePath();
        ctx.fill();
        ctx.stroke();
      }
      Connections {
        target: scanner
        function onCornersChanged() {
          outline.requestPaint();
        }
      }
      Connections {
        target: photo
        function onPaintedWidthChanged() {
          outline.requestPaint();
        }
        function onPaintedHeightChanged() {
          outline.requestPaint();
        }
      }
    }

    Repeater {
      model: scanner.corners.length === 4 && photo.status === Image.Ready ? 4 : 0

      Rectangle {
        id: handle
        required property int index
        width: 44
        height: 44
        radius: 22
        color: Qt.rgba(1, 1, 1, 0.25)
        border.color: QfTheme.mainColor
        border.width: 3
        x: photoArea.paintedX + scanner.corners[index].x * photo.paintedWidth - width / 2
        y: photoArea.paintedY + scanner.corners[index].y * photo.paintedHeight - height / 2

        DragHandler {
          id: dragHandler
          target: null
          property point startCorner
          onActiveChanged: {
            if (active) {
              startCorner = Qt.point(scanner.corners[handle.index].x, scanner.corners[handle.index].y);
            }
          }
          onTranslationChanged: {
            if (active && photo.paintedWidth > 0 && photo.paintedHeight > 0) {
              scanner.setCorner(handle.index, startCorner.x + translation.x / photo.paintedWidth, startCorner.y + translation.y / photo.paintedHeight);
            }
          }
        }
      }
    }

    BusyIndicator {
      anchors.centerIn: parent
      running: scanner.busy || photo.status === Image.Loading
    }
  }

  ColumnLayout {
    id: controls
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.bottom: parent.bottom
    anchors.margins: 12
    anchors.bottomMargin: mainWindow.sceneBottomMargin + 12
    spacing: 8

    Label {
      Layout.fillWidth: true
      horizontalAlignment: Text.AlignHCenter
      wrapMode: Text.WordWrap
      color: "#ffffff"
      font: QfTheme.tipFont
      text: qsTr("Drag the corners onto the page's edges. Page %1").arg(scanner.pages.length + 1)
    }

    RowLayout {
      Layout.fillWidth: true
      spacing: 8

      QfButton {
        Layout.fillWidth: true
        text: qsTr("Cancel")
        bgcolor: "transparent"
        enabled: !scanner.busy
        onClicked: scanner.cancelAll()
      }

      QfButton {
        Layout.fillWidth: true
        text: qsTr("Retake")
        enabled: !scanner.busy
        onClicked: {
          scanner.currentPhoto = "";
          scanner.photoRequested(true);
        }
      }

      QfButton {
        Layout.fillWidth: true
        text: qsTr("Add page")
        enabled: !scanner.busy && scanner.currentPhoto !== ""
        onClicked: {
          if (scanner.keepCurrentPage()) {
            scanner.photoRequested(false);
          }
        }
      }

      QfButton {
        Layout.fillWidth: true
        text: qsTr("Done")
        enabled: !scanner.busy && (scanner.currentPhoto !== "" || scanner.pages.length > 0)
        onClicked: scanner.finish()
      }
    }
  }
}
