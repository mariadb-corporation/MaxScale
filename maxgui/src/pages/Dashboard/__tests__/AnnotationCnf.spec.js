/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import mount from '@tests/unit/setup'
import { lodash } from '@share/utils/helpers'
import AnnotationCnf from '@src/pages/Dashboard/AnnotationCnf'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: false,
                component: AnnotationCnf,
                propsData: {
                    value: {
                        display: true,
                        yMin: 80,
                        yMax: 80,
                        borderColor: '#EB5757',
                        borderWidth: 1,
                        label: {
                            backgroundColor: '#EB5757',
                            color: '#FFFFFF',
                            content: 'max',
                            display: true,
                            padding: 2,
                        },
                    },
                },
            },
            opts
        )
    )

let wrapper

describe('AnnotationCnf', () => {
    afterEach(() => sinon.restore())
    const mockFields = [
        { dataId: 'content', label: 'label', type: 'string', isLabel: true },
        { dataId: 'color', label: 'color-input', type: 'color', isLabel: true },
    ]
    it('Each field in annotationFields should have expected attributes', async () => {
        wrapper = mountFactory()
        wrapper.vm.annotationFields.forEach(field =>
            expect(field).to.include.all.keys('id', 'dataId', 'label', 'type')
        )
    })

    it('Should emit "on-delete" event when delete button is clicked', async () => {
        wrapper = mountFactory()
        await wrapper.find('.delete-btn').trigger('click')
        expect(wrapper.emitted('on-delete').length).to.equal(1)
    })

    mockFields.forEach(field => {
        const updateMethod = field.type === 'color' ? 'onUpdateColor' : 'setFieldValue'

        it(`Should call ${updateMethod} when value is changed`, () => {
            wrapper = mountFactory({ computed: { annotationFields: () => [field] } })
            const spy = sinon.spy(wrapper.vm, updateMethod)
            wrapper.findComponent({ name: 'mxs-cnf-field' }).vm.$emit('input', '123')
            spy.should.have.been.calledOnceWithExactly({ field: field, value: '123' })
        })
        if (field.type === 'color')
            it(`Should call onClickColorInput when input is clicked`, async () => {
                wrapper = mountFactory({ computed: { annotationFields: () => [field] } })
                const spy = sinon.spy(wrapper.vm, 'onClickColorInput')
                await wrapper.findComponent({ name: 'mxs-cnf-field' }).trigger('click')
                spy.should.have.been.calledOnceWithExactly(field)
            })
    })

    it('Should debounce setColorFieldValue method', async () => {
        const debounceTime = 300
        const clock = sinon.useFakeTimers()
        const spy = sinon.spy(AnnotationCnf.methods, 'setFieldValue')
        wrapper = mountFactory()
        wrapper.vm.setColorFieldValue({ field: mockFields[1], value: '#EEEEEE' })
        const threshold = 50
        clock.tick(debounceTime - threshold)
        spy.should.have.not.been.calledOnce
        clock.tick(threshold)
        spy.should.have.been.calledOnce
    })
})
