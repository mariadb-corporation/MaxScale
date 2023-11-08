/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import mount from '@tests/unit/setup'
import ConfigWizard from '@rootSrc/pages/ConfigWizard'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts =>
    mount(
        lodash.merge(
            { shallow: false, component: ConfigWizard, methods: { fetchAllModules: sinon.stub() } },
            opts
        )
    )

describe('ConfigWizard index', () => {
    let wrapper

    afterEach(() => {
        sinon.restore()
    })

    it(`Should call init method on created`, async () => {
        const spy = sinon.spy(ConfigWizard.methods, 'init')
        wrapper = mountFactory()
        spy.should.have.been.calledOnce
    })

    it(`Should set server as the active object type`, async () => {
        wrapper = mountFactory({
            data: { activeObjType: 'Monitors' },
        })
        wrapper.vm.init()
        expect(wrapper.vm.$data.activeObjType).to.equal(wrapper.vm.MXS_OBJ_TYPES.SERVER)
    })
})
