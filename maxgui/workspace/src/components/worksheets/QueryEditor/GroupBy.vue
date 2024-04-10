<template>
    <v-menu
        allow-overflow
        transition="slide-y-transition"
        offset-y
        left
        content-class="v-menu--mariadb v-menu--mariadb-full-border group-by-dropdown"
        :close-on-content-click="false"
    >
        <template v-slot:activator="{ on, attrs, value }">
            <v-btn
                x-small
                class="text-capitalize font-weight-medium"
                outlined
                depressed
                color="primary"
                :disabled="disabled"
                v-bind="attrs"
                v-on="on"
            >
                {{ $mxs_t('groupBy') }}
                <v-icon size="24" color="primary" :class="[value ? 'rotate-up' : 'rotate-down']">
                    mdi-menu-down
                </v-icon>
            </v-btn>
        </template>
        <v-list max-width="220px" :max-height="maxHeight" class="group-by-list">
            <v-list-item class="px-0" dense>
                <v-text-field
                    v-model="filterTxt"
                    dense
                    outlined
                    height="36"
                    class="vuetify-input--override group-by-list__search"
                    :placeholder="$mxs_t('search')"
                    hide-details
                />
            </v-list-item>
            <v-divider />
            <v-list-item v-for="(item, index) in itemsList" :key="index" class="px-2" dense link>
                <v-checkbox
                    dense
                    color="primary"
                    class="pa-0 ma-0 group-by-list__checkbox d-flex align-center"
                    :input-value="chosenIndex === index"
                    hide-details
                    @change="toggleItem($event, index)"
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
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'group-by',
    props: {
        value: { type: Number, required: true },
        items: { type: Array, required: true }, // array of strings
        maxHeight: { type: Number, default: 400 },
        disabled: { type: Boolean, default: false },
    },
    data() {
        return {
            filterTxt: '',
        }
    },
    computed: {
        chosenIndex: {
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
    },
    methods: {
        toggleItem(isChecked, index) {
            if (isChecked) this.chosenIndex = index
            else this.chosenIndex = -1
        },
    },
}
</script>

<style lang="scss">
.group-by-dropdown {
    .group-by-list {
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
}
</style>
