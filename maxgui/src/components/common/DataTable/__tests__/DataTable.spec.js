/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Vue from 'vue'
import { expect } from 'chai'
import mount from '@tests/unit/setup'
import DataTable from '@/components/common/DataTable'

let dummy_rowspan_headers = [
    { text: `Monitor`, value: 'groupId' },
    { text: 'State', value: 'monitorState' },
    { text: 'Servers', value: 'id' },
    { text: 'Address', value: 'serverAddress' },
    { text: 'Port', value: 'serverPort' },
    { text: 'Connections', value: 'serverConnections' },
    { text: 'State', value: 'serverState' },
    { text: 'GTID', value: 'gtid' },
    { text: 'Services', value: 'serviceIds' },
]

let dummy_rowspan_data = [
    {
        id: 'row_server_2',
        serverAddress: '127.0.0.1',
        serverPort: 4002,
        serverConnections: 0,
        serverState: 'Slave, Running',
        serviceIds: ['RCR-Router', 'RCR-Writer', 'RWS-Router'],
        gtid: '0-1000-9',
        groupId: 'Monitor', // will be assigned rowspan 2 via handleDisplayRowspan
        monitorState: 'Running', //rowspan 2
    },
    {
        id: 'row_server_1',
        serverAddress: '127.0.0.1',
        serverPort: 4001,
        serverConnections: 0,
        serverState: 'Master, Running',
        serviceIds: ['RCR-Router', 'RCR-Writer', 'RWS-Router'],
        gtid: '0-1000-9',
        groupId: 'Monitor', // will be assigned rowspan 2 via handleDisplayRowspan
        monitorState: 'Running', //rowspan 2
    },
    {
        id: 'row_server_3',
        serverAddress: '127.0.0.1',
        serverPort: 4003,
        serverConnections: 0,
        serverState: 'Down',
        serviceIds: 'No services',
        gtid: null,
        groupId: 'monitor-test', // will be assigned rowspan 1 via handleDisplayRowspan
        monitorState: 'Running', //rowspan 1
    },
]

/**
 * This function mockup processing tree data
 * @return {Array} Tree data array that is used to display in data-table
 */
function mockupTreeData() {
    const DUMMY_OBJ_PARAMS = {
        root_node: {
            node_child: { grand_child: { great_grand_child: 'great_grand_child' } },
            node_child_1: 'node_child_1 value',
        },
    }
    return Vue.prototype.$help.objToTree({
        obj: DUMMY_OBJ_PARAMS,
        keepPrimitiveValue: true,
        level: 0,
    })
}

const defaultProps = {
    /*
        object in headers may have these properties:
        sortable(true || false), editableCol (true || false), align ("center || left || right"),
        cellTruncated (true || false), width (String), padding (String)

        - editableCol, align is mainly used for styling purpose which is applied in table-cell.
        - sortable is used for disable sorting on table, by default it's always true.
        - cellTruncated is a condition to emit 'get-truncated-info' event in table-cell
    */
    headers: [
        { text: 'Variable', value: 'id' },
        { text: 'Value', value: 'value' },
    ],
    data: [
        {
            id: 'Item 0',
            value: null,
        },
        {
            id: 'Item 1',
            value: undefined,
        },
        {
            id: 'Item 2',
            value: 'value of item 2',
        },
    ],
}

const mountPropsFactory = props =>
    mount({
        shallow: false,
        component: DataTable,
        props: props ? props : defaultProps,
    })

describe('DataTable.vue', () => {
    let wrapper
    beforeEach(() => {
        wrapper = mountPropsFactory()
    })

    it(`Should process data as expected when keepPrimitiveValue
      props is true or false`, async () => {
        /*
            by default keepPrimitiveValue is set to false which means
            null, undefined become a string i.e. 'null', 'undefined' respectively.
            This allows null or undefined value to be shown on the table and can be searched using global-search
            component
        */
        expect(wrapper.vm.$props.keepPrimitiveValue).to.equal(false)
        let processedData = wrapper.vm.processingData

        expect(processedData[0].value).to.equal('null')
        expect(processedData[1].value).to.equal('undefined')
        expect(processedData[0].value).to.be.a('string')
        expect(processedData[1].value).to.be.a('string')

        // this keep original value
        await wrapper.setProps({
            keepPrimitiveValue: true,
        })
        expect(wrapper.vm.$props.keepPrimitiveValue).to.equal(true)
        let oriData = wrapper.vm.processingData
        expect(oriData[0].value).to.be.a('null')
        expect(oriData[1].value).to.be.an('undefined')
    })
})

describe('DataTable.vue - Rowspan feature', () => {
    let wrapper
    beforeEach(() => {
        wrapper = mountPropsFactory({
            colsHasRowSpan: 2,
            headers: dummy_rowspan_headers,
            data: dummy_rowspan_data,
        })
    })

    it(`Should change background color of cells have same groupId if
      a cell or a rowspan cell (rowspanCell) is hovered`, async () => {
        // get all table-cell components with ref = MonitorRowspanCell
        const rowspanCells = wrapper.findAllComponents({ ref: 'MonitorRowspanCell' })
        expect(rowspanCells.length).to.equal(4)
        /*
            trigger mouseenter at cell 0 having header as groupId and value equal to 'Monitor'
            cell-hover event should be emitted, setRowspanBg is called when
            colsHasRowSpan > 0
        */
        await rowspanCells.at(0).trigger('mouseenter')

        // testing for setRowspanBg method
        const normalCell = wrapper.findAllComponents({ ref: 'MonitorCell' })
        expect(normalCell.length).to.equal(14) // 7 columns with 2 rows have same groupId
        for (let i = 0; i < normalCell.length; ++i) {
            expect(normalCell.at(i).vm.$el.style['_values']['background-color']).to.equal(
                'rgb(250, 252, 252)'
            )
        }
    })

    it(`Should emit cell-hover event and return accurate data when hovering a cell`, async () => {
        // add cb for cell-hover event
        let eventFired = 0
        wrapper.vm.$on('cell-hover', ({ item }) => {
            eventFired++
            expect(item.groupId).to.equal('Monitor')
        })

        // get all table-cell components with ref = MonitorRowspanCell
        const rowspanCells = wrapper.findAllComponents({ ref: 'MonitorRowspanCell' })
        expect(rowspanCells.length).to.equal(4)
        /*
            trigger mouseenter at cell 0 having header as groupId and value equal to 'Monitor'
            cell-hover event should be emitted, setRowspanBg is called when
            colsHasRowSpan > 0
        */
        await rowspanCells.at(0).trigger('mouseenter')

        /*  after hovering 'Monitor' cell, cb and cell-hover event have been triggered
            cell-hover event is emitted correctly
        */
        expect(eventFired).to.equal(1)
    })
})

describe('DataTable.vue - Tree data feature', () => {
    let wrapper
    beforeEach(() => {
        wrapper = mountPropsFactory({
            isTree: true,
            headers: [
                { text: 'Variable', value: 'id' },
                { text: 'Value', value: 'value' },
            ],
            data: mockupTreeData(),
        })
    })

    it(`It processes data as expected when isTree props is true.`, async () => {
        expect(wrapper.vm.$props.isTree).to.equal(true)
        expect(wrapper.vm.$data.hasValidChild).to.equal(true)
    })
    it(`Should expand all nodes by default`, async () => {
        await wrapper.vm.$nextTick(() => {
            expect(wrapper.vm.tableRows.length).to.equal(5)
            wrapper.vm.tableRows.forEach(row => {
                if (row.children) expect(row.expanded).to.equal(true)
            })
        })
    })
    describe('The collapse and expansion of node works as expected', () => {
        before(() => {
            wrapper = mountPropsFactory({
                isTree: true,
                headers: [
                    { text: 'Variable', value: 'id' },
                    { text: 'Value', value: 'value' },
                ],
                data: mockupTreeData(),
            })
        })

        it(`Should collapse root_node and its child when root_node is toggle`, async () => {
            await wrapper.vm.toggleNode(wrapper.vm.tableRows[0])
            await wrapper.vm.$nextTick(() => {
                expect(wrapper.vm.tableRows[0].expanded).to.equal(false) // root_node is collapsed
                expect(wrapper.vm.tableRows.length).to.equal(1)
                expect(wrapper.vm.tableRows[0].id).to.equal('root_node') // only root_node
            })
        })
        it(`Should expand root_node`, async () => {
            await wrapper.vm.toggleNode(wrapper.vm.tableRows[0])
            await wrapper.vm.$nextTick(() => {
                expect(wrapper.vm.tableRows[0].expanded).to.equal(true) // root_node is expanded
                expect(wrapper.vm.tableRows.length).to.equal(3)
                // tableRows should now includes tree levels i.e. 0 and 1
                let activeNodes = ['root_node', 'node_child', 'node_child_1']
                wrapper.vm.tableRows.forEach((row, i) => {
                    expect(row.id).to.equal(activeNodes[i])
                })
            })
        })
        it(`Should expand child of root_node`, async () => {
            await wrapper.vm.toggleNode(wrapper.vm.tableRows[1]) //expand node_child
            await wrapper.vm.$nextTick(() => {
                expect(wrapper.vm.tableRows[1].expanded).to.equal(true) // node_child is expanded
                expect(wrapper.vm.tableRows.length).to.equal(4)
                // tableRows should now includes tree levels i.e. 0, 1 and 2
                let activeNodes = ['root_node', 'node_child', 'grand_child', 'node_child_1']
                wrapper.vm.tableRows.forEach((row, i) => {
                    expect(row.id).to.equal(activeNodes[i])
                })
            })
        })
    })
})
