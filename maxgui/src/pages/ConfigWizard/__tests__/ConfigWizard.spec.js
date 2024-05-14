/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import mount from '@tests/unit/setup'
import ConfigWizard from '@src/pages/ConfigWizard'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts => mount(lodash.merge({ shallow: false, component: ConfigWizard }, opts))

describe('ConfigWizard index', () => {
    let wrapper

    afterEach(() => {
        sinon.restore()
    })

    it(`Should call init method on created`, () => {
        const spy = sinon.spy(ConfigWizard.methods, 'init')
        wrapper = mountFactory()
        spy.should.have.been.calledOnce
    })

    const spyMethods = ['initStageMapData', 'fetchAllModules']
    spyMethods.forEach(method =>
        it(`Should call ${method} when init method is called`, () => {
            const spy = sinon.spy(ConfigWizard.methods, method)
            wrapper = mountFactory()
            spy.should.have.been.calledOnce
        })
    )

    it(`Should have overview as the default stage`, () => {
        wrapper = mountFactory()
        expect(wrapper.vm.$data.activeIdxStage).to.equal(0)
    })

    it(`Should have 6 stages`, () => {
        wrapper = mountFactory()
        expect(Object.keys(wrapper.vm.$data.stageDataMap).length).to.equal(6)
    })

    it(`Should have expected data fields for each stage`, () => {
        wrapper = mountFactory()
        Object.keys(wrapper.vm.$data.stageDataMap).forEach(stage => {
            const data = wrapper.vm.$data.stageDataMap[stage]
            if (stage !== wrapper.vm.overviewStage.label)
                expect(data).to.have.all.keys('label', 'component', 'newObjMap', 'existingObjMap')
            else expect(data).to.have.all.keys('label', 'component')
        })
    })

    it(`Should render overview-stage component by default`, () => {
        wrapper = mountFactory()
        expect(wrapper.findComponent({ name: 'overview-stage' }).exists()).to.be.true
    })

    it(`Should pass accurate data to obj-stage component`, () => {
        wrapper = mountFactory({ data: () => ({ activeIdxStage: 1 }) })
        const { objType, stageDataMap } = wrapper.findComponent({
            name: 'obj-stage',
        }).vm.$props
        expect(objType).to.equal('servers')
        expect(stageDataMap).to.eql(wrapper.vm.$data.stageDataMap)
    })
})
