/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import chai, { expect } from 'chai'
import mount from '@tests/unit/setup'
import DetailsReadonlyTable from '@/components/common/DetailsPage/DetailsReadonlyTable'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'

chai.should()
chai.use(sinonChai)

const expectTableHeaders = [
    { text: 'Variable', value: 'id', width: '65%' },
    { text: 'Value', value: 'value', width: '35%' },
]

const dummy_data = {
    active_operations: 0,
    adaptive_avg_select_time: '0ns',
    connections: 0,
    max_connections: 0,
    persistent_connections: 0,
    routed_packets: 0,
    total_connections: 0,
}

describe('DetailsReadonlyTable.vue', () => {
    let wrapper, processTableRowsSpy
    before(() => {
        // spy on processTableRows before mounting occurs
        processTableRowsSpy = sinon.spy(DetailsReadonlyTable.methods, 'processTableRows')
        wrapper = mount({
            shallow: false,
            component: DetailsReadonlyTable,
            props: {
                loading: false,
                title: 'STATISTICS',
                objData: dummy_data,
            },
            computed: {
                search_keyword: () => '',
            },
        })
    })

    it(`Should call processTableRows once time when component is mounted`, () => {
        processTableRowsSpy.should.have.been.calledOnce
    })

    it(`Should call processTableRows when objData props changes`, async () => {
        // spy on processTableRows after mounting occurs
        const processTableRowsSpy = sinon.spy(wrapper.vm, 'processTableRows')
        // mockup props changes
        const objData = wrapper.vm.$help.lodash.cloneDeep(wrapper.vm.$props.objData)
        objData.connections = 100
        await wrapper.setProps({
            objData: objData,
        })
        processTableRowsSpy.should.have.been.calledOnce
    })

    it(`Should show table by default`, async () => {
        expect(wrapper.vm.$data.showTable).to.be.true
    })

    it(`Should render table headers having 'Variable' and 'Value' columns with
      with as 65%, 35%, respectively`, async () => {
        expect(wrapper.vm.$data.tableHeaders).to.be.deep.equals(expectTableHeaders)
    })

    it(`Should pass necessary props to data-table`, async () => {
        const dataTable = wrapper.findComponent({
            name: 'data-table',
        })
        expect(dataTable.exists()).to.be.true
        const {
            search,
            headers,
            data,
            loading,
            tdBorderLeft,
            showAll,
            isTree,
        } = dataTable.vm.$props

        const {
            search_keyword,
            isLoading,
            $data: { tableHeaders, tableRows },
            $props: { isTree: isTreeProps },
        } = wrapper.vm

        expect(search).to.be.equals(search_keyword)
        expect(headers).to.be.deep.equals(tableHeaders)
        expect(data).to.be.deep.equals(tableRows)
        expect(loading).to.be.equals(isLoading)
        expect(tdBorderLeft).to.be.true
        expect(showAll).to.be.true
        expect(isTree).to.be.equals(isTreeProps)
    })
})
