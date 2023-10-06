/* eslint-disable no-unused-vars */
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
import { ciStrIncludes, dateFormat, lodash } from '@share/utils/helpers'

function filter({ cellValue, search, header }) {
    let value = cellValue
    // format the value before filtering
    if (header.filterDateFormat) value = dateFormat({ value, formatType: header.filterDateFormat })
    return ciStrIncludes(`${value}`, search)
}

function genOrderNumberCell({ value, rowHeight, orderNumberCellWidth, y }) {
    return {
        data: {
            value,
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

function genCell({ value, x, y, colWidths, rowIdx, colIdx, rowHeight }) {
    const width = t(colWidths, `[${colIdx}]`).safeNumber
    return {
        data: {
            value,
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
    showOrderNumberCell,
    orderNumberCellWidth,
    headers,
    colWidths,
    colLeftPosMap,
    search,
    searchBy,
}) {
    let cells = []
    let matched = false
    for (let colIdx = 0; colIdx < row.length; colIdx++) {
        if (showOrderNumberCell && colIdx === 0)
            cells.push(
                genOrderNumberCell({
                    value: orderNumber,
                    rowHeight,
                    orderNumberCellWidth,
                    y,
                })
            )
        const header = headers[colIdx]
        if (!header.hidden) {
            const cellValue = row[colIdx]
            if (filter({ cellValue, search, header }) && searchBy.includes(header.text))
                matched = true
            cells.push(
                genCell({
                    value: cellValue,
                    x: t(colLeftPosMap, `[${colIdx}]`).safeNumber,
                    y,
                    colWidths,
                    rowIdx,
                    colIdx,
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
        showOrderNumberCell,
        rowHeight,
        orderNumberCellWidth,
        colWidths,
        colLeftPosMap,
        search,
        searchBy,
    } = data
    let collection = []
    let currentVisibleIdx = 0
    for (let i = 0; i < rows.length; i++) {
        const cells = genRowCells({
            row: rows[i],
            rowIdx: i,
            orderNumber: i + 1,
            rowHeight,
            y: rowHeight * currentVisibleIdx,
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

/**
 * Sort the provided collection, then update new y position
 * @param {object} data
 * @param {object} data.headerNamesIndexesMap
 * @param {array} data.collection - virtual-collection data
 * @param {object} data.sortOptions - width of the order number cell
 * @param {number} data.rowHeight - height of the row
 * @param {boolean} data.showOrderNumberCell
 * @returns {array} sorted collection
 */
function sortCollection({
    headerNamesIndexesMap,
    rowHeight,
    collection,
    sortOptions,
    showOrderNumberCell,
}) {
    const { sortBy, sortDesc } = sortOptions
    let colIdxToBeSorted = headerNamesIndexesMap[sortBy]
    if (showOrderNumberCell) colIdxToBeSorted++

    let sortedCollection = lodash.cloneDeep(collection).sort((a, b) => {
        const aValue = a.group[colIdxToBeSorted].data.value
        const bValue = b.group[colIdxToBeSorted].data.value
        if (sortDesc) return bValue < aValue ? -1 : 1
        return aValue > bValue ? 1 : -1
    })
    return sortedCollection.map((item, rowIdx) => ({
        group: item.group.map(cellItem => ({ ...cellItem, y: rowIdx * rowHeight })),
    }))
}
onmessage = e => {
    const { action, data } = e.data
    if (action === 'compute') postMessage(computeCollection(data))
    else if (action === 'sort') postMessage(sortCollection(data))
    //TODO: Add collection grouping
}
