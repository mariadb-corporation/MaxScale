<template>
    <v-menu
        allow-overflow
        transition="slide-y-transition"
        offset-y
        left
        content-class="v-menu--mariadb v-menu--mariadb-full-border"
        :close-on-content-click="false"
    >
        <template v-slot:activator="{ on, attrs, value }">
            <slot name="activator" :data="{ on, attrs, value, label }">
                <v-btn
                    x-small
                    :class="`text-capitalize font-weight-medium ${activatorClass}`"
                    outlined
                    depressed
                    color="primary"
                    v-bind="attrs"
                    v-on="on"
                >
                    <v-icon size="12" color="primary" class="mr-1">
                        $vuetify.icons.mxs_filter
                    </v-icon>
                    {{ label }}
                    <v-icon
                        size="24"
                        color="primary"
                        :class="[value ? 'rotate-up' : 'rotate-down']"
                    >
                        mdi-menu-down
                    </v-icon>
                </v-btn>
            </slot>
        </template>
        <v-list :max-width="maxWidth" :max-height="maxHeight" class="mxs-filter-list">
            <v-list-item class="px-0" dense>
                <v-text-field
                    v-model="filterTxt"
                    dense
                    outlined
                    height="36"
                    class="vuetify-input--override mxs-filter-list__search"
                    :placeholder="$mxs_t('search')"
                    hide-details
                />
            </v-list-item>
            <v-divider />
            <v-list-item class="px-2" dense link>
                <v-checkbox
                    dense
                    color="primary"
                    class="pa-0 ma-0 mxs-filter-list__checkbox d-flex align-center"
                    hide-details
                    :label="$mxs_t('selectAll')"
                    :input-value="isAllTicked"
                    :indeterminate="indeterminate"
                    @change="toggleAll"
                />
            </v-list-item>
            <v-divider />
            <v-list-item
                v-for="(item, index) in itemsList"
                :key="`${index}`"
                class="px-2"
                dense
                link
            >
                <v-checkbox
                    dense
                    color="primary"
                    class="pa-0 ma-0 mxs-filter-list__checkbox d-flex align-center"
                    :input-value="!untickedItems.includes(returnIndex ? index : item)"
                    hide-details
                    @change="toggleItem($event, item, index)"
                >
                    <template v-slot:label>
                        <mxs-truncate-str
                            v-mxs-highlighter="{ keyword: filterTxt, txt: item }"
                            :tooltipItem="{ txt: `${item}` }"
                        />
                    </template>
                </v-checkbox>
            </v-list-item>
        </v-list>
    </v-menu>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/**
 * All items are ticked by default, the value model contains nonticked items !!!
 */
export default {
    name: 'mxs-filter-list',
    props: {
        value: { type: Array, required: true },
        label: { type: String, default: '' },
        items: { type: Array, required: true }, // array of strings
        maxHeight: { type: [Number, String], default: 'unset' },
        maxWidth: { type: [Number, String], default: '220px' },
        activatorClass: { type: String, default: '' },
        returnIndex: { type: Boolean, default: false },
    },
    data() {
        return {
            filterTxt: '',
        }
    },
    computed: {
        untickedItems: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
        itemsList() {
            return this.items.filter(str => this.$helpers.ciStrIncludes(`${str}`, this.filterTxt))
        },
        isAllTicked() {
            return this.untickedItems.length === 0
        },
        indeterminate() {
            if (this.untickedItems.length === this.items.length) return false
            return !this.isAllTicked
        },
    },
    methods: {
        toggleAll(v) {
            if (v) this.untickedItems = []
            else
                this.untickedItems = this.returnIndex
                    ? this.items.map((_, i) => i)
                    : this.$helpers.lodash.cloneDeep(this.items)
        },
        toggleItem(isChecked, item, index) {
            if (isChecked)
                this.untickedItems.splice(
                    this.untickedItems.indexOf(this.returnIndex ? index : item),
                    1
                )
            else this.untickedItems.push(this.returnIndex ? index : item)
        },
    },
}
</script>

<style lang="scss">
.mxs-filter-list {
    overflow-y: auto;
    &__search {
        .v-input__control {
            fieldset {
                border: none !important;
            }
        }
    }
    &__checkbox {
        width: 100%;
        height: 36px;
        .v-label {
            font-size: 0.875rem;
            color: $navigation;
            white-space: nowrap;
            overflow: hidden;
            text-overflow: ellipsis;
            line-height: 36px;
        }
    }
}
</style>
