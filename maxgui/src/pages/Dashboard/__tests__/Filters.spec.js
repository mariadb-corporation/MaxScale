import chai, { expect } from 'chai'
import mount from '@tests/unit/setup'
import Filters from '@/pages/Dashboard/Filters'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'
import { mockupAllFilters } from '@tests/unit/mockup'

chai.should()
chai.use(sinonChai)

const expectedTableHeaders = [
    { text: 'Filter', value: 'id' },
    { text: 'Service', value: 'serviceIds' },
    { text: 'Module', value: 'module' },
]

const expectedTableRows = [
    {
        id: 'filter_0',
        serviceIds: ['RCR-Router', 'RCR-Writer'],
        module: 'qlafilter',
    },
    {
        id: 'filter_1',
        serviceIds: ['RCR-Writer'],
        module: 'binlogfilter',
    },
]

describe('Dashboard Filters tab', () => {
    let wrapper, axiosStub

    after(async () => {
        await axiosStub.reset()
    })

    beforeEach(async () => {
        wrapper = mount({
            shallow: false,
            component: Filters,
            computed: {
                all_filters: () => mockupAllFilters,
            },
        })
        axiosStub = sinon.stub(wrapper.vm.$axios, 'get').resolves(
            Promise.resolve({
                data: {},
            })
        )
    })

    afterEach(async function() {
        await axiosStub.restore()
    })

    it(`Should process table rows accurately`, async () => {
        expect(wrapper.vm.tableRows).to.be.deep.equals(expectedTableRows)
    })

    it(`Should pass expected table headers to data-table`, async () => {
        const dataTable = wrapper.findComponent({ name: 'data-table' })
        expect(wrapper.vm.tableHeaders).to.be.deep.equals(expectedTableHeaders)
        expect(dataTable.vm.$props.headers).to.be.deep.equals(expectedTableHeaders)
    })

    it(`Should navigate to service detail page when a service is clicked`, async () => {
        const dataTable = wrapper.findComponent({ name: 'data-table' })
        const listenerId = mockupAllFilters[1].id
        const cellIndex = expectedTableHeaders.findIndex(item => item.value === 'serviceIds')
        const serviceId = mockupAllFilters[1].relationships.services.data[0].id
        let tableCell = dataTable.find(`.cell-${cellIndex}-${listenerId}`)
        let aTag = tableCell.find('a')
        await aTag.trigger('click')
        expect(wrapper.vm.$route.path).to.be.equals(`/dashboard/services/${serviceId}`)
    })
})
