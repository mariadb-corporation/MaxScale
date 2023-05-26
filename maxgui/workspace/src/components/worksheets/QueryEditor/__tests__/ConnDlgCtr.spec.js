/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import ConnDlgCtr from '@wkeComps/QueryEditor/ConnDlgCtr.vue'
import { lodash } from '@share/utils/helpers'
import { getErrMsgEle, inputChangeMock, itemSelectMock } from '@tests/unit/utils'

const dummy_rc_target_names_map = {
    listeners: [
        { id: 'RO-Listener', type: 'listeners' },
        { id: 'RW-Listener', type: 'listeners' },
        { id: 'nosql-Listener', type: 'listeners' },
    ],
    servers: [
        { id: 'server_0', type: 'servers' },
        { id: 'server_1', type: 'servers' },
    ],
    services: [
        { id: 'RO-Service', type: 'services' },
        { id: 'RO-Service', type: 'services' },
    ],
}
const dummy_pre_select_conn_rsrc = { id: 'server_0', type: 'servers' }
const mountFactory = opts =>
    // deep merge opts
    mount(
        lodash.merge(
            {
                shallow: true,
                component: ConnDlgCtr,
                propsData: {
                    value: true, // open dialog by default
                    handleSave: sinon.stub(),
                },
            },
            opts
        )
    )

/**
 * a mock to change a value in body obj
 */
async function mockChangingBody({ wrapper, key, value }) {
    await wrapper.setData({
        body: { ...wrapper.vm.body, [key]: value },
    })
}

describe(`ConnDlgCtr - child component's data communication tests `, () => {
    let wrapper
    beforeEach(() => {
        wrapper = mountFactory({
            computed: { rc_target_names_map: () => dummy_rc_target_names_map },
        })
    })
    it(`Should pass accurate data to mxs-dlg via props`, () => {
        const {
            value,
            onSave,
            title,
            lazyValidation,
            hasSavingErr,
            hasFormDivider,
        } = wrapper.findComponent({
            name: 'mxs-dlg',
        }).vm.$props
        expect(value).to.be.equals(wrapper.vm.isOpened)
        expect(onSave).to.be.equals(wrapper.vm.onSave)
        expect(title).to.be.equals(`${wrapper.vm.$mxs_t('connectTo')}...`)
        expect(lazyValidation).to.be.false
        expect(hasSavingErr).to.be.equals(wrapper.vm.hasSavingErr)
        expect(hasFormDivider).to.be.true
    })
    it(`Should pass accurate data to resource-type-dropdown via props`, () => {
        const { value, items, hideDetails } = wrapper
            .findComponent({ name: 'mxs-dlg' })
            .find('.resource-type-dropdown').vm.$props
        expect(value).to.be.equals(wrapper.vm.$data.resourceType)
        expect(items).to.be.equals(wrapper.vm.$data.resourceTypes)
        expect(hideDetails).to.be.equals('auto')
    })
    it(`Should pass accurate data to resource-dropdown via props`, () => {
        const {
            value,
            items,
            entityName,
            clearable,
            showPlaceHolder,
            required,
            errorMessages,
        } = wrapper.findComponent({ name: 'mxs-dlg' }).find('.resource-dropdown').vm.$props
        expect(value).to.be.deep.equals(wrapper.vm.$data.selectedResource)
        expect(items).to.be.deep.equals(wrapper.vm.resourceItems)
        expect(entityName).to.be.equals(wrapper.vm.$data.resourceType)
        expect(clearable).to.be.true
        expect(showPlaceHolder).to.be.true
        expect(required).to.be.true
        expect(errorMessages).to.be.equals(wrapper.vm.$data.errRsrcMsg)
    })
})
describe(`ConnDlgCtr - dialog open/close side-effect tests`, () => {
    let wrapper, handleFetchRsrcsSpy
    beforeEach(() => {
        handleFetchRsrcsSpy = sinon.spy(ConnDlgCtr.methods, 'handleFetchRsrcs')
        wrapper = mountFactory({
            methods: {
                fetchRcTargetNames: () => null,
            },
        })
    })
    afterEach(() => {
        handleFetchRsrcsSpy.restore()
    })
    it(`Should choose listeners as the default resource when dialog is opened`, () => {
        expect(wrapper.vm.resourceType).to.be.equals('listeners')
    })
    it(`Should call handleFetchRsrcs when dialog is opened`, () => {
        handleFetchRsrcsSpy.should.have.been.calledOnceWith(wrapper.vm.$data.resourceTypes[0])
    })
    it(`Should reset to initial state after closing dialog`, async () => {
        const initialData = wrapper.vm.$data
        await mockChangingBody({ wrapper, key: 'user', value: 'maxskysql' })
        await wrapper.setProps({ value: false }) // close dialog
        expect(initialData).to.be.deep.equals(wrapper.vm.$data)
    })
    it(`Should call SET_PRE_SELECT_CONN_RSRC after closing dialog`, async () => {
        const spy = sinon.spy(wrapper.vm, 'SET_PRE_SELECT_CONN_RSRC')
        await wrapper.setProps({ value: false }) // close dialog
        spy.should.have.been.calledOnceWith(null)
    })
})
describe(`ConnDlgCtr - methods and computed properties tests `, () => {
    let wrapper
    it(`Should return accurate value for hasSavingErr computed property`, () => {
        const errStates = [true, false]
        errStates.forEach(errState => {
            wrapper = mountFactory({ computed: { conn_err_state: () => errState } })
            expect(wrapper.vm.hasSavingErr).to.be[errState]
        })
    })
    const resourceTypes = ['listeners', 'servers', 'services']
    resourceTypes.forEach(type => {
        it(`Should return accurate resources for resourceItems computed property
        if resourceType is ${type}`, async () => {
            wrapper = mountFactory({
                computed: { rc_target_names_map: () => dummy_rc_target_names_map },
            })
            await wrapper.setData({ resourceType: type })
            expect(wrapper.vm.resourceItems).to.be.deep.equals(dummy_rc_target_names_map[type])
        })
    })
    it(`Should use value from pre_select_conn_rsrc to assign it to resourceType`, () => {
        wrapper = mountFactory({
            computed: { pre_select_conn_rsrc: () => dummy_pre_select_conn_rsrc },
        })
        expect(wrapper.vm.resourceType).to.be.equals(dummy_pre_select_conn_rsrc.type)
    })
    it(`Should assign pre_select_conn_rsrc to selectedResource`, () => {
        wrapper = mountFactory({
            computed: {
                rc_target_names_map: () => dummy_rc_target_names_map,
                pre_select_conn_rsrc: () => dummy_pre_select_conn_rsrc,
            },
        })
        wrapper.vm.handleChooseDefRsrc(dummy_pre_select_conn_rsrc.type)
        expect(wrapper.vm.$data.selectedResource).to.be.eql(dummy_pre_select_conn_rsrc)
    })
    it(`Should assign first resource item to to selectedResource`, () => {
        wrapper = mountFactory({
            computed: {
                rc_target_names_map: () => dummy_rc_target_names_map,
            },
        })
        wrapper.vm.handleChooseDefRsrc('listeners')
        expect(wrapper.vm.$data.selectedResource).to.be.eql(dummy_rc_target_names_map.listeners[0])
    })
    it(`Should call handleSave with accurate arguments`, () => {
        let handleSaveArgs
        const mockBodyFormData = {
            target: 'nosql-Listener',
            user: 'maxskysql',
            password: 'skysql',
            db: '',
            timeout: 300,
        }
        const mockSelectedResource = { id: 'nosql-Listener' }
        const mockSelectedResourceType = 'listeners'
        // mock form data
        wrapper = mountFactory({
            data: () => ({
                resourceType: mockSelectedResourceType,
                selectedResource: mockSelectedResource,
                body: mockBodyFormData,
            }),
            propsData: {
                handleSave: args => (handleSaveArgs = args),
            },
        })
        wrapper.vm.onSave()
        expect(handleSaveArgs).to.be.deep.equals({
            body: { target: mockSelectedResource.id, ...mockBodyFormData },
        })
    })
})
describe(`ConnDlgCtr - form input tests`, () => {
    let wrapper
    it(`Should parse value as number for timeout field`, async () => {
        wrapper = mountFactory({ shallow: false })
        const inputComponent = wrapper.findComponent({ name: 'mxs-dlg' }).find(`.timeout`)
        await inputChangeMock(inputComponent, '300')
        expect(wrapper.vm.body.timeout).to.be.equals(300)
    })

    it(`Should show error message if selectedResource field value is empty`, async () => {
        wrapper = mountFactory({
            shallow: false,
            data: () => ({
                resourceType: 'listeners',
                selectedResource: dummy_rc_target_names_map.listeners[0],
            }),
        })
        const dlg = wrapper.findComponent({ name: 'mxs-dlg' })
        const dropDownComponent = dlg.find('.resource-dropdown')
        await itemSelectMock(dropDownComponent, null)
        await wrapper.vm.$nextTick()
        expect(getErrMsgEle(dropDownComponent).text()).to.be.equals(
            wrapper.vm.$mxs_t('errors.requiredInput', {
                inputName: wrapper.vm.$mxs_tc('listeners'),
            })
        )
    })
})
