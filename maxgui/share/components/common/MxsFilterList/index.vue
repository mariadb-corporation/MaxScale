<template>
    <v-menu
        v-model="isOpened"
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
                    :class="activatorClasses"
                    depressed
                    :color="changeColorOnActive ? 'unset' : activatorColor"
                    v-bind="{ ...attrs, ...btnProps }"
                    v-on="on"
                >
                    <v-icon size="12" class="mr-1">
                        $vuetify.icons.mxs_filter
                    </v-icon>
                    {{ label }}
                    <v-icon size="24" :class="[value ? 'rotate-up' : 'rotate-down']">
                        mdi-menu-down
                    </v-icon>
                </v-btn>
            </slot>
        </template>
        <v-list :max-width="maxWidth" :max-height="maxHeight" class="mxs-filter-list">
            <template v-if="!hideSearch">
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
            </template>
            <template v-if="!hideSelectAll">
                <v-list-item class="px-2" dense link>
                    <v-checkbox
                        dense
                        color="primary"
                        class="pa-0 ma-0 mxs-filter-list__checkbox d-flex align-center"
                        hide-details
                        :label="$mxs_t('selectAll')"
                        :input-value="isAllSelected"
                        :indeterminate="indeterminate"
                        @change="toggleAll"
                    />
                </v-list-item>
                <v-divider />
            </template>
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
                    :input-value="
                        reverse
                            ? !valueModel.includes(returnIndex ? index : item)
                            : valueModel.includes(returnIndex ? index : item)
                    "
                    hide-details
                    @change="toggleItem({ v: $event, item, index })"
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
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
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
        changeColorOnActive: { type: Boolean, default: false },
        activatorProps: {
            type: Object,
            default: () => ({ xSmall: true, outlined: true, color: 'primary' }),
        },
        // reverse the logic, value model contains unselected items
        reverse: { type: Boolean, default: false },
        hideSelectAll: { type: Boolean, default: false },
        hideSearch: { type: Boolean, default: false },
    },
    data() {
        return {
            filterTxt: '',
            isOpened: false,
        }
    },
    computed: {
        valueModel: {
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
        isAllSelected() {
            return this.reverse
                ? this.valueModel.length === 0
                : this.valueModel.length === this.items.length
        },
        indeterminate() {
            if (this.reverse)
                return !(this.valueModel.length === this.items.length || this.isAllSelected)
            return !(this.valueModel.length === 0 || this.isAllSelected)
        },
        activatorClasses() {
            let classes = [this.activatorClass, 'text-capitalize']
            if (this.changeColorOnActive) {
                classes.push('change-color-btn mxs-color-helper')
                if (this.isOpened)
                    classes.push('change-color-btn--active text-primary border-primary')
            }
            return classes
        },
        btnProps() {
            return this.$helpers.lodash.pickBy(this.activatorProps, (v, key) => key !== 'color')
        },
        activatorColor() {
            return this.$typy(this.activatorProps, 'color').safeString || 'primary'
        },
    },
    methods: {
        selectAll() {
            this.valueModel = this.returnIndex
                ? this.items.map((_, i) => i)
                : this.$helpers.lodash.cloneDeep(this.items)
        },
        deselectAll() {
            this.valueModel = []
        },
        toggleAll(v) {
            this.reverse === v ? this.deselectAll() : this.selectAll()
        },
        deselectItem({ item, index }) {
            this.valueModel.splice(this.valueModel.indexOf(this.returnIndex ? index : item), 1)
        },
        selectItem({ item, index }) {
            this.valueModel.push(this.returnIndex ? index : item)
        },
        toggleItem({ v, item, index }) {
            this.reverse === v
                ? this.deselectItem({ item, index })
                : this.selectItem({ item, index })
        },
    },
}
</script>
<style lang="scss" scoped>
.change-color-btn {
    border-color: $text-subtle;
    color: $navigation;
    &:focus::before {
        opacity: 0;
    }
    .v-btn__content .v-icon {
        color: rgba(0, 0, 0, 0.54);
    }
    &--active {
        .v-btn__content .v-icon {
            color: inherit;
        }
    }
}
</style>

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
