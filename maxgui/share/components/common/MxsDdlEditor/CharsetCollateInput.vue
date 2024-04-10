<template>
    <mxs-lazy-input
        v-model="isInputShown"
        :inputValue="inputValue"
        :height="height"
        :disabled="isDisabled"
        :placeholder="isDisabled ? '' : CREATE_TBL_TOKENS.default"
        type="select"
        :name="name"
        :getInputRef="() => $typy($refs, 'inputCtr.$children[0]').safeObject"
    >
        <charset-collate-select
            ref="inputCtr"
            v-model="inputValue"
            :items="
                isCharsetInput
                    ? Object.keys(charsetCollationMap)
                    : $typy(charsetCollationMap, `[${columnCharset}].collations`).safeArray
            "
            :defItem="defItem"
            :cache-items="isCharsetInput"
            :disabled="isDisabled"
            :height="height"
            :placeholder="CREATE_TBL_TOKENS.default"
            @blur="onBlur"
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
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import CharsetCollateSelect from '@share/components/common/MxsDdlEditor/CharsetCollateSelect.vue'
import { checkCharsetSupport } from '@share/components/common/MxsDdlEditor/utils'
import { CREATE_TBL_TOKENS, COL_ATTRS, COL_ATTRS_IDX_MAP } from '@wsSrc/constants'

export default {
    name: 'charset-collate-input',
    components: { CharsetCollateSelect },
    props: {
        value: { type: String, required: true },
        rowData: { type: Array, required: true },
        field: { type: String, required: true },
        height: { type: Number, required: true },
        charsetCollationMap: { type: Object, required: true },
        defTblCharset: { type: String, default: '' },
        defTblCollation: { type: String, default: '' },
    },
    data() {
        return {
            isInputShown: false,
        }
    },
    computed: {
        columnCharset() {
            return (
                this.$typy(this.rowData, `[${COL_ATTRS_IDX_MAP[COL_ATTRS.CHARSET]}]`).safeString ||
                this.defTblCharset
            )
        },
        columnType() {
            return this.$typy(this.rowData, `[${COL_ATTRS_IDX_MAP[COL_ATTRS.TYPE]}]`).safeString
        },
        isCharsetInput() {
            return this.field === COL_ATTRS.CHARSET
        },
        name() {
            return this.isCharsetInput ? 'charset' : 'collation'
        },
        isDisabled() {
            if (this.columnType.includes('NATIONAL')) return true
            return !checkCharsetSupport(this.columnType)
        },
        defItem() {
            return this.isCharsetInput ? this.defTblCharset : this.defTblCollation
        },
        inputValue: {
            get() {
                return this.value
            },
            set(v) {
                if (v != this.inputValue) this.$emit('on-input', v)
            },
        },
    },
    created() {
        this.CREATE_TBL_TOKENS = CREATE_TBL_TOKENS
    },
    methods: {
        onBlur(e) {
            this.inputValue = e.srcElement.value
            this.isInputShown = false
        },
    },
}
</script>
