<template>
    <v-menu
        allow-overflow
        transition="slide-y-transition"
        offset-y
        left
        content-class="mariadb-select-v-menu mariadb-select-v-menu--full-border"
        :close-on-content-click="false"
    >
        <template v-slot:activator="{ on, attrs, value }">
            <v-btn
                x-small
                class="text-capitalize font-weight-medium"
                outlined
                depressed
                color="accent-dark"
                v-bind="attrs"
                v-on="on"
            >
                {{ label }}
                <v-icon
                    size="24"
                    color="accent-dark"
                    :class="{ 'column-list-toggle--active': value }"
                >
                    arrow_drop_down
                </v-icon>
            </v-btn>
        </template>
        <v-list max-width="220px" :max-height="maxHeight" class="column-list">
            <v-list-item class="px-0" dense>
                <v-text-field
                    v-model="filterHeader"
                    dense
                    outlined
                    height="36"
                    class="std column-list__search"
                    :placeholder="$t('search')"
                    hide-details
                />
            </v-list-item>
            <v-divider />

            <v-list-item class="px-2" dense link>
                <v-checkbox
                    dense
                    color="primary"
                    class="pa-0 ma-0 checkbox d-flex align-center"
                    hide-details
                    :label="$t('selectAll')"
                    :input-value="isAllHeaderChecked"
                    @change="toggleAllHeaders"
                />
            </v-list-item>
            <v-divider />
            <v-list-item
                v-for="item in columnList"
                :key="`${item.name}_${item.index}`"
                class="px-2"
                dense
                link
            >
                <v-checkbox
                    v-model="selectedIdxs"
                    dense
                    color="primary"
                    class="pa-0 ma-0 checkbox d-flex align-center"
                    :value="item.index"
                    hide-details
                >
                    <template v-slot:label>
                        <truncate-string :text="item.name" />
                    </template>
                </v-checkbox>
            </v-list-item>
        </v-list>
    </v-menu>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'columns-list',
    props: {
        value: { type: Array, required: true },
        label: { type: String, required: true },
        cols: { type: Array, required: true },
        maxHeight: { type: Number, required: true },
    },
    data() {
        return {
            filterHeader: '',
        }
    },
    computed: {
        selectedIdxs: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
        columnList() {
            let list = this.$help.lodash.cloneDeep(this.cols).map((h, i) => ({ index: i, name: h }))
            return list.filter(obj => this.$help.ciStrIncludes(`${obj.name}`, this.filterHeader))
        },
        isAllHeaderChecked() {
            return this.selectedIdxs.length === this.cols.length
        },
    },
    created() {
        this.showAllHeaders()
    },
    methods: {
        toggleAllHeaders(v) {
            if (!v) this.selectedIdxs = []
            else this.showAllHeaders()
        },
        showAllHeaders() {
            this.selectedIdxs = this.cols.map((h, i) => i)
        },
    },
}
</script>

<style lang="scss" scoped>
.column-list-toggle--active {
    transform: rotate(180deg);
    transition: 0.3s cubic-bezier(0.25, 0.8, 0.5, 1), visibility 0s;
}
.column-list {
    overflow-y: auto;
    &__search {
        ::v-deep .v-input__control {
            fieldset {
                border: none !important;
            }
        }
    }
}
::v-deep.checkbox {
    width: 100%;
    height: 36px;
    label {
        height: 36px !important;
        font-size: 0.875rem;
        color: $navigation;
        display: inline-block !important;
        white-space: nowrap !important;
        overflow: hidden !important;
        text-overflow: ellipsis !important;
        line-height: 36px;
    }
}
</style>
