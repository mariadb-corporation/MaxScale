<template>
    <div :class="[isLeftPaneCollapsed ? 'pa-1' : 'pa-3']">
        <portal to="toggle-pane">
            <v-btn icon small @click="toggleExpand">
                <v-icon
                    size="16"
                    color="deep-ocean"
                    class="collapse-icon"
                    :class="{ 'collapse-icon--active': isLeftPaneCollapsed }"
                >
                    double_arrow
                </v-icon>
            </v-btn>
        </portal>
        <div class="visible-when-expand fill-height">
            <div class="schema-list-tools">
                <div class="d-flex align-center justify-end">
                    <span
                        v-if="!isLeftPaneCollapsed"
                        class="color text-small-text db-tb-list__title d-inline-block text-truncate"
                    >
                        SCHEMAS
                    </span>
                    <v-btn v-if="!isLeftPaneCollapsed" icon small @click="reloadSchema">
                        <v-icon size="12" color="deep-ocean">
                            $vuetify.icons.reload
                        </v-icon>
                    </v-btn>
                    <portal-target name="toggle-pane" />
                </div>
                <v-text-field
                    v-if="!isLeftPaneCollapsed"
                    id="searchSchema"
                    v-model="searchSchema"
                    name="searchSchema"
                    required
                    dense
                    outlined
                    height="28"
                    class="std filter-objects"
                    placeholder="Filter schema objects"
                />
            </div>
            <div v-if="!isLeftPaneCollapsed" class="schema-list-wrapper">
                <!-- TODO: Restyle and add more functionalities -->
                <v-treeview
                    :items="schemaList"
                    :search="searchSchema"
                    :filter="filter"
                    :open.sync="activeNodes"
                    hoverable
                    dense
                    open-on-click
                    transition
                >
                    <template v-slot:label="{ item }">
                        <!-- TODO:
                        Create truncate-string component to truncate text and show its full text in a tooltip
                        -->
                        <div>
                            <span class="inline-block text-truncate">
                                {{ item.name }}
                                <span v-if="item.dataType" class="ml-1 color text-field-text">
                                    {{ item.dataType }}
                                </span>
                            </span>
                        </div>
                    </template>
                    <template v-slot:append="{ item }">
                        <v-menu
                            transition="slide-y-transition"
                            offset-y
                            content-class="setting-menu"
                        >
                            <template v-slot:activator="{ on, attrs }">
                                <!-- TODO: Only show option icon when hover on node -->
                                <v-btn icon x-small v-bind="attrs" v-on="on" @click="openNodeMenu">
                                    <v-icon size="12" color="deep-ocean">
                                        more_horiz
                                    </v-icon>
                                </v-btn>
                            </template>
                            <v-list>
                                <v-list-item
                                    v-for="option in tableOptions"
                                    :key="option"
                                    dense
                                    link
                                >
                                    <v-list-item-title
                                        @click="() => optionHandler({ item, option })"
                                        v-text="option"
                                    />
                                </v-list-item>
                            </v-list>
                        </v-menu>
                    </template>
                </v-treeview>
            </div>
        </div>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'db-list',
    props: {
        connSchema: { type: Object, required: true },
        loadingSchema: { type: Boolean, required: true },
    },
    data() {
        return {
            searchSchema: '',
            isLeftPaneCollapsed: false,
            activeNodes: [],
            tableOptions: ['Preview Data', 'View Details', 'Place Name in SQL'],
        }
    },
    computed: {
        schemaList() {
            if (this.loadingSchema) return []
            const { schemas = [] } = this.connSchema
            let schemaList = schemas.map(({ name: schemaId, tables = [] }) => {
                let schemaObj = {
                    type: 'schema',
                    name: schemaId,
                    id: schemaId,
                    children: [],
                }
                schemaObj.children = tables.map(({ name: tableName, columns = [] }) => {
                    const tableId = `${schemaObj.id}.${tableName}`
                    return {
                        type: 'table',
                        name: tableName,
                        id: tableId,
                        level: 1,
                        children: columns.map(({ name: columnName, dataType }) => ({
                            type: 'column',
                            name: columnName,
                            dataType: dataType,
                            id: `${tableId}.${columnName}`,
                            level: 2,
                        })),
                    }
                })
                return schemaObj
            })
            return schemaList
        },
        filter() {
            return (item, search, textKey) => item[textKey].indexOf(search) > -1
        },
    },
    watch: {
        isLeftPaneCollapsed(v) {
            this.$emit('is-collapsed', v)
        },
        schemaList: {
            deep: true,
            handler(v) {
                if (v.length) this.activeNodes.push(v[0].id)
            },
        },
    },
    methods: {
        reloadSchema() {
            this.$emit('reload-schema')
        },
        toggleExpand() {
            this.isLeftPaneCollapsed = !this.isLeftPaneCollapsed
        },
        openNodeMenu(e) {
            e.stopPropagation()
        },
        optionHandler({ item, option }) {
            /* eslint-disable no-console */
            console.log(item)
            console.log(option)
            //TODO: emits event to parent
        },
    },
}
</script>

<style lang="scss" scoped>
.db-tb-list__title {
    font-size: 12px;
    margin-right: auto;
}
.collapse-icon {
    transform: rotate(-180deg);
    &--active {
        transform: rotate(0deg);
    }
}
::v-deep .std.filter-objects {
    input {
        font-size: 12px;
    }
}
$tools-height: 60px;
.schema-list-tools {
    height: $tools-height;
}
::v-deep .schema-list-wrapper {
    font-size: 12px;
    max-height: calc(100% - #{$tools-height});
    overflow-y: auto;
    z-index: 1;
}
</style>
