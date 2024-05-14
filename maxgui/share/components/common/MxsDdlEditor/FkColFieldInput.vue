<template>
    <lazy-select
        :value="inputValue"
        :height="height"
        :items="items"
        item-text="text"
        item-value="id"
        :name="field"
        :selectionText="selectionText"
        :multiple="true"
        :required="true"
        :rules="[v => Boolean(v.length)]"
        @on-input="inputValue = $event"
    >
        <template v-slot:item="{ item }">
            <span class="col-order pr-2" :class="{ 'col-order--visible': getColOrder(item) > 0 }">
                {{ getColOrder(item) }}
            </span>
            <v-icon class="mr-3">
                {{
                    inputValue.includes(item.id)
                        ? 'mdi-checkbox-marked'
                        : 'mdi-checkbox-blank-outline'
                }}
            </v-icon>
            {{ item.text }}
            <v-spacer />

            <span class="ma-0 ml-auto pl-2 field__label mxs-color-helper text-small-text">
                {{ item.type }}
            </span>
        </template>
        <template v-slot:selection="{ item, index }">
            <span class="v-select__selection v-select__selection--comma mr-0">
                <template v-if="index === 0"> {{ item.text }}</template>
                <template v-else-if="index === 1">
                    &nbsp;(+{{ inputValue.length - 1 }} {{ $mxs_t('others') }})
                </template>
            </span>
        </template>
    </lazy-select>
</template>

<script>
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
import LazySelect from '@share/components/common/MxsDdlEditor/LazySelect'
import { FK_EDITOR_ATTRS } from '@wsSrc/constants'

export default {
    name: 'fk-col-field-input',
    components: { LazySelect },
    props: {
        value: { type: Array, required: true },
        field: { type: String, required: true },
        height: { type: Number, required: true },
        referencingColOptions: { type: Array, required: true },
        refColOpts: { type: Array, required: true },
    },
    computed: {
        inputValue: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('on-input', v)
            },
        },
        items() {
            const { COLS, REF_COLS } = FK_EDITOR_ATTRS
            switch (this.field) {
                case COLS:
                    return this.referencingColOptions
                case REF_COLS:
                    return this.refColOpts
                default:
                    return []
            }
        },
        itemMap() {
            return this.$helpers.lodash.keyBy(this.items, 'id')
        },
        selectionText() {
            const selectedNames = this.inputValue.map(
                id => this.$typy(this.itemMap, `[${id}]text`).safeString
            )
            if (selectedNames.length > 1) {
                return `${selectedNames[0]} (+${selectedNames.length - 1} ${this.$mxs_t('others')})`
            }
            return this.$typy(selectedNames, '[0]').safeString
        },
    },
    methods: {
        getColOrder(col) {
            return this.inputValue.findIndex(id => id === col.id) + 1
        },
    },
}
</script>

<style lang="scss" scoped>
.col-order {
    visibility: hidden;
    &--visible {
        visibility: visible;
    }
}
</style>
