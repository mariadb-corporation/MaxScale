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
        axiosStub = sinon.stub(Vue.axios, 'get').resolves(
            Promise.resolve({
                data: {},
            })
        )
        wrapper = mount({
            shallow: false,
            component: Dashboard,
        })
    })

    afterEach(async function() {
        await axiosStub.restore()
    })

    it(`Should send 7 requests in parallel to get maxscale overview info,
      maxscale threads, all servers, monitors, sessions and services`, async () => {
        axiosStub.firstCall.should.have.been.calledWith(
            '/maxscale?fields[maxscale]=version,commit,started_at,activated_at,uptime'
        )
        axiosStub.secondCall.should.have.been.calledWith('/maxscale/threads?fields[threads]=stats')
        axiosStub.thirdCall.should.have.been.calledWith('/servers')
        axiosStub.getCall(3).should.have.been.calledWith('/monitors')
        axiosStub.getCall(4).should.have.been.calledWith('/sessions')
        axiosStub.getCall(5).should.have.been.calledWith('/services')
        axiosStub.lastCall.should.have.been.calledWith('/listeners')
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
