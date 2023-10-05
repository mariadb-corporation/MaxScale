/*
 * Copyright (c) 2023 MariaDB plc
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
import { ciStrIncludes } from '@share/utils/helpers'

function filter(value, search) {
    return ciStrIncludes(`${value}`, search)
}

function genOrderNumberCell({ value, rowId, rowHeight, orderNumberCellWidth, y }) {
    return {
        data: {
            value,
            rowId,
            rowIdx: value - 1,
            colIdx: 0,
            width: orderNumberCellWidth,
            isOrderNumberCell: true,
        },
        height: rowHeight,
        width: orderNumberCellWidth,
        x: 0,
        y,
    }
}

function genCell({ value, x, y, colWidths, rowIdx, colIdx, rowId, rowHeight }) {
    const width = t(colWidths, `[${colIdx}]`).safeNumber
    return {
        data: {
            value,
            rowId,
            rowIdx,
            colIdx,
            width,
        },
        height: rowHeight,
        width,
        x,
        y,
    }
}

function genRowCells({
    row,
    rowIdx,
    orderNumber,
    rowHeight,
    y,
    autoId,
    showOrderNumberCell,
    orderNumberCellWidth,
    headers,
    colWidths,
    colLeftPosMap,
    search,
    searchBy,
}) {
    let rowId = undefined
    let cells = []
    let matched = false
    for (let colIdx = 0; colIdx < row.length; colIdx++) {
        if (autoId && colIdx === 0) rowId = row[colIdx]
        if (showOrderNumberCell && (autoId ? colIdx === 1 : colIdx === 0))
            cells.push(
                genOrderNumberCell({
                    value: orderNumber,
                    rowId,
                    rowHeight,
                    orderNumberCellWidth,
                    y,
                })
            )
        const header = headers[colIdx]
        if (!header.hidden) {
            const cellValue = row[colIdx]
            if (filter(cellValue, search) && searchBy.includes(header.text)) matched = true
            cells.push(
                genCell({
                    value: cellValue,
                    x: t(colLeftPosMap, `[${colIdx}]`).safeNumber,
                    y,
                    colWidths,
                    rowIdx,
                    colIdx,
                    rowId,
                    rowHeight,
                })
            )
        }
    }
    if (matched) return cells
    return []
}

/**
 * @param {object} data
 * @param {array} data.headers - table headers
 * @param {array} data.rows - table 2d rows
 * @param {boolean} data.autoId - if it's true, the first item in the row is the uuid
 * @param {boolean} data.showOrderNumberCell - conditionally push an order number cell
 * @param {number} data.rowHeight - height of the row
 * @param {number} data.orderNumberCellWidth - width of the order number cell
 * @param {array} data.colWidths - column widths
 * @param {object} data.colLeftPosMap
 * @param {string} data.search - keyword for filtering items
 * @param {array} data.searchBy - included header names for filtering
 * @returns {array} collection data for virtual-collection component
 */
function computeCollection(data) {
    const {
        headers,
        rows,
        autoId,
        showOrderNumberCell,
        rowHeight,
        orderNumberCellWidth,
        colWidths,
        colLeftPosMap,
        search,
        searchBy,
    } = data
    //TODO: Add props for sorting and grouping rows
    let collection = []
    let currentVisibleIdx = 0
    for (let i = 0; i < rows.length; i++) {
        const cells = genRowCells({
            row: rows[i],
            rowIdx: i,
            orderNumber: i + 1,
            rowHeight,
            y: rowHeight * currentVisibleIdx,
            autoId,
            showOrderNumberCell,
            orderNumberCellWidth,
            headers,
            colWidths,
            colLeftPosMap,
            search,
            searchBy,
        })
        if (cells.length) {
            collection.push({ group: cells })
            currentVisibleIdx++
        }
    }
    return collection
}

onmessage = e => postMessage(computeCollection(e.data))
