import chai, { expect } from 'chai'
import mount from '@tests/unit/setup'
import Services from '@/pages/Dashboard/Services'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'
import { mockupAllServices } from '@tests/unit/mockup'

chai.should()
chai.use(sinonChai)

const expectedTableHeaders = [
    { text: 'Service', value: 'id' },
    { text: 'State', value: 'state' },
    { text: 'Router', value: 'router' },
    { text: 'Current Sessions', value: 'connections' },
    { text: 'Total Sessions', value: 'total_connections' },
    { text: 'Servers', value: 'serverIds' },
]

const expectedTableRows = [
    {
        id: 'service_0',
        state: 'Started',
        router: 'readconnroute',
        connections: 0,
        total_connections: 1000001,
        serverIds: ['row_server_0'],
    },
    {
        id: 'service_1',
        state: 'Started',
        router: 'readwritesplit',
        connections: 0,
        total_connections: 0,
        serverIds: 'No servers',
    },
]

describe('Dashboard Services tab', () => {
    let wrapper, axiosStub

    after(async () => {
        await axiosStub.reset()
    })

    beforeEach(async () => {
        wrapper = mount({
            shallow: false,
            component: Services,
            computed: {
                all_services: () => mockupAllServices,
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
        const serviceId = mockupAllServices[0].id
        const cellIndex = 0
        let tableCell = dataTable.find(`.${serviceId}-cell-${cellIndex}`)
        let aTag = tableCell.find('a')
        await aTag.trigger('click')
        expect(wrapper.vm.$route.path).to.be.equals(`/dashboard/services/${serviceId}`)
    })

    it(`Should navigate to server detail page when a server is clicked`, async () => {
        const dataTable = wrapper.findComponent({ name: 'data-table' })
        const serviceId = mockupAllServices[0].id
        const cellIndex = expectedTableHeaders.length - 1
        const serverId = mockupAllServices[0].relationships.servers.data[0].id
        let tableCell = dataTable.find(`.${serviceId}-cell-${cellIndex}`)
        let aTag = tableCell.find('a')
        await aTag.trigger('click')
        expect(wrapper.vm.$route.path).to.be.equals(`/dashboard/servers/${serverId}`)
    })

    it(`Should get total number of unique servers name accurately`, async () => {
        let allServerNames = []
        expectedTableRows.forEach(row => {
            if (Array.isArray(row.serverIds)) {
                let serverName = row.serverIds.map(name => `${name}`)
                allServerNames.push(serverName)
            }
        })
        // create unique set then convert back to array with unique items
        const uniqueServerNames = [...new Set(allServerNames)]
        expect(wrapper.vm.$data.serversLength).to.be.equals(uniqueServerNames.length)
    })
})
