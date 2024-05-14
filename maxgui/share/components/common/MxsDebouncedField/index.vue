<template>
    <v-text-field
        v-bind="{ ...$attrs }"
        :value="inputValue"
        @input="handleInputDebounced"
        v-on="filteredListeners"
    />
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
export default {
    name: 'mxs-debounced-field',
    inheritAttrs: false,
    props: {
        debounceTime: { type: Number, default: 500 },
    },
    data() {
        return {
            inputValue: this.$attrs.value,
        }
    },
    computed: {
        filteredListeners() {
            return this.$helpers.lodash.pickBy(this.$listeners, (value, key) => key !== 'input')
        },
    },
    watch: {
        '$attrs.value'(newVal) {
            this.inputValue = newVal
        },
    },
    created() {
        this.debouncedInput = this.$helpers.lodash.debounce(v => {
            this.inputValue = v
            this.$emit('input', v)
        }, this.debounceTime)
    },
    methods: {
        handleInputDebounced(v) {
            this.debouncedInput(v)
        },
    },
}
</script>
