<template>
    <v-combobox
        v-if="data.field === COL_ATTRS.TYPE"
        v-model="inputValue"
        class="vuetify-input--override v-select--mariadb error--text__bottom error--text__bottom--no-margin"
        :class="data.field"
        :menu-props="{
            contentClass: 'v-select--menu-mariadb',
            bottom: true,
            offsetY: true,
        }"
        :items="dataTypes"
        item-text="value"
        item-value="value"
        outlined
        dense
        :height="height"
        hide-details="auto"
        :return-object="false"
    />
    <v-select
        v-else-if="data.field === COL_ATTRS.GENERATED_TYPE"
        v-model="inputValue"
        class="vuetify-input--override v-select--mariadb error--text__bottom error--text__bottom--no-margin"
        :class="data.field"
        :menu-props="{
            contentClass: 'v-select--menu-mariadb',
            bottom: true,
            offsetY: true,
        }"
        :items="Object.values(GENERATED_TYPES)"
        outlined
        dense
        :height="height"
        hide-details="auto"
        :disabled="isDisabled"
    />
    <v-checkbox
        v-else-if="$typy(inputValue).isBoolean"
        v-model="inputValue"
        dense
        class="v-checkbox--mariadb-xs ma-0 pa-0"
        primary
        hide-details
        :disabled="isDisabled"
    />
    <charset-collate-select
        v-else-if="data.field === COL_ATTRS.CHARSET || data.field === COL_ATTRS.COLLATE"
        v-model="inputValue"
        :items="
            data.field === COL_ATTRS.CHARSET
                ? Object.keys(charsetCollationMap)
                : $typy(charsetCollationMap, `[${columnCharset}].collations`).safeArray
        "
        :defItem="data.field === COL_ATTRS.CHARSET ? defTblCharset : defTblCollation"
        :disabled="isDisabled"
        :height="height"
    />
    <v-text-field
        v-else
        v-model="inputValue"
        class="vuetify-input--override error--text__bottom error--text__bottom--no-margin"
        :class="`${data.field}`"
        single-line
        outlined
        dense
        :height="height"
        hide-details="auto"
    />
</template>

<script>
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
/*
 *
 * data: {
 *  field?: string, header name
 *  value?: string, cell value
 *  rowObj?: object, entire row data
 *  alterColIdx?: number, index of the column being altered
 *  colOptIdx?: number, index of the column option. e.g. index of PK, NN, UN ,...
 * }
 */
import { mapState } from 'vuex'
import CharsetCollateSelect from '@wsSrc/components/common/MxsDdlEditor/CharsetCollateSelect.vue'
import {
    check_charset_support,
    check_UN_ZF_support,
    check_AI_support,
} from '@wsSrc/components/common/MxsDdlEditor/utils'
export default {
    name: 'col-opt-input',
    components: {
        CharsetCollateSelect,
    },
    props: {
        data: { type: Object, required: true },
        height: { type: Number, required: true },
        charsetCollationMap: { type: Object, required: true },
        defTblCharset: { type: String, default: '' },
        defTblCollation: { type: String, default: '' },
        dataTypes: { type: Array, default: () => [] },
    },
    computed: {
        ...mapState({
            COL_ATTRS: state => state.mxsWorkspace.config.COL_ATTRS,
            GENERATED_TYPES: state => state.mxsWorkspace.config.GENERATED_TYPES,
        }),
        columnCharset() {
            return this.$typy(this.data, `rowObj.${this.COL_ATTRS.CHARSET}`).safeString
        },
        columnType() {
            return this.$typy(this.data, `rowObj.${this.COL_ATTRS.TYPE}`).safeString
        },
        isPK() {
            return this.$typy(this.data, `rowObj.${this.COL_ATTRS.PK}`).safeBoolean
        },
        isGenerated() {
            return (
                this.$typy(this.data, `rowObj.${this.COL_ATTRS.GENERATED_TYPE}`).safeString !==
                this.GENERATED_TYPES.NONE
            )
        },
        isAI() {
            return this.$typy(this.data, `rowObj.${this.COL_ATTRS.AI}`).safeBoolean
        },
        isDisabled() {
            const { PK, NN, UN, UQ, ZF, AI, GENERATED_TYPE, CHARSET, COLLATE } = this.COL_ATTRS
            switch (this.$typy(this.data, 'field').safeString) {
                case CHARSET:
                case COLLATE:
                    if (this.columnCharset === 'utf8') return true
                    return !check_charset_support(this.columnType)
                case PK:
                    //disable if column is generated
                    return this.isGenerated
                case UN:
                case ZF:
                    return !check_UN_ZF_support(this.columnType)
                case AI:
                    return !check_AI_support(this.columnType)
                case NN:
                    //isAI or isPK implies NOT NULL so must be disabled
                    // when column is generated, NN or NULL can not be defined
                    return this.isAI || this.isPK || this.isGenerated
                case UQ:
                    return this.isPK // implies UNIQUE already so UQ must be disabled
                case GENERATED_TYPE:
                    //disable if column is PK
                    return this.isPK //https://mariadb.com/kb/en/generated-columns/#index-support
                default:
                    return false
            }
        },
        inputValue: {
            get() {
                return this.data.value
            },
            set(v) {
                this.$emit('on-input', { ...this.data, value: v })
            },
        },
    },
}
</script>
