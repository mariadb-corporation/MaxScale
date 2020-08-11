import Vue from 'vue'
import chai from 'chai'
import mount from '@tests/unit/setup'
import Graphs from '@/pages/Dashboard/Graphs'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'
import { mockupAllServers } from '@tests/unit/mockup'

chai.should()
chai.use(sinonChai)

describe('Graphs index', () => {
    let wrapper, axiosStub

    after(async () => {
        await axiosStub.reset()
    })

    beforeEach(async () => {
        axiosStub = sinon.stub(Vue.prototype.$axios, 'get').resolves(
            Promise.resolve({
                data: {},
            })
        )
        wrapper = mount({
            shallow: false,
            component: Graphs,

            computed: {
                sessionsChartData: () => ({
                    datasets: [
                        {
                            label: 'Total sessions',
                            type: 'line',
                            backgroundColor: 'rgba(171,199,74,0.1)',
                            borderColor: 'rgba(171,199,74,1)',
                            borderWidth: 1,
                            lineTension: 0,
                            data: [{ x: 1596440973122, y: 30 }],
                        },
                    ],
                }),
                serversConnectionsChartData: () => ({
                    datasets: [
                        {
                            label: 'CONNECTIONS',
                            type: 'line',
                            backgroundColor: 'rgba(171,199,74,0.1)',
                            borderColor: 'rgba(171,199,74,1)',
                            borderWidth: 1,
                            lineTension: 0,
                            data: [{ x: 1596440973122, y: 10 }],
                        },
                    ],
                }),
                threads_chart_data: () => ({
                    datasets: [
                        {
                            label: 'LOAD',
                            type: 'line',
                            backgroundColor: 'rgba(171,199,74,0.1)',
                            borderColor: 'rgba(171,199,74,1)',
                            borderWidth: 1,
                            lineTension: 0,
                            data: [{ x: 1596440973122, y: 20 }],
                        },
                    ],
                }),
                allServers: () => mockupAllServers,
            },
        })
    })

    afterEach(async function() {
        await axiosStub.restore()
    })

    it(`Should update graphs by first sending requests in parallel to
      get all servers, monitors, sessions, services and maxscale threads`, async () => {
        // this prevent fetch loop in line-chart
        await wrapper.setData({
            chartOptionsWithOutCallBack: null,
            mainChartOptions: null,
        })
        //mockup update chart
        await wrapper.vm.updateChart()

        await axiosStub.getCall(0).should.have.been.calledWith('/servers')
        await axiosStub.getCall(1).should.have.been.calledWith('/monitors')
        await axiosStub.getCall(2).should.have.been.calledWith('/sessions')
        await axiosStub.getCall(3).should.have.been.calledWith('/services')
        await axiosStub.lastCall.should.have.been.calledWith(
            '/maxscale/threads?fields[threads]=stats'
        )
    })
})
