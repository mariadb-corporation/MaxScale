import chai, { expect } from 'chai'
import mount from '@tests/unit/setup'
import Listeners from '@/pages/Dashboard/Listeners'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'
import { mockupAllListeners } from '@tests/unit/mockup'

chai.should()
chai.use(sinonChai)

const expectedTableHeaders = [
    { text: 'Listener', value: 'id' },
    { text: 'Port', value: 'port' },
    { text: 'Host', value: 'address' },
    { text: 'State', value: 'state' },
    { text: 'Service', value: 'serviceIds' },
]

const expectedTableRows = [
    {
        id: 'RCR-Writer-Listener',
        port: 3308,
        address: '::',
        state: 'Running',
        serviceIds: ['RCR-Writer'],
    },
    {
        id: 'RWS-Listener',
        port: 3306,
        address: '::',
        state: 'Running',
        serviceIds: ['RWS-Router'],
    },
    {
        id: 'RCR-Router-Listener',
        port: 3307,
        address: '::',
        state: 'Running',
        serviceIds: ['RCR-Router'],
    },
]

describe('Dashboard Listeners tab', () => {
    let wrapper, axiosStub

    after(async () => {
        await axiosStub.reset()
    })

    beforeEach(async () => {
        wrapper = mount({
            shallow: false,
            component: Listeners,
            computed: {
                all_listeners: () => mockupAllListeners,
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
        const listenerId = mockupAllListeners[0].id
        const cellIndex = expectedTableHeaders.length - 1
        const serviceId = mockupAllListeners[0].relationships.services.data[0].id
        let tableCell = dataTable.find(`.${listenerId}-cell-${cellIndex}`)
        let aTag = tableCell.find('a')
        await aTag.trigger('click')
        expect(wrapper.vm.$route.path).to.be.equals(`/dashboard/services/${serviceId}`)
    })
})
