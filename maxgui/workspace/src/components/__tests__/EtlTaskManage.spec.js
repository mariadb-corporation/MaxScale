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
import EtlTaskManage from '@wsComps/EtlTaskManage.vue'
import { ETL_ACTIONS } from '@wsSrc/constants'
import { lodash } from '@share/utils/helpers'
import EtlTask from '@wsModels/EtlTask'

const typesStub = Object.values(ETL_ACTIONS)

const etlTaskObjStub = {
    id: 'c74d6e00-4263-11ee-a879-6f8dfc9ca55f',
    status: 'Initializing',
}

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: true,
                component: EtlTaskManage,
                propsData: {
                    task: etlTaskObjStub,
                    types: typesStub,
                },
            },
            opts
        )
    )

describe('EtlTaskManage', () => {
    let wrapper

    afterEach(() => sinon.restore())

    it('Should pass $attrs to v-menu', () => {
        wrapper = mountFactory({ attrs: { value: true, tile: true } })
        const { value, tile } = wrapper.findComponent({ name: 'v-menu' }).vm.$props
        expect(value).to.equals(wrapper.vm.$attrs.value)
        expect(tile).to.equals(wrapper.vm.$attrs.tile)
    })

    it("actionMap shouldn't return an empty object", () => {
        wrapper = mountFactory()
        expect(wrapper.vm.actionMap).to.be.an('object')
        expect(wrapper.vm.actionMap).to.not.be.empty
    })

    it('Should emit "on-restart" event when RESTART action is chosen', () => {
        wrapper = mountFactory()
        wrapper.vm.handler(ETL_ACTIONS.RESTART)
        expect(wrapper.emitted('on-restart')[0][0]).to.equals(etlTaskObjStub.id)
    })

    Object.values(ETL_ACTIONS).forEach(action => {
        if (action !== ETL_ACTIONS.RESTART)
            it(`Should dispatch EtlTask actionHandler when ${action} is chosen`, async () => {
                wrapper = mountFactory()
                const stub = sinon.stub(EtlTask, 'dispatch')
                await wrapper.vm.handler(action)
                expect(stub).to.have.been.calledWith('actionHandler', {
                    type: action,
                    task: wrapper.vm.$props.task,
                })
            })
    })
})
