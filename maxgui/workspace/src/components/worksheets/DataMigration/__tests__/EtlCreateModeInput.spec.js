/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import EtlCreateModeInput from '@wkeComps/DataMigration/EtlCreateModeInput'
import EtlTaskTmp from '@wsModels/EtlTaskTmp'
import { ETL_CREATE_MODES } from '@wsSrc/constants'

describe('EtlCreateModeInput', () => {
    let wrapper
    beforeEach(() => {
        wrapper = mount({
            shallow: true,
            component: EtlCreateModeInput,
            propsData: { taskId: '' },
        })
    })
    afterEach(() => sinon.restore())

    it('Should pass accurate data to v-select', () => {
        const { value, items, itemText, itemValue, hideDetails } = wrapper.findComponent({
            name: 'v-select',
        }).vm.$props
        expect(value).to.equal(wrapper.vm.createMode)
        expect(items).to.eql(Object.values(ETL_CREATE_MODES))
        expect(itemText).to.equal('text')
        expect(itemValue).to.equal('id')
        expect(hideDetails).to.be.true
    })

    it('Should update EtlTaskTmp when createMode is changed', () => {
        const stub = sinon.stub(EtlTaskTmp, 'update')
        const newValue = ETL_CREATE_MODES.REPLACE
        wrapper.vm.createMode = newValue
        stub.calledOnceWithExactly({
            where: wrapper.vm.$props.taskId,
            data: { create_mode: newValue },
        })
    })
})
