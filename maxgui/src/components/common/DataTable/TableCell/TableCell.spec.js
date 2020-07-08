/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { expect } from 'chai'
import mount from '@tests/unit/setup'
import TableCell from '@/components/common/DataTable/TableCell'

describe('TableCell.vue', () => {
    let wrapper

    // mockup parent value passing to Collapse

    beforeEach(() => {
        localStorage.clear()
        wrapper = mount({
            shallow: false,
            component: TableCell,
            props: {
                cellIndex: 0,
                rowIndex: 0,
                indexOfLastColumn: 0,
                item: {
                    id: 'RWS-Router',
                },

                header: { text: 'Service', value: 'id' },

                hasOrderNumber: false,
                editableCell: false,
                tdBorderLeft: false,
                // For displaying draggable icon
                draggable: false,
                showDragIcon: false,
                // For displaying action column slot
                indexOfHoveredRow: null,
                // For displaying rowspan table
                colsHasRowSpan: null,
                // for tree view
                isTree: false,
                hasValidChild: false,
                // for external v-menu activator
                componentId: 'test-0123i',
            },
        })
    })

    it('cell-hover and get-truncated-info events are emitted when cell is hovered', async () => {
        const td = wrapper.findAll('td')
        expect(td.length).to.equal(1)
        await wrapper.setProps({
            item: {
                value: 'Longggggggggggggggggggggggggggggg text',
            },
            header: { text: 'Value', value: 'value', cellTruncated: true },
        })
        let eventFired = 0
        wrapper.vm.$on('cell-hover', () => {
            eventFired++
        })
        // item.level > 0 || header.cellTruncated will emit get-truncated-info
        wrapper.vm.$on('get-truncated-info', () => {
            eventFired++
        })
        td.at(0).trigger('mouseenter')

        expect(eventFired).to.equal(2)
    })

    it(`When draggable props is true, drag icon will be rendered and 
      shown at the last column at hovered row`, async () => {
        await wrapper.setProps({
            draggable: true,
            /*  indexOfHoveredRow === rowIndex &&
                cellIndex === indexOfLastColumn
                will show drag icon
            */
            indexOfLastColumn: 0,
            indexOfHoveredRow: 0,
            cellIndex: 0,
            item: {
                action: '',
            },
            header: { text: '', value: 'action', sortable: false },
        })

        const dragHandle = wrapper.find('.drag-handle')
        // drag icon is rendered
        expect(dragHandle.exists()).to.be.true
        // drag icon is shown at hovered row
        expect(dragHandle.attributes().style).to.not.include('display: none')
    })

    it(`When toggle button is clicked, toggle button will be displayed at the first column 
      and emitted "toggle-node" event `, async () => {
        await wrapper.setProps({
            cellIndex: 0,
            indexOfHoveredRow: 0,
            item: {
                id: 'log_throttling',
                value: '',
                nodeId: 1,
                parentNodeId: 0,
                level: 0,
                parentNodeInfo: null,
                leaf: false,
                children: [
                    {
                        id: 'count',
                        value: 0,
                        nodeId: 2,
                        parentNodeId: 1,
                        level: 1,
                        leaf: true,
                    },
                ],
            },
            header: { text: 'Variable', value: 'id' },
        })
        let eventFired = 0
        wrapper.vm.$on('toggle-node', () => {
            eventFired++
        })

        let toggleBtn = wrapper.find('.arrow-toggle')
        expect(toggleBtn.exists()).to.be.true
        toggleBtn.trigger('click')
        expect(eventFired).to.equal(1)
    })

    it('When hover at a table row, "actions" slot is rendered at the last column ', async () => {
        await wrapper.setProps({
            indexOfLastColumn: 0,
            indexOfHoveredRow: 0,
            cellIndex: 0,
            item: {
                action: 'text',
            },
            header: { text: '', value: 'action', sortable: false },
        })
        expect(wrapper.find('.action-slot-wrapper').exists()).to.be.true
    })

    it(`Cell rowspan feature:
      - component assigned accurate td rowspan attribute value`, async () => {
        await wrapper.setProps({
            cellIndex: 0, // rendering the first cell (aka groupId)
            header: { text: `Monitor`, value: 'groupId' }, // rendering the first cell (aka groupId)
            colsHasRowSpan: 1,
            item: {
                groupId: 'Monitor',
                rowspan: 2,
                hidden: true,
                id: 'row_server_2',
                gtid: '0-1000-9',
                monitorState: 'Running',
                serverAddress: '127.0.0.1',
                serverConnections: 0,
                serverPort: 4002,
                serverState: 'Slave, Running',
                serviceIds: ['RCR-Router', 'RCR-Writer', 'RWS-Router'],
            },
        })
        const td = wrapper.findAll('td')
        expect(td.length).to.equal(1)
        expect(td.at(0).attributes().rowspan).to.equal('2')
    })
})
