<template>
    <!-- TODO: Virtual scroll treeview -->
    <div>
        <m-treeview
            :items="schemaList"
            :search="$parent.searchSchema"
            :filter="filter"
            hoverable
            dense
            open-on-click
            transition
            :load-children="handleLoadChildren"
            @item:contextmenu="onContextMenu"
            @item:hovered="hoveredItem = $event"
        >
            <template v-slot:label="{ item }">
                <div
                    :id="`item-label-${activatorIdTransform(item.id)}`"
                    class="d-flex align-center node-label"
                >
                    <v-icon class="mr-1" size="12" color="deep-ocean">
                        {{ iconSheet(item) }}
                    </v-icon>
                    <span class="text-truncate d-inline-block">{{ item.name }}</span>
                </div>
            </template>

            <template v-slot:append="{ isHover, item }">
                <v-btn
                    v-show="isHover || showCtxBtn(item)"
                    :id="activatorIdTransform(item.id)"
                    icon
                    x-small
                    @click="e => handleOpenCtxMenu({ e, item })"
                >
                    <v-icon size="12" color="deep-ocean">more_horiz</v-icon>
                </v-btn>
            </template>
        </m-treeview>
        <v-tooltip
            v-if="hoveredItem"
            :value="Boolean(hoveredItem)"
            right
            :nudge-right="45"
            transition="slide-x-transition"
            content-class="shadow-drop"
            :activator="`#item-label-${activatorIdTransform(hoveredItem.id)}`"
        >
            <v-list class="mariadb-v-list" dense>
                <v-list-item
                    v-for="(value, key) in $help.lodash.pick(hoveredItem, [
                        'type',
                        'name',
                        'dataType',
                    ])"
                    :key="key"
                    class="color text-text"
                    dense
                >
                    <v-list-item-title>
                        <span class="font-weight-bold text-capitalize"> {{ key }}: </span>
                        <span> {{ value }}</span>
                    </v-list-item-title>
                </v-list-item>
            </v-list>
        </v-tooltip>
        <v-menu
            v-if="activeCtxItem"
            :key="activeCtxItem.id"
            v-model="showCtxMenu"
            transition="slide-y-transition"
            left
            nudge-right="12"
            nudge-bottom="28"
            content-class="mariadb-select-v-menu mariadb-select-v-menu--full-border"
            :activator="`#${activatorIdTransform(activeCtxItem.id)}`"
        >
            <v-list>
                <v-list-item
                    v-for="option in getOptions(activeCtxItem.type)"
                    :key="option"
                    dense
                    link
                    @click="() => optionHandler({ item: activeCtxItem, option })"
                >
                    <v-list-item-title class="color text-text" v-text="option" />
                </v-list-item>
            </v-list>
        </v-menu>
    </div>
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
    name: 'db-list-tree',
    props: {
        schemaList: { type: Array, required: true },
    },
    data() {
        return {
            tableOptions: [
                this.$t('previewData'),
                this.$t('viewDetails'),
                this.$t('placeSchemaInEditor'),
            ],
            schemaOptions: [this.$t('useDb'), this.$t('placeSchemaInEditor')],
            columnOptions: [this.$t('placeColumnNameInEditor')],
            showCtxMenu: false,
            activeCtxItem: null, // active item to show in context(options) menu
            hoveredItem: null,
        }
    },
    computed: {
        filter() {
            return (item, search, textKey) => item[textKey].indexOf(search) > -1
        },
        showCtxBtn() {
            return item => this.activeCtxItem && item.id === this.activeCtxItem.id
        },
    },
    watch: {
        showCtxMenu(v) {
            if (!v) this.activeCtxItem = null
        },
    },
    methods: {
        /** This replaces dots with __ as vuetify activator slots
         * can't not parse html id contains dots.
         * @param {String} id - html id attribute
         * @returns {String} valid id that works with vuetify activator props
         */
        activatorIdTransform(id) {
            return id.replace(/\./g, '__')
        },
        async handleLoadChildren(item) {
            await this.emitPromise('load-children', item)
        },
        async emitPromise(method, ...params) {
            const listener = this.$listeners[method]
            if (listener) {
                const res = await listener(...params)
                return res === undefined || res
            }
            return false
        },
        handleOpenCtxMenu({ e, item }) {
            e.stopPropagation()
            if (this.activeCtxItem === item) {
                this.showCtxMenu = false
                this.activeCtxItem = null
            } else {
                if (!this.showCtxMenu) this.showCtxMenu = true
                this.activeCtxItem = item
            }
        },
        optionHandler({ item, option }) {
            const schema = item.id
            switch (option) {
                case this.$t('previewData'):
                    this.$emit('preview-data', schema)
                    break
                case this.$t('viewDetails'):
                    this.$emit('view-details', schema)
                    break
                case this.$t('placeSchemaInEditor'):
                    this.$emit('place-to-editor', this.$help.escapeIdentifiers(schema))
                    break
                case this.$t('placeColumnNameInEditor'):
                    this.$emit('place-to-editor', this.$help.escapeIdentifiers(item.name))
                    break
                case this.$t('useDb'):
                    this.$emit('use-db', schema)
                    break
            }
        },
        iconSheet(item) {
            switch (item.type) {
                case 'schema':
                    return '$vuetify.icons.database'
                //TODO: a separate icon for tables
                case 'tables':
                case 'table':
                    return '$vuetify.icons.table'
                //TODO: an icon for column
            }
        },
        getOptions(type) {
            return this[`${type}Options`]
        },
        onContextMenu({ e, item }) {
            this.handleOpenCtxMenu({ e, item })
        },
    },
}
</script>

<style lang="scss" scoped>
::v-deep .v-treeview-node__toggle {
    width: 16px;
    height: 16px;
}
::v-deep .v-treeview-node__level {
    width: 16px;
}
.node-label {
    height: 40px;
}
</style>
