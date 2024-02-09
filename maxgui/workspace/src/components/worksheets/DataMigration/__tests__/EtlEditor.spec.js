/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import EtlEditor from '@wkeComps/DataMigration/EtlEditor'

describe('EtlEditor', () => {
    let wrapper
    beforeEach(() => {
        wrapper = mount({
            shallow: true,
            component: EtlEditor,
            propsData: {
                value: 'SELECT `id` FROM `Workspace`.`AnalyzeEditor`',
                label: 'Retrieve data from source',
                skipRegEditorCompleters: false,
            },
        })
    })

    it('Should add etl-editor--error class if there is no value', async () => {
        await wrapper.setProps({ value: '' })
        expect(wrapper.classes('etl-editor--error')).to.be.true
    })

    it('Should render min/max button', () => {
        expect(wrapper.findComponent({ name: 'mxs-tooltip-btn' }).exists()).to.be.true
    })

    it('Should pass accurate data to mxs-sql-editor', () => {
        const { value, options, skipRegCompleters } = wrapper.findComponent({
            name: 'mxs-sql-editor',
        }).vm.$props
        expect(value).to.equal(wrapper.vm.sql)
        expect(options).to.eql({ contextmenu: false, wordWrap: 'on' })
        expect(skipRegCompleters).to.equal(wrapper.vm.$props.skipRegEditorCompleters)
    })

    it('Should emit input event when sql is changed', () => {
        const newValue = 'SELECT 1'
        wrapper.vm.sql = newValue
        expect(wrapper.emitted('input')[0][0]).to.eql(newValue)
    })
})
