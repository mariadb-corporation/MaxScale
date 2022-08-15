<template>
    <div
        class="sidebar-wrapper d-flex flex-column fill-height color border-right-table-border"
        :class="{ 'not-allowed': disabled }"
    >
        <div class="sidebar-toolbar" :class="[isCollapsed ? 'pa-1' : 'pa-3']">
            <div class="d-flex align-center justify-center">
                <span
                    v-if="!isCollapsed"
                    class="color text-small-text sidebar-toolbar__title d-inline-block text-truncate text-uppercase"
                >
                    {{ $t('schemas') }}
                </span>
                <v-tooltip
                    v-if="!isCollapsed"
                    top
                    transition="slide-y-transition"
                    content-class="shadow-drop color text-navigation py-1 px-4"
                >
                    <template v-slot:activator="{ on }">
                        <v-btn
                            icon
                            small
                            :disabled="reloadDisabled"
                            class="reload-schemas"
                            v-on="on"
                            @click="$emit('reload-schemas')"
                        >
                            <v-icon size="12" :color="reloadDisabled ? '' : 'deep-ocean'">
                                $vuetify.icons.reload
                            </v-icon>
                        </v-btn>
                    </template>
                    <span>{{ $t('reload') }}</span>
                </v-tooltip>
                <v-tooltip
                    top
                    transition="slide-y-transition"
                    content-class="shadow-drop color text-navigation py-1 px-4"
                >
                    <template v-slot:activator="{ on }">
                        <v-btn
                            icon
                            small
                            class="toggle-sidebar"
                            v-on="on"
                            @click="$emit('toggle-sidebar')"
                        >
                            <v-icon
                                size="22"
                                color="deep-ocean"
                                class="collapse-icon"
                                :class="[isCollapsed ? 'rotate-right' : 'rotate-left']"
                            >
                                mdi-chevron-double-down
                            </v-icon>
                        </v-btn>
                    </template>
                    <span>{{ isCollapsed ? $t('expand') : $t('collapse') }}</span>
                </v-tooltip>
            </div>
            <v-text-field
                v-if="!isCollapsed"
                v-model="filterTxt"
                name="searchSchema"
                dense
                outlined
                height="28"
                class="vuetify-input--override filter-objects"
                :placeholder="$t('filterSchemaObjects')"
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
 * Change Date: 2026-08-08
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
import SchemaTree from './SchemaTree.container.vue'
export default {
    name: 'sidebar',
    components: { 'schema-tree-ctr': SchemaTree },
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
