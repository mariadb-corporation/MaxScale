<template>
    <div class="log-header d-flex flex-row align-center flex-wrap">
        <span class="mxs-color-helper text-grayed-out d-flex mr-2 align-self-end">
            {{ $mxs_t('logSource') }}: {{ log_source }}
        </span>
        <v-spacer />

        <template v-for="item in filterAttrs">
            <date-range-picker
                v-if="item.key === 'date_range'"
                :key="item.key"
                v-model="item.value"
                v-bind="item.props"
            />
            <v-combobox
                v-else-if="item.key === 'session_ids'"
                :id="item.key"
                :key="item.key"
                v-model="item.value"
                :items="item.value"
                outlined
                class="vuetify-input--override v-select--mariadb mr-2"
                :menu-props="{
                    contentClass: 'v-select--menu-mariadb',
                    bottom: true,
                    offsetY: true,
                }"
                dense
                hide-details
                multiple
                :style="{ maxWidth: '300px' }"
                v-bind="item.props"
                @focus="isSessionIdsInputFocused = true"
                @blur="isSessionIdsInputFocused = false"
            >
                <template v-slot:prepend-inner>
                    <label
                        class="field__label mxs-color-helper text-small-text text-no-wrap ma-0"
                        :for="item.key"
                    >
                        {{ $mxs_t('sessionIDs') }}
                    </label>
                </template>
                <template v-slot:no-data>
                    <v-list-item class="px-3">
                        <i18n path="mxs.info.sessionIDsInputGuide" tag="span">
                            <template v-slot>
                                <kbd class="mx-1 mxs-color-helper bg-code-color"> ENTER </kbd>
                            </template>
                        </i18n>
                    </v-list-item>
                </template>
                <template v-slot:selection="{ item: selectedItem, index }">
                    <template v-if="!isSessionIdsInputFocused">
                        <span
                            v-if="index === 0"
                            class="d-inline-block v-select__selection v-select__selection--comma text-truncate"
                            :style="{ maxWidth: '100px' }"
                        >
                            {{ selectedItem }}
                        </span>
                        <span
                            v-if="index === 1"
                            class="v-select__selection v-select__selection--comma mxs-color-helper text-caption text-grayed-out"
                        >
                            (+{{ item.value.length - 1 }} {{ $mxs_t('others') }})
                        </span>
                    </template>
                </template>
            </v-combobox>
            <mxs-filter-list
                v-else
                :key="item.key"
                v-model="item.value"
                activatorClass="mr-2 font-weight-regular"
                changeColorOnActive
                :activatorProps="filterActivatorBtnProps"
                v-bind="item.props"
            />
        </template>

        <v-btn
            small
            class="ml-2 text-capitalize font-weight-medium"
            outlined
            depressed
            color="primary"
            @click="applyFilter"
        >
            {{ $mxs_t('filter') }}
        </v-btn>
    </div>
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
import { mapMutations, mapState, mapActions } from 'vuex'
import { LOG_PRIORITIES } from '@src/constants'

export default {
    name: 'log-header',
    data() {
        return {
            allModuleIds: [],
            filterAttrs: [],
            isSessionIdsInputFocused: false,
        }
    },
    computed: {
        ...mapState({
            log_source: state => state.maxscale.log_source,
            all_obj_ids: state => state.maxscale.all_obj_ids,
        }),
        filterActivatorBtnProps() {
            return { small: true, outlined: true, color: 'primary' }
        },
    },
    async created() {
        await this.init()
    },
    methods: {
        ...mapMutations({ SET_LOG_FILTER: 'maxscale/SET_LOG_FILTER' }),
        ...mapActions({ fetchAllMxsObjIds: 'maxscale/fetchAllMxsObjIds' }),
        async init() {
            await this.fetchAllMxsObjIds()
            await this.fetchModuleIds()
            this.filterAttrs = [
                {
                    key: 'session_ids',
                    value: [],
                    props: { height: 28 },
                },
                {
                    key: 'obj_ids',
                    value: [],
                    props: {
                        items: this.all_obj_ids,
                        label: this.$mxs_tc('objects', 2),
                        maxHeight: 500,
                    },
                },
                {
                    key: 'module_ids',
                    value: [],
                    props: {
                        items: this.allModuleIds,
                        label: this.$mxs_tc('module', 2),
                        maxHeight: 500,
                    },
                },
                {
                    key: 'priorities',
                    value: [],
                    props: {
                        items: LOG_PRIORITIES,
                        label: this.$mxs_t('priorities'),
                        hideSelectAll: true,
                        hideSearch: true,
                    },
                },
                {
                    key: 'date_range',
                    value: [],
                    props: { height: 28 },
                },
            ]
        },
        async fetchModuleIds() {
            const { $helpers, $http } = this
            // use an uid to ensure the result includes only ids
            const [, res] = await $helpers.tryAsync(
                $http.get(`/maxscale/modules?load=all&fields[modules]=${$helpers.uuidv1()}`)
            )
            this.allModuleIds = res.data.data.map(item => item.id)
        },
        applyFilter() {
            // deep clone the filterAttrs to break obj reference
            let filterAttrs = this.$helpers.lodash.cloneDeep(this.filterAttrs)
            this.SET_LOG_FILTER(
                filterAttrs.reduce((res, item) => ((res[item.key] = item.value), res), {})
            )
        },
    },
}
</script>
