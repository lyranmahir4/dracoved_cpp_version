import QtQuick 2.15
import QtLocation 6.5
import QtPositioning 6.5

Item {
    id: root
    property var lineOverlays: []
    property var bandOverlays: []
    property var markerOverlays: []

    Map {
        id: map
        anchors.fill: parent
        plugin: Plugin {
            name: "osm"
        }
        center: QtPositioning.coordinate(0, 0)
        zoomLevel: 2
        minimumZoomLevel: 1
        maximumZoomLevel: 10

        Repeater {
            model: root.bandOverlays
            delegate: MapPolygon {
                path: modelData.path
                color: modelData.fillColor
                border.color: modelData.borderColor
                border.width: modelData.borderWidth
            }
        }

        Repeater {
            model: root.lineOverlays
            delegate: MapPolyline {
                path: modelData.path
                line.width: modelData.width
                line.color: modelData.color
            }
        }
    }
}
