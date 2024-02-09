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
import MigrCreateDlg from '@wkeComps/DataMigration/MigrCreateDlg'
import { MIGR_DLG_TYPES } from '@wsSrc/constants'
import { lodash } from '@share/utils/helpers'

const mountFactory = opts =>
    mount(
        lodash.merge(
            { shallow: true, component: MigrCreateDlg, propsData: { handleSave: () => null } },
            opts
        )
    )

describe('MigrCreateDlg', () => {
    let wrapper

    afterEach(() => sinon.restore())

    it('Should pass accurate data to mxs-dlg', () => {
        wrapper = mountFactory()
        const { value, onSave, title, saveText } = wrapper.findComponent({
            name: 'mxs-dlg',
        }).vm.$props
        expect(value).to.equal(wrapper.vm.isOpened)
        expect(onSave).to.eql(wrapper.vm.onSave)
        expect(title).to.equal(wrapper.vm.$mxs_t('newMigration'))
        expect(saveText).to.equal(wrapper.vm.migr_dlg.type)
    })

    it('Should render form-prepend slot', () => {
        wrapper = mountFactory({
            computed: { isOpened: () => true },
            slots: { 'form-prepend': '<div data-test="form-prepend">test div</div>' },
        })
        expect(wrapper.find('[data-test="form-prepend"]').html()).to.be.equal(
            '<div data-test="form-prepend">test div</div>'
        )
    })

    it('Should render an input for migration name', () => {
        wrapper = mountFactory({ computed: { isOpened: () => true } })
        const { value } = wrapper.findComponent({ name: 'v-text-field' }).vm.$props
        expect(value).to.equal(wrapper.vm.$data.name)
    })

    Object.values(MIGR_DLG_TYPES).forEach(type => {
        const shouldBeOpened = type === MIGR_DLG_TYPES.CREATE
        it(`Should ${shouldBeOpened ? 'open' : 'not open'} the dialog
        when migr_dlg type is ${type}`, () => {
            wrapper = mountFactory({ computed: { migr_dlg: () => ({ type, is_opened: true }) } })
            expect(wrapper.vm.isOpened).to.equal(shouldBeOpened)
        })
    })

    it(`Should call SET_MIGR_DLG when isOpened value is changed`, () => {
        wrapper = mountFactory()
        const stub = sinon.stub(wrapper.vm, 'SET_MIGR_DLG')
        const newValue = !wrapper.vm.isOpened
        wrapper.vm.isOpened = newValue
        stub.calledOnceWithExactly({ ...wrapper.vm.migr_dlg, is_opened: newValue })
    })
})
