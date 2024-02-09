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
import BlankWke from '@wkeComps/BlankWke'
import { lodash } from '@share/utils/helpers'

const cardsStub = [
    {
        title: 'Run Queries',
        icon: '',
        iconSize: 26,
        disabled: false,
        click: () => null,
    },
    {
        title: 'Data Migration',
        icon: '',
        iconSize: 32,
        disabled: false,
        click: () => null,
    },
    {
        title: 'Create an ERD',
        icon: '',
        iconSize: 32,
        click: () => null,
    },
]
const mountFactory = opts =>
    mount(
        lodash.merge(
            {
                shallow: true,
                component: BlankWke,
                propsData: {
                    cards: cardsStub,
                    ctrDim: { width: 1024, height: 768 },
                },
            },
            opts
        )
    )

describe('BlankWke', () => {
    let wrapper

    afterEach(() => sinon.restore())

    it('Should render cards', () => {
        wrapper = mountFactory()
        expect(wrapper.findAllComponents({ name: 'v-card' }).length).to.equal(cardsStub.length)
    })

    it('Should disable card and icon', () => {
        wrapper = mountFactory({
            propsData: {
                cards: [
                    {
                        title: 'Create an ERD',
                        icon: '$vuetify.icons.mxs_erd',
                        iconSize: 32,
                        click: () => null,
                        disabled: true,
                    },
                ],
            },
        })
        expect(wrapper.findComponent({ name: 'v-card' }).vm.$props.disabled).to.be.true
    })

    const etlTasksRenderTestCases = [
        { hasEtlTasks: true, expectation: 'Should render etl-tasks component' },
        { hasEtlTasks: false, expectation: 'Should not render etl-tasks component' },
    ]
    etlTasksRenderTestCases.forEach(testCase => {
        it(testCase.expectation, () => {
            wrapper = mountFactory({ computed: { hasEtlTasks: () => testCase.hasEtlTasks } })
            const etlTasksComponent = wrapper.findComponent({ name: 'etl-tasks' })
            expect(etlTasksComponent.exists()).to.be[testCase.hasEtlTasks]
        })
    })

    it('Should call setTaskCardCtrHeight on mounted', () => {
        const spy = sinon.spy(BlankWke.methods, 'setTaskCardCtrHeight')
        wrapper = mountFactory()
        spy.should.have.been.calledOnce
    })
})
