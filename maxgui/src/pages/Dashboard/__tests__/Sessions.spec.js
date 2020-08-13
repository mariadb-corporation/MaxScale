/* eslint-disable no-unused-vars */
import chai, { expect } from 'chai'
import mount from '@tests/unit/setup'
import Sessions from '@/pages/Dashboard/Sessions'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'
import { mockupAllSessions } from '@tests/unit/mockup'

chai.should()
chai.use(sinonChai)

const expectedTableHeaders = [
    { text: 'ID', value: 'id' },
    { text: 'Client', value: 'user' },
    { text: 'Connected', value: 'connected' },
    { text: 'IDLE (s)', value: 'idle' },
    { text: 'Service', value: 'serviceIds' },
]

const expectedTableRows = [
    {
        id: '1000002',
        user: 'maxskysql@::ffff:127.0.0.1',
        connected: 'Thu Aug 13 14:06:17 2020',
        idle: 55.5,
        serviceIds: ['RCR-Router'],
    },
]

describe('Dashboard Sessions tab', () => {
    let wrapper, axiosStub

    after(async () => {
        await axiosStub.reset()
    })

    beforeEach(async () => {
        wrapper = mount({
            shallow: false,
            component: Sessions,
            computed: {
                all_sessions: () => mockupAllSessions,
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
        const sessionId = mockupAllSessions[0].id
        const cellIndex = expectedTableHeaders.length - 1
        const serviceId = mockupAllSessions[0].relationships.services.data[0].id
        let tableCell = dataTable.find(`.cell-${cellIndex}-${sessionId}`)
        let aTag = tableCell.find('a')
        await aTag.trigger('click')
        expect(wrapper.vm.$route.path).to.be.equals(`/dashboard/services/${serviceId}`)
    })
})
