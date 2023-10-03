/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { t } from 'typy'

function computeCollection(data) {
    const {
        tableHeaders,
        cells,
        autoId,
        showOrderNumberHeader,
        rowHeight,
        orderNumberHeaderWidth,
        headerWidthMap,
        colLeftPosMap,
    } = data
    const headersLength = tableHeaders.length
    let rowIdx = 0
    let uuid = undefined
    return cells.reduce((acc, cell, i) => {
        // cells of a row are pushed to group
        if (t(acc[rowIdx], 'group').isUndefined) acc.push({ group: [] })
        const headerIdx = i % headersLength

        // get uuid of a row
        if (autoId && headerIdx === 0) uuid = cells[i]

        // Push order number cell
        if (showOrderNumberHeader && (autoId ? headerIdx === 1 : headerIdx === 0)) {
            acc[rowIdx].group.push({
                data: { value: rowIdx + 1, uuid, rowIdx, isOrderNumberCol: true },
                height: rowHeight,
                width: orderNumberHeaderWidth,
                x: 0,
                y: rowHeight * rowIdx,
            })
        }

        // Push visible cells to group
        const width = t(headerWidthMap, `[${headerIdx}]`).safeNumber
        if (!t(tableHeaders, `[${headerIdx}].hidden`).safeBoolean)
            acc[rowIdx].group.push({
                data: { value: cell, uuid, rowIdx, colIdx: headerIdx },
                height: rowHeight,
                width,
                x: colLeftPosMap[headerIdx],
                y: rowHeight * rowIdx,
            })
        if (headerIdx === headersLength - 1) rowIdx++
        return acc
    }, [])
}

onmessage = e => {
    const { action, data } = e.data
    switch (action) {
        case 'compute-collection':
            postMessage(computeCollection(data))
            break
        //TODO: Add action to filter, group rows
    }
}
