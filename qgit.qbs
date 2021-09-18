import qbs

Project {
    name: "QGit"
    minimumQbsVersion: "1.5.0"
    property string minimumQtVersion: "5.11.0"
    property bool conversionWarnEnabled: true

    qbsSearchPaths: ["qbs"]

    references: [
        "src/src.qbs",
    ]
}
