/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import PageToolbar from '@/pages/QueryPage/PageToolbar'

const mountFactory = opts =>
    mount({
        shallow: false,
        component: PageToolbar,
        stubs: {
            'readonly-query-editor': "<div class='stub'></div>",
        },
        ...opts,
    })

// stub cnct_resources
const dummy_curr_cnct_resource = { id: '1', name: 'server_0', type: 'servers' }
async function clickAddBtnMock(wrapper) {
    await wrapper.find('.add-wke-btn').trigger('click') // click + button
}

describe(`PageToolbar - mounted hook and child component's interaction tests`, () => {
    let wrapper
    beforeEach(() => {
        wrapper = mountFactory()
    })
    it('Should emit get-total-btn-width evt in the next tick after component is mounted', () => {
        wrapper.vm.$nextTick(() => {
            expect(wrapper.emitted()).to.have.property('get-total-btn-width')
        })
    })
    it(`Should pass accurate data to confirm-dialog
      (confirm saving query to favorite) via props`, () => {
        const confirmDialog = wrapper.findComponent({ name: 'confirm-dialog' })
        const { value, title, type, onSave } = confirmDialog.vm.$props
        expect(value).to.be.equals(wrapper.vm.isConfDlgOpened)
        expect(title).to.be.equals(wrapper.vm.$t('confirmations.addToFavorite'))
        expect(type).to.be.equals('add')
        expect(onSave).to.be.equals(wrapper.vm.addToFavorite)
    })
    it('Should pass accurate data to query-config-dialog via props', () => {
        const cnfDlg = wrapper.findComponent({ name: 'query-config-dialog' })
        expect(cnfDlg.vm.$props.value).to.be.equals(wrapper.vm.queryConfigDialog)
    })
})

describe('PageToolbar - Add new worksheet tests', () => {
    it(`Should only allow to add new worksheet when a worksheet
      is bound to a connection`, async () => {
        let handleAddNewWkeCallCount = 0
        let wrapper = mountFactory({
            computed: { cnct_resources: () => [dummy_curr_cnct_resource] },
            methods: {
                // stubs vuex actions
                addNewWs: () => handleAddNewWkeCallCount++,
            },
        })
        await clickAddBtnMock(wrapper)
        expect(handleAddNewWkeCallCount).to.be.equals(1)
    })
})

describe('PageToolbar - save to favorite tests', () => {
    let wrapper
    it(`Should disable save to favorite button if query_txt is empty `, () => {
        wrapper = mountFactory()
        const saveToFavBtn = wrapper.find('.save-to-fav-btn')
        expect(saveToFavBtn.element.disabled).to.be.true
    })
    it(`Should allow query to be saved to favorite if query_txt is not empty`, () => {
        wrapper = mountFactory({ computed: { query_txt: () => 'SELECT 1' } })
        const saveToFavBtn = wrapper.find('.save-to-fav-btn')
        expect(saveToFavBtn.element.disabled).to.be.false
    })
    it(`Should popup dialog to save query text to favorite`, () => {
        expect(wrapper.vm.isConfDlgOpened).to.be.false
        wrapper = mountFactory({ computed: { query_txt: () => 'SELECT 1' } })
        wrapper.find('.save-to-fav-btn').trigger('click')
        expect(wrapper.vm.isConfDlgOpened).to.be.true
    })
    it(`Should generate favorite object before popup the dialog`, () => {
        wrapper = mountFactory({ computed: { query_txt: () => 'SELECT 1' } })
        wrapper.find('.save-to-fav-btn').trigger('click')
        const ts = new Date().valueOf()
        const generatedName = `Favorite statements - ${wrapper.vm.$help.dateFormat({
            value: ts,
            formatType: 'DATE_RFC2822',
        })}`
        expect(wrapper.vm.favorite.date).to.be.equals(ts)
        expect(wrapper.vm.favorite.name).to.be.equals(generatedName)
    })
    it(`Should call addToFavorite`, async () => {
        wrapper = mountFactory({ computed: { query_txt: () => 'SELECT 1' } })
        const addToFavoriteSpy = sinon.spy(wrapper.vm, 'addToFavorite')
        // mock open dialog and save
        await wrapper.find('.save-to-fav-btn').trigger('click')
        // wait until dialog is rendered
        await wrapper.vm.$nextTick()
        const confirmDialog = wrapper.findComponent({ name: 'confirm-dialog' })
        await confirmDialog.find('.save').trigger('click')
        addToFavoriteSpy.should.have.been.calledOnce
    })
})

describe('PageToolbar - query setting button tests', () => {
    it(`Should popup query setting dialog`, () => {
        let wrapper = mountFactory()
        expect(wrapper.vm.queryConfigDialog).to.be.false
        wrapper.find('.query-setting-btn').trigger('click')
        expect(wrapper.vm.queryConfigDialog).to.be.true
    })
})

describe('PageToolbar - maximize/minimize button tests', () => {
    const is_fullscreen_values = [true, false]
    is_fullscreen_values.forEach(v => {
        it(`Should call SET_FULLSCREEN action to ${
            v ? 'maximize' : 'minimize'
        } page content`, () => {
            let wrapper = mountFactory()
            const btn = wrapper.find('.min-max-btn')
            btn.trigger('click')
            expect(wrapper.vm.is_fullscreen).to.be.equals(v)
        })
    })
})
