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
import ConnDlgCtr from '@wsComps/ConnDlgCtr.vue'
import { lodash } from '@share/utils/helpers'
import QueryConn from '@wsModels/QueryConn'

const stubPreSelectConnRsrc = { id: 'Read-Only-Service', type: 'services' }
const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: true,
                component: ConnDlgCtr,
                propsData: { value: true },
            },
            opts
        )
    )

describe('ConnDlgCtr', () => {
    let wrapper

    describe(`Child component's data communication tests`, () => {
        beforeEach(() => {
            wrapper = mountFactory()
        })

        it('Should pass accurate data to mxs-dlg', () => {
            const {
                value,
                title,
                lazyValidation,
                hasSavingErr,
                hasFormDivider,
                onSave,
            } = wrapper.findComponent({
                name: 'mxs-dlg',
            }).vm.$props
            expect(title).to.equal(`${wrapper.vm.$mxs_t('connectTo')}...`)
            expect(value).to.equal(wrapper.vm.isOpened)
            expect(lazyValidation).to.be.false
            expect(hasFormDivider).to.be.true
            expect(hasSavingErr).to.equal(wrapper.vm.hasSavingErr)
            expect(onSave).to.be.eql(wrapper.vm.onSave)
        })

        const formValidTests = [false, true]
        formValidTests.forEach(isFormValid => {
            it(`Should ${
                isFormValid ? 'enable' : 'disable'
            } connect button when isFormValid ${isFormValid}`, async () => {
                await wrapper.setData({ isFormValid })
                expect(wrapper.find('[data-test="connect-btn"]').vm.$props.disabled).to.be[
                    !isFormValid
                ]
            })
        })

        it('Should pass accurate data to mxs-select', () => {
            const { value, items, entityName, showPlaceHolder, required } = wrapper.findComponent({
                name: 'mxs-select',
            }).vm.$props
            expect(value).to.equal(wrapper.vm.$data.selectedResource)
            expect(items).to.be.eql(wrapper.vm.resourceItems)
            expect(entityName).to.equal(wrapper.vm.$data.resourceType)
            expect(showPlaceHolder).to.be.true
            expect(required).to.be.true
        })

        const renderedComponents = [
            'mxs-uid-input',
            'mxs-pwd-input',
            'mxs-label-field',
            'mxs-timeout-input',
        ]
        renderedComponents.forEach(name => {
            it(`Should render ${name}`, () => {
                expect(wrapper.findComponent({ name }).exists()).to.be.true
            })
        })
    })

    describe(`Computed properties tests`, () => {
        beforeEach(() => {
            wrapper = mountFactory()
        })

        it(`Should return accurate value for isOpened`, () => {
            expect(wrapper.vm.isOpened).to.equal(wrapper.vm.$props.value)
        })

        it(`Should emit input event`, () => {
            wrapper.vm.isOpened = false
            expect(wrapper.emitted('input')[0][0]).to.be.eql(false)
        })

        it(`resourceItems should be an array`, () => {
            expect(wrapper.vm.resourceItems).to.be.an('array')
        })

        it(`Should return accurate value for hasSavingErr`, () => {
            expect(wrapper.vm.hasSavingErr).to.equal(wrapper.vm.conn_err_state)
        })
    })

    describe(`Watcher tests`, () => {
        afterEach(() => sinon.restore())

        it('Should immediately call setDefResourceType when dialog is opened', async () => {
            const spy = sinon.spy(ConnDlgCtr.methods, 'setDefResourceType')
            wrapper = mountFactory()
            spy.should.have.been.calledOnce
        })
        it(`Should immediately call onChangeResourceType when resourceType is changed`, () => {
            const spy = sinon.spy(ConnDlgCtr.methods, 'onChangeResourceType')
            wrapper = mountFactory()
            spy.should.have.been.calledOnce
        })
    })

    describe(`Method tests`, () => {
        afterEach(() => sinon.restore())

        it('Should call expected methods in onChangeResourceType', async () => {
            wrapper = mountFactory({
                propsData: { value: false },
                methods: { handleFetchResources: () => {}, handleChooseDefResource: () => {} },
            })
            const methods = [
                'SET_DEF_CONN_OBJ_TYPE',
                'handleFetchResources',
                'handleChooseDefResource',
            ]
            const spies = methods.map(method => sinon.spy(wrapper.vm, method))
            await wrapper.vm.onChangeResourceType('servers')
            for (const spy of spies) {
                spy.should.have.been.calledOnce
            }
        })

        it(`Should call fetchRcTargetNames in handleFetchResources`, async () => {
            wrapper = mountFactory({
                propsData: { value: false },
                computed: { rc_target_names_map: () => ({}) },
            })
            const spy = sinon.spy(wrapper.vm, 'fetchRcTargetNames')
            await wrapper.vm.handleFetchResources('servers')
            spy.should.have.been.calledOnce
        })

        it(`Should call fetchRcTargetNames in handleFetchResources only
            if it has not been called before`, async () => {
            wrapper = mountFactory({
                propsData: { value: false },
                computed: { rc_target_names_map: () => ({ servers: ['server_0'] }) },
            })
            const spy = sinon.spy(wrapper.vm, 'fetchRcTargetNames')
            await wrapper.vm.handleFetchResources('servers')
            spy.should.not.been.called
        })

        it(`setDefResourceType method should use def_conn_obj_type as the
            default resourceType`, async () => {
            wrapper = mountFactory({ propsData: { value: false } })
            wrapper.vm.setDefResourceType()
            expect(wrapper.vm.resourceType).to.equal(wrapper.vm.def_conn_obj_type)
        })

        it(`setDefResourceType method should use pre_select_conn_rsrc as the
            default resourceType`, async () => {
            wrapper = mountFactory({
                propsData: { value: false },
                computed: {
                    pre_select_conn_rsrc: () => stubPreSelectConnRsrc,
                },
            })
            wrapper.vm.setDefResourceType()
            expect(wrapper.vm.resourceType).to.equal('services')
        })

        it(`handleChooseDefResource method should choose the first item in resourceItems as
            the default selected resource`, () => {
            wrapper = mountFactory({
                propsData: { value: false },
                computed: { resourceItems: () => [stubPreSelectConnRsrc] },
            })
            wrapper.vm.handleChooseDefResource()
            expect(wrapper.vm.selectedResource).to.eql(stubPreSelectConnRsrc)
        })

        it(`handleChooseDefResource method should use pre_select_conn_rsrc as the
            default selected resource`, () => {
            wrapper = mountFactory({
                propsData: { value: false },
                computed: {
                    resourceItems: () => [stubPreSelectConnRsrc],
                    pre_select_conn_rsrc: () => stubPreSelectConnRsrc,
                },
            })
            wrapper.vm.handleChooseDefResource()
            expect(wrapper.vm.$data.selectedResource).to.eql(stubPreSelectConnRsrc)
        })

        it(`Should call onSave with accurate arguments`, () => {
            const spy = sinon.spy(QueryConn, 'dispatch')
            const mockBodyFormData = {
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
            })
            wrapper.vm.onSave()
            spy.should.have.been.calledOnceWith('handleOpenConn', {
                body: { target: mockSelectedResource.id, ...mockBodyFormData },
                meta: { name: mockSelectedResource.id },
            })
        })
    })
})
