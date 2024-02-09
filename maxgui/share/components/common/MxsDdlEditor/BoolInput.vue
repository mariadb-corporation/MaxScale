<template>
    <mxs-lazy-input
        v-model="isInputShown"
        :inputValue="inputValue"
        type="checkbox"
        :height="height"
        :name="field"
        :disabled="isDisabled"
        :getInputRef="() => $typy($refs, 'inputCtr').safeObject"
        @toggle-checkbox="toggle"
    >
        <v-checkbox
            ref="inputCtr"
            v-model="inputValue"
            class="v-checkbox--mariadb-xs ma-0 pa-0"
            primary
            hide-details
            :disabled="isDisabled"
        />
    </mxs-lazy-input>
</template>

<script>
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
import {
    checkUniqueZeroFillSupport,
    checkAutoIncrementSupport,
} from '@share/components/common/MxsDdlEditor/utils'
import { COL_ATTRS, COL_ATTRS_IDX_MAP, GENERATED_TYPES } from '@wsSrc/constants'

export default {
    name: 'bool-input',
    props: {
        value: { type: Boolean, required: true },
        rowData: { type: Array, required: true },
        field: { type: String, required: true },
        height: { type: Number, required: true },
    },
    data() {
        return {
            isInputShown: false,
        }
    },
    computed: {
        colData() {
            const { TYPE, PK, AI, GENERATED } = COL_ATTRS
            return {
                type: this.$typy(this.rowData, `[${COL_ATTRS_IDX_MAP[TYPE]}]`).safeString,
                isPK: this.$typy(this.rowData, `[${COL_ATTRS_IDX_MAP[PK]}]`).safeBoolean,
                isAI: this.$typy(this.rowData, `[${COL_ATTRS_IDX_MAP[AI]}]`).safeBoolean,
                isGenerated:
                    this.$typy(this.rowData, `[${COL_ATTRS_IDX_MAP[GENERATED]}]`).safeString !==
                    GENERATED_TYPES.NONE,
            }
        },
        isDisabled() {
            const { PK, NN, UN, UQ, ZF, AI } = COL_ATTRS
            switch (this.field) {
                case PK:
                    //disable if column is generated
                    return this.colData.isGenerated
                case UN:
                case ZF:
                    return !checkUniqueZeroFillSupport(this.colData.type)
                case AI:
                    return !checkAutoIncrementSupport(this.colData.type)
                case NN:
                    //isAI or isPK implies NOT NULL so must be disabled
                    // when column is generated, NN or NULL can not be defined
                    return this.colData.isAI || this.colData.isPK || this.isGenerated
                case UQ:
                    return this.colData.isPK // implies UNIQUE already so UQ must be disabled
                default:
                    return false
            }
        },
        inputValue: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('on-input', v)
            },
        },
    },
    methods: {
        toggle() {
            this.inputValue = !this.inputValue
            this.isInputShown = false
        },
    },
}
</script>
