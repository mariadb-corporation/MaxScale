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
                    class="text-capitalize font-weight-medium"
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
        <v-list max-width="220px" :max-height="maxHeight" class="mxs-filter-list">
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
                    :input-value="isAllSelected"
                    @change="toggleAll"
                />
            </v-list-item>
            <v-divider />
            <v-list-item v-for="item in itemsList" :key="`${item.text}`" class="px-2" dense link>
                <!-- value of checkbox cannot be object, so using text then get object via itemsMapByText -->
                <v-checkbox
                    v-model="selectedItems"
                    dense
                    color="primary"
                    class="pa-0 ma-0 mxs-filter-list__checkbox d-flex align-center"
                    :value="item.text"
                    hide-details
                >
                    <template v-slot:label>
                        <mxs-truncate-str
                            v-mxs-highlighter="{ keyword: filterTxt, txt: item.text }"
                            :tooltipItem="{ txt: `${item.text}` }"
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
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
 *
 items?: Array of objects. This props accepts `mxs-virtual-scroll-tbl` headers
 or at least the object needs to have `text` property in order to make the search
 filter work.
 returnObject?: boolean, by default this component returns selected index, if true,
 it returns selected objects.
 */
export default {
    name: 'mxs-filter-list',
    props: {
        value: { type: Array, required: true },
        label: { type: String, required: true },
        items: {
            type: Array,
            validator: arr => {
                if (!arr.length) return true
                else return arr.filter(item => 'text' in item).length === arr.length
            },
            required: true,
        },
        maxHeight: { type: Number, required: true },
        returnObject: { type: Boolean, default: false },
        selectAllOnActivated: { type: Boolean, default: false },
    },
    data() {
        return {
            filterTxt: '',
        }
    },
    computed: {
        itemsMapByText() {
            const map = {}
            this.items.forEach(col => {
                map[col.text] = col
            })
            return map
        },
        selectedItems: {
            get() {
                if (this.returnObject) return this.value.map(col => col.text)
                else {
                    // this.value is array of indexes
                    let items = []
                    this.items.forEach((c, i) => {
                        if (this.value.includes(i)) items.push(c.text)
                    })
                    return items
                }
            },
            set(arr) {
                // arr is an array of strings
                if (this.returnObject) {
                    const items = arr.reduce((arr, name) => {
                        arr.push(this.itemsMapByText[name])
                        return arr
                    }, [])
                    this.$emit('input', items) // emit array of the original objects from this.items
                } else {
                    let idxs = []
                    this.items.forEach((h, i) => {
                        if (arr.includes(h.text)) idxs.push(i)
                    })
                    this.$emit('input', idxs) // emit array of indexes
                }
            },
        },
        itemsList() {
            let list = this.$helpers.lodash.cloneDeep(this.items)
            return list.filter(obj => this.$helpers.ciStrIncludes(`${obj.text}`, this.filterTxt))
        },
        isAllSelected() {
            return this.selectedItems.length === this.items.length
        },
    },
    activated() {
        if (this.selectAllOnActivated) this.selectAll()
    },
    methods: {
        toggleAll(v) {
            if (!v) this.selectedItems = []
            else this.selectAll()
        },
        selectAll() {
            //value of checkbox cannot be object, so using text here
            this.selectedItems = this.items.map(h => h.text)
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
