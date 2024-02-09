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
import { lodash } from '@share/utils/helpers'
import AnnotationsCnfCtr from '@src/pages/Dashboard/AnnotationsCnfCtr'

const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: false,
                component: AnnotationsCnfCtr,
                propsData: {
                    value: {
                        '5b111d60-b46c-11ee-893e-29dfd14d8395': {
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
                    cnfType: 'Annotations',
                },
            },
            opts
        )
    )

let wrapper

describe('AnnotationsCnfCtr', () => {
    afterEach(() => sinon.restore())

    it('Should render cnfType prop correctly', () => {
        wrapper = mountFactory()
        expect(wrapper.find('[data-test="headline"]').text()).to.equal('Annotations')
    })
    const annotationsLengthTestCases = [0, 1]
    annotationsLengthTestCases.forEach(v => {
        it(`Should ${
            v ? 'render' : 'not render'
        } a small add button when annotationsLength === ${v}`, () => {
            wrapper = mountFactory({ computed: { annotationsLength: () => v } })
            const btn = wrapper.find('[data-test="add-btn"]')
            if (v) expect(btn.exists()).to.be.true
            else expect(btn.exists()).to.be.false
        })

        it(`Should ${
            v ? 'not render' : 'render'
        } add-btn-block when annotationsLength === ${v}`, () => {
            wrapper = mountFactory({ computed: { annotationsLength: () => v } })
            const btn = wrapper.find('[data-test="add-btn-block"]')
            if (v) expect(btn.exists()).to.be.false
            else expect(btn.exists()).to.be.true
        })
    })

    it('Should call onAdd method when add button is clicked', async () => {
        const spy = sinon.spy(AnnotationsCnfCtr.methods, 'onAdd')
        wrapper = mountFactory()
        await wrapper.find('[data-test="add-btn"]').trigger('click')
        spy.should.have.been.calledOnce
    })

    it('Should call onDelete method when on-delete event is emitted', async () => {
        const spy = sinon.spy(wrapper.vm, 'onDelete')
        await wrapper.findComponent({ name: 'annotation-cnf' }).vm.$emit('on-delete', 'annotation1')
        spy.should.have.been.calledOnce
    })
})
