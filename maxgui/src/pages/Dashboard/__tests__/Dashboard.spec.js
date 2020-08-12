import Vue from 'vue'
import chai, { expect } from 'chai'
import mount from '@tests/unit/setup'
import Dashboard from '@/pages/Dashboard'
import sinon from 'sinon'
import sinonChai from 'sinon-chai'

chai.should()
chai.use(sinonChai)

describe('Dashboard index', () => {
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
            shallow: true,
            component: Dashboard,
        })
    })

    afterEach(async function() {
        await axiosStub.restore()
        await wrapper.setData({
            loop: false,
        })
        await wrapper.destroy()
    })

    it(`Should send requests in parallel to get maxscale overview info,
      maxscale threads, all servers, monitors, sessions, services,
      listeners and filters`, async () => {
        await axiosStub.firstCall.should.have.been.calledWith(
            '/maxscale?fields[maxscale]=version,commit,started_at,activated_at,uptime'
        )
        await axiosStub.secondCall.should.have.been.calledWith(
            '/maxscale/threads?fields[threads]=stats'
        )
        await axiosStub.thirdCall.should.have.been.calledWith('/servers')
        await axiosStub.getCall(3).should.have.been.calledWith('/monitors')
        await axiosStub.getCall(4).should.have.been.calledWith('/sessions')
        await axiosStub.getCall(5).should.have.been.calledWith('/services')

        await wrapper.vm.$nextTick(async () => {
            await axiosStub.should.have.been.calledWith('/listeners')
            await axiosStub.should.have.been.calledWith('/filters')
        })
    })

    it(`Should render page-wrapper component`, async () => {
        expect(wrapper.findComponent({ name: 'page-wrapper' }).exists()).to.be.true
    })

    it(`Should render page-header component`, async () => {
        expect(wrapper.findComponent({ name: 'page-header' }).exists()).to.be.true
    })

    it(`Should render graphs component`, async () => {
        expect(wrapper.findComponent({ name: 'graphs' }).exists()).to.be.true
    })
})
