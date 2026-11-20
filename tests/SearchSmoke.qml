// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.milou as Milou

Item {
    id: test
    required property var backend
    property int step: 0
    property bool completed: false
    readonly property var queries: ["2+2", "Dolphin", backend.places[0].path + "/"]

    Milou.ResultsModel {
        id: model
        queryString: test.queries[test.step]
    }
    Timer {
        id: poll
        interval: 150
        running: !test.completed
        repeat: true
        onTriggered: {
            if (model.querying || model.rowCount() === 0) return
            let matched = false
            for (let row = 0; row < model.rowCount(); ++row) {
                const text = String(model.data(model.index(row, 0), Qt.DisplayRole))
                if ((test.step === 0 && (text === "4" || text.startsWith("4 ")))
                    || (test.step === 1 && text.toLowerCase().includes("dolphin"))
                    || (test.step === 2 && text.includes(test.backend.places[0].path))) matched = true
            }
            if (!matched) return
            console.log("PASS:", test.queries[test.step], "returned", model.rowCount(), "matches")
            if (test.step === test.queries.length - 1) {
                test.completed = true
                Qt.exit(0)
            } else {
                test.step++
            }
        }
    }
    Timer {
        interval: 25000
        running: true
        onTriggered: {
            console.error("FAIL: no expected result for", test.queries[test.step])
            Qt.exit(1)
        }
    }
}
