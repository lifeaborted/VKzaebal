import QtQuick
import QtQuick.Shapes

Item {
    id: root
    property string name: "home"
    property real size: 22
    property color color: "#FFFFFF"
    property real strokeWidth: 1.8

    width: size
    height: size

    readonly property bool isFilledIcon: (
        name === "heart-fill" ||
        name === "play" ||
        name === "pause" ||
        name === "prev" ||
        name === "next" ||
        name === "more" ||
        name === "dots"
    )

    readonly property var iconPaths: ({
        "home": "M3 9l9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z",
        "search": "M11 19a8 8 0 1 0 0-16 8 8 0 0 0 0 16z M21 21l-4.35-4.35",
        "radar": "M12 2a10 10 0 0 1 10 10 M12 6a6 6 0 0 1 6 6 M12 10a2 2 0 0 1 2 2 M12 22A10 10 0 0 1 2 12 M12 18a6 6 0 0 1-6-6 M12 14a2 2 0 0 1-2-2",
        "shazam": "M12 2a10 10 0 0 1 10 10 M12 6a6 6 0 0 1 6 6 M12 10a2 2 0 0 1 2 2 M12 22A10 10 0 0 1 2 12 M12 18a6 6 0 0 1-6-6 M12 14a2 2 0 0 1-2-2",
        "library": "M16 6l4 14 M12 6v14 M8 8v12 M4 4v16",
        "settings": "M12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6z M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z",
        "heart": "M19 14c1.49-1.46 3-3.21 3-5.5A5.5 5.5 0 0 0 16.5 3c-1.76 0-3 .5-4.5 2-1.5-1.5-2.74-2-4.5-2A5.5 5.5 0 0 0 2 8.5c0 2.3 1.5 4.05 3 5.5l7 7Z",
        "heart-fill": "M19 14c1.49-1.46 3-3.21 3-5.5A5.5 5.5 0 0 0 16.5 3c-1.76 0-3 .5-4.5 2-1.5-1.5-2.74-2-4.5-2A5.5 5.5 0 0 0 2 8.5c0 2.3 1.5 4.05 3 5.5l7 7Z",
        "play": "M6 4l14 8-14 8z",
        "pause": "M6 4h4v16H6z M14 4h4v16h-4z",
        "prev": "M19 20L9 12l10-8v16z M5 4v16",
        "next": "M5 4l10 8-10 8V4z M19 4v16",
        "more": "M5 12a1.8 1.8 0 1 1 0-.01 M12 12a1.8 1.8 0 1 1 0-.01 M19 12a1.8 1.8 0 1 1 0-.01",
        "dots": "M5 12a1.8 1.8 0 1 1 0-.01 M12 12a1.8 1.8 0 1 1 0-.01 M19 12a1.8 1.8 0 1 1 0-.01",
        "shuffle": "M16 3h5v5 M4 20L21 3 M21 16v5h-5 M15 15l6 6 M4 4l5 5",
        "repeat": "M17 1l4 4-4 4 M3 11V9a4 4 0 0 1 4-4h14 M7 23l-4-4 4-4 M21 13v2a4 4 0 0 1-4 4H3",
        "download": "M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4 M7 10l5 5 5-5 M12 15V3",
        "lyrics": "M9 18V5l12-2v13 M9 18a3 3 0 1 1-6 0 3 3 0 0 1 6 0z M21 16a3 3 0 1 1-6 0 3 3 0 0 1 6 0z",
        "music": "M9 18V5l12-2v13 M9 18a3 3 0 1 1-6 0 3 3 0 0 1 6 0z M21 16a3 3 0 1 1-6 0 3 3 0 0 1 6 0z",
        "sliders": "M4 21v-7 M4 10V3 M12 21v-9 M12 8V3 M20 21v-5 M20 12V3 M1 14h6 M9 8h6 M17 16h6",
        "close": "M18 6L6 18 M6 6l12 12",
        "x": "M18 6L6 18 M6 6l12 12",
        "plus": "M12 5v14 M5 12h14",
        "link": "M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71 M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71",
        "chevron-down": "M6 9l6 6 6-6",
        "arrow-left": "M19 12H5 M12 19l-7-7 7-7",
        "back": "M19 12H5 M12 19l-7-7 7-7",
        "disc": "M12 2a10 10 0 1 0 10 10A10 10 0 0 0 12 2zm0 7a3 3 0 1 0 3 3 3 3 0 0 0-3-3z",
        "globe": "M12 2a10 10 0 1 0 10 10A10 10 0 0 0 12 2zm-1 17.93A8 8 0 0 1 4.07 13H7a14 14 0 0 0 1.25 5.75A8.1 8.1 0 0 1 11 19.93zM4.07 11A8 8 0 0 1 11 4.07 14 14 0 0 0 8.25 11zm8.93-6.93A8 8 0 0 1 19.93 11H17a14 14 0 0 0-1.25-5.75A8.1 8.1 0 0 1 13 4.07zM17 13h2.93A8 8 0 0 1 13 19.93 14 14 0 0 0 15.75 13z",
        "sort": "m3 16 4 4 4-4 M7 20V4 M21 8l-4-4-4 4 M17 4v16",
        "arrow-up-right": "M7 17L17 7 M7 7h10v10",
        "gear": "M12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6z M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z",
        "check": "M20 6L9 17l-5-5",
        "trash": "M3 6h18 M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6 M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2",
        "queue": "M8 6h13 M8 12h13 M8 18h13 M3 6h.01 M3 12h.01 M3 18h.01"
    })

    Shape {
        id: shapeItem
        width: 24
        height: 24
        scale: root.size / 24
        anchors.centerIn: parent
        layer.enabled: true
        layer.samples: 4

        ShapePath {
            strokeColor: (root.name === "play" || root.name === "pause" || root.name === "more" || root.name === "dots") ? "transparent" : root.color
            strokeWidth: (root.name === "play" || root.name === "pause" || root.name === "more" || root.name === "dots") ? 0 : root.strokeWidth
            fillColor: root.isFilledIcon ? root.color : "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathSvg {
                path: root.iconPaths[root.name] || ""
            }
        }
    }
}
