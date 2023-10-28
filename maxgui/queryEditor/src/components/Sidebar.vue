<template>
    <div
        class="sidebar-wrapper d-flex flex-column fill-height mxs-color-helper border-right-table-border"
        :class="{ 'not-allowed': disabled }"
    >
        <div class="sidebar-toolbar" :class="[isCollapsed ? 'pa-1' : 'pa-3']">
            <div class="d-flex align-center justify-center">
                <span
                    v-if="!isCollapsed"
                    class="mxs-color-helper text-small-text sidebar-toolbar__title d-inline-block text-truncate text-capitalize"
                >
                    {{ $mxs_t('schemas') }}
                </span>
                <mxs-tooltip-btn
                    v-if="!isCollapsed"
                    btnClass="reload-schemas"
                    icon
                    small
                    :disabled="reloadDisabled"
                    @click="$emit('reload-schemas')"
                >
                    <template v-slot:btn-content>
                        <v-icon size="12" :color="reloadDisabled ? '' : 'deep-ocean'">
                            $vuetify.icons.mxs_reload
                        </v-icon>
                    </template>
                    {{ $mxs_t('reload') }}
                </mxs-tooltip-btn>
                <mxs-tooltip-btn
                    btnClass="toggle-sidebar"
                    icon
                    small
                    @click="$emit('toggle-sidebar')"
                >
                    <template v-slot:btn-content>
                        <v-icon
                            size="22"
                            color="deep-ocean"
                            class="collapse-icon"
                            :class="[isCollapsed ? 'rotate-right' : 'rotate-left']"
                        >
                            mdi-chevron-double-down
                        </v-icon>
                    </template>
                    {{ isCollapsed ? $mxs_t('expand') : $mxs_t('collapse') }}
                </mxs-tooltip-btn>
            </div>
            <v-text-field
                v-if="!isCollapsed"
                v-model="filterTxt"
                name="searchSchema"
                dense
                outlined
                height="28"
                class="vuetify-input--override filter-objects"
                :placeholder="$mxs_t('filterSchemaObjects')"
                :disabled="!hasConn"
            />
        </div>
        <keep-alive>
            <schema-tree-ctr v-show="!isCollapsed" class="schema-list-ctr" v-on="$listeners" />
        </keep-alive>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
 * Emits
 * $emit('reload-schemas')
 * $emit('toggle-sidebar')
 * $emit('set-search-schema', v?: string)
 */
import SchemaTreeCtr from './SchemaTreeCtr.vue'
export default {
    name: 'sidebar',
    components: { SchemaTreeCtr },
    props: {
        disabled: { type: Boolean, required: true },
        isCollapsed: { type: Boolean, required: true },
        hasConn: { type: Boolean, required: true },
        isLoading: { type: Boolean, required: true },
        searchSchema: { type: String, required: true },
    },
    computed: {
        reloadDisabled() {
            return !this.hasConn || this.isLoading
        },
        filterTxt: {
            get() {
                return this.searchSchema
            },
            set(value) {
                this.$emit('set-search-schema', value)
            },
        },
    },
}
</script>

<style lang="scss" scoped>
.sidebar-wrapper {
    width: 100%;
    .sidebar-toolbar {
        height: 60px;
        padding-top: 2px !important;
        &__title {
            font-size: 12px;
            margin-right: auto;
        }
    }
    .schema-list-ctr {
        font-size: 12px;
        overflow-y: auto;
        z-index: 1;
    }
}
</style>

<style lang="scss">
.vuetify-input--override.filter-objects {
    input {
        font-size: 12px !important;
    }
}
</style>
