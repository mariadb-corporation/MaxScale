<template>
    <div
        class="er-toolbar-ctr d-flex align-center pr-3 white mxs-color-helper border-bottom-table-border"
        :style="{ minHeight: `${height}px`, maxHeight: `${height}px` }"
    >
        <v-tooltip top transition="slide-y-transition">
            <template v-slot:activator="{ on }">
                <div class="er-toolbar__btn" v-on="on">
                    <v-select
                        :value="config.linkShape.type"
                        :items="allLinkShapes"
                        item-text="text"
                        item-value="id"
                        name="linkShapeType"
                        outlined
                        class="link-shape-select vuetify-input--override v-select--mariadb v-select--mariadb--borderless"
                        :menu-props="{
                            contentClass:
                                'v-select--menu-mariadb v-menu--mariadb-with-shadow-no-border',
                            bottom: true,
                            offsetY: true,
                        }"
                        dense
                        :height="buttonHeight"
                        hide-details
                        @change="onChangeLinkShape"
                    >
                        <template v-slot:selection="{ item }">
                            <v-icon size="28" color="primary">
                                {{ `$vuetify.icons.mxs_${$helpers.lodash.camelCase(item)}Shape` }}
                            </v-icon>
                        </template>
                        <template v-slot:item="{ item }">
                            <v-icon size="28" color="primary">
                                {{ `$vuetify.icons.mxs_${$helpers.lodash.camelCase(item)}Shape` }}
                            </v-icon>
                        </template>
                    </v-select>
                </div>
            </template>
            {{ $mxs_t('shapeOfLinks') }}
        </v-tooltip>
        <v-tooltip top transition="slide-y-transition">
            <template v-slot:activator="{ on }">
                <div class="er-toolbar__btn" v-on="on">
                    <v-select
                        v-model.number="zoomValue"
                        :items="ERD_ZOOM_OPTS"
                        name="zoomSelect"
                        outlined
                        class="zoom-select vuetify-input--override v-select--mariadb v-select--mariadb--borderless"
                        :menu-props="{
                            contentClass:
                                'v-select--menu-mariadb v-menu--mariadb-with-shadow-no-border',
                            bottom: true,
                            offsetY: true,
                            closeOnContentClick: true,
                        }"
                        dense
                        :height="buttonHeight"
                        hide-details
                        :maxlength="3"
                        :placeholder="handleShowSelection()"
                        @keypress="$helpers.preventNonNumericalVal($event)"
                    >
                        <template v-slot:prepend-item>
                            <v-list-item link @click="$emit('set-zoom', { isFitIntoView: true })">
                                {{ $mxs_t('fit') }}
                            </v-list-item>
                        </template>
                        <template v-slot:selection> {{ handleShowSelection() }} </template>
                        <template v-slot:item="{ item }"> {{ `${item}%` }} </template>
                    </v-select>
                </div>
            </template>
            {{ $mxs_t('zoom') }}
        </v-tooltip>

        <v-divider class="align-self-center er-toolbar__separator mx-2" vertical />
        <mxs-tooltip-btn
            btnClass="toolbar-square-btn"
            text
            depressed
            color="primary"
            :disabled="isUndoDisabled"
            @click="$emit('on-undo')"
        >
            <template v-slot:btn-content>
                <v-icon size="22" color="primary">mdi-undo</v-icon>
            </template>
            {{ $mxs_t('undo') }}
            <br />
            {{ OS_KEY }} + Z
        </mxs-tooltip-btn>
        <mxs-tooltip-btn
            btnClass="toolbar-square-btn"
            text
            depressed
            color="primary"
            :disabled="isRedoDisabled"
            @click="$emit('on-redo')"
        >
            <template v-slot:btn-content>
                <v-icon size="22" color="primary">mdi-redo</v-icon>
            </template>
            {{ $mxs_t('redo') }}
            <br />
            {{ OS_KEY }} + SHIFT + Z
        </mxs-tooltip-btn>
        <v-divider class="align-self-center er-toolbar__separator mx-2" vertical />
        <mxs-tooltip-btn
            btnClass="toolbar-square-btn"
            :text="!config.link.isAttrToAttr"
            depressed
            color="primary"
            @click="toggleIsAttrToAttr"
        >
            <template v-slot:btn-content>
                <v-icon size="22">mdi-key-link </v-icon>
            </template>
            {{ $mxs_t('info.drawFkLinks') }}
        </mxs-tooltip-btn>
        <mxs-tooltip-btn
            btnClass="toolbar-square-btn"
            text
            depressed
            color="primary"
            :disabled="!hasConnId"
            @click="$emit('on-create-table')"
        >
            <template v-slot:btn-content>
                <v-icon size="22" color="primary">mdi-table-plus</v-icon>
            </template>
            {{ $mxs_t('createTable') }}
        </mxs-tooltip-btn>
        <v-divider class="align-self-center er-toolbar__separator mx-2" vertical />
        <v-menu
            offset-y
            bottom
            content-class="v-menu--mariadb v-menu--mariadb-with-shadow-no-border"
        >
            <template v-slot:activator="{ on: menu, attrs }">
                <v-tooltip top transition="slide-y-transition">
                    <template v-slot:activator="{ on: tooltip }">
                        <v-btn
                            text
                            color="primary"
                            class="toolbar-square-btn"
                            v-bind="attrs"
                            v-on="{ ...tooltip, ...menu }"
                        >
                            <v-icon size="20">mdi-download</v-icon>
                        </v-btn>
                    </template>
                    {{ $mxs_t('export') }}
                </v-tooltip>
            </template>
            <v-list>
                <v-list-item v-for="opt in exportOptions" :key="opt.name" @click="opt.action">
                    <v-list-item-title class="mxs-color-helper text-text">
                        {{ opt.name }}
                    </v-list-item-title>
                </v-list-item>
            </v-list>
        </v-menu>
        <mxs-tooltip-btn
            btnClass="toolbar-square-btn"
            text
            color="primary"
            :disabled="!hasConnId"
            @click="$emit('on-apply-script')"
        >
            <template v-slot:btn-content>
                <v-icon size="20">$vuetify.icons.mxs_running</v-icon>
            </template>
            {{ $mxs_t('applyScript') }}
            <br />
            {{ OS_KEY }} + SHIFT + ENTER
        </mxs-tooltip-btn>
        <v-spacer />
        <mxs-tooltip-btn
            btnClass="er-toolbar__btn toolbar-square-btn"
            text
            :disabled="!hasConnId"
            :color="activeErdConn ? 'primary' : ''"
            @click="genErd"
        >
            <template v-slot:btn-content>
                <v-icon size="20">$vuetify.icons.mxs_reports </v-icon>
            </template>
            {{ $mxs_t('genErd') }}
        </mxs-tooltip-btn>
        <connection-btn
            btnClass="er-toolbar__btn connection-btn"
            depressed
            :height="buttonHeight"
            :activeConn="activeErdConn"
            @click="SET_CONN_DLG({ is_opened: true, type: QUERY_CONN_BINDING_TYPES.ERD })"
        />
    </div>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/**
 * Emits
 * set-zoom: { isFitIntoView: boolean }
 * on-undo: void
 * on-redo: void
 * on-create-table: void
 * on-export-script: void
 * on-export-as-jpeg: void
 * on-apply-script: void
 * input: Object. v-model
 */
import ErdTask from '@wsModels/ErdTask'
import QueryConn from '@wsModels/QueryConn'
import { LINK_SHAPES } from '@share/components/common/MxsSvgGraphs/shapeConfig'
import ConnectionBtn from '@wkeComps/ConnectionBtn.vue'
import { EventBus } from '@wkeComps/EventBus'
import { mapMutations, mapState } from 'vuex'

export default {
    name: 'er-toolbar-ctr',
    components: { ConnectionBtn },
    props: {
        value: { type: Object, required: true },
        height: { type: Number, required: true },
        zoom: { type: Number, required: true },
        isFitIntoView: { type: Boolean, required: true },
    },
    computed: {
        ...mapState({
            QUERY_CONN_BINDING_TYPES: state => state.mxsWorkspace.config.QUERY_CONN_BINDING_TYPES,
            ERD_ZOOM_OPTS: state => state.mxsWorkspace.config.ERD_ZOOM_OPTS,
            OS_KEY: state => state.mxsWorkspace.config.OS_KEY,
        }),
        config: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
        allLinkShapes() {
            return Object.values(LINK_SHAPES)
        },
        activeErdConn() {
            return QueryConn.getters('activeErdConn')
        },
        buttonHeight() {
            return 28
        },
        zoomValue: {
            get() {
                return Math.floor(this.zoom * 100)
            },
            set(v) {
                if (v) this.$emit('set-zoom', { v: v / 100 })
            },
        },
        nodesHistory() {
            return ErdTask.getters('nodesHistory')
        },
        activeHistoryIdx() {
            return ErdTask.getters('activeHistoryIdx')
        },
        hasConnId() {
            return Boolean(this.activeErdConn.id)
        },
        isUndoDisabled() {
            return this.activeHistoryIdx === 0
        },
        isRedoDisabled() {
            return this.activeHistoryIdx === this.nodesHistory.length - 1
        },
        eventBus() {
            return EventBus
        },
        exportOptions() {
            return [
                {
                    name: this.$mxs_t('exportScript'),
                    action: () => this.$emit('on-export-script'),
                },
                {
                    name: this.$mxs_t('exportAsJpeg'),
                    action: () => this.$emit('on-export-as-jpeg'),
                },
            ]
        },
    },
    activated() {
        this.eventBus.$on('workspace-shortkey', this.shortKeyHandler)
    },
    deactivated() {
        this.eventBus.$off('workspace-shortkey')
    },
    beforeDestroy() {
        this.eventBus.$off('workspace-shortkey')
    },
    methods: {
        ...mapMutations({
            SET_CONN_DLG: 'mxsWorkspace/SET_CONN_DLG',
            SET_GEN_ERD_DLG: 'mxsWorkspace/SET_GEN_ERD_DLG',
        }),
        immutableUpdateConfig(obj, path, value) {
            const updatedObj = this.$helpers.lodash.cloneDeep(obj)
            this.$helpers.lodash.update(updatedObj, path, () => value)
            return updatedObj
        },
        toggleIsAttrToAttr() {
            this.config = this.immutableUpdateConfig(
                this.config,
                'link.isAttrToAttr',
                !this.config.link.isAttrToAttr
            )
        },
        onChangeLinkShape(v) {
            this.config = this.immutableUpdateConfig(this.config, 'linkShape.type', v)
        },
        genErd() {
            this.SET_GEN_ERD_DLG({
                is_opened: true,
                preselected_schemas: [],
                connection: this.activeErdConn,
                gen_in_new_ws: false,
            })
        },
        handleShowSelection() {
            return `${this.isFitIntoView ? this.$mxs_t('fit') : `${this.zoomValue}%`}`
        },
        shortKeyHandler(key) {
            switch (key) {
                case 'ctrl-z':
                case 'mac-cmd-z':
                    if (!this.isUndoDisabled) this.$emit('on-undo')
                    break
                case 'ctrl-shift-z':
                case 'mac-cmd-shift-z':
                    if (!this.isRedoDisabled) this.$emit('on-redo')
                    break
                case 'ctrl-shift-enter':
                case 'mac-cmd-shift-enter':
                    this.$emit('on-apply-script')
                    break
            }
        },
    },
}
</script>
<style lang="scss" scoped>
.er-toolbar-ctr {
    z-index: 4;
    .er-toolbar__separator {
        min-height: 28px;
        max-height: 28px;
    }
}
</style>
<style lang="scss">
.er-toolbar-ctr {
    .zoom-select,
    .link-shape-select {
        max-width: 64px;
        .v-input__slot {
            padding-left: 8px !important;
            padding-right: 0px !important;
        }
    }
    .zoom-select {
        max-width: 76px;
        input::placeholder {
            color: black;
            opacity: 1;
        }
    }
    .connection-btn {
        border-radius: 0px !important;
        &:hover {
            border-radius: 0px !important;
        }
    }
    .er-toolbar__btn {
        padding: 0px 2px;
    }
}
</style>
