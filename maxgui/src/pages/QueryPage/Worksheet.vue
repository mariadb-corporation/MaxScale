<template>
    <split-pane
        v-model="sidebarPct"
        class="query-page__content"
        :minPercent="minSidebarPct"
        split="vert"
        :disable="is_sidebar_collapsed"
    >
        <template slot="pane-left">
            <sidebar-container
                @place-to-editor="$typy($refs.txtEditorPane, 'placeToEditor').safeFunction($event)"
                @on-dragging="$typy($refs.txtEditorPane, 'draggingTxt').safeFunction($event)"
                @on-dragend="
                    $typy($refs.txtEditorPane, 'dropTxtToEditor').safeFunction({
                        e: $event,
                        type: 'schema',
                    })
                "
            />
        </template>
        <template slot="pane-right">
            <ddl-editor v-if="isDDLEditor" />
            <txt-editor-container
                v-show="isTxtEditor"
                ref="txtEditorPane"
                :dim="txtEditorPaneDim"
                v-on="$listeners"
            />
        </template>
    </split-pane>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import SidebarContainer from './SidebarContainer'
import { mapGetters, mapState } from 'vuex'
import DDLEditor from './DDLEditor.vue'
import TxtEditorContainer from './TxtEditorContainer.vue'
export default {
    name: 'worksheet',
    components: {
        SidebarContainer,
        'txt-editor-container': TxtEditorContainer,
        'ddl-editor': DDLEditor,
    },
    props: {
        ctrDim: { type: Object, required: true },
    },
    data() {
        return {
            // split-pane states
            minSidebarPct: 0,
            sidebarPct: 0,
            txtEditorPaneDim: { width: 0, height: 0 },
        }
    },
    computed: {
        ...mapState({
            SQL_EDITOR_MODES: state => state.app_config.SQL_EDITOR_MODES,
            show_vis_sidebar: state => state.query.show_vis_sidebar,
            query_txt: state => state.query.query_txt,
            is_sidebar_collapsed: state => state.query.is_sidebar_collapsed,
            curr_editor_mode: state => state.query.curr_editor_mode,
        }),
        ...mapGetters({
            getDbCmplList: 'query/getDbCmplList',
        }),
        isTxtEditor() {
            return this.curr_editor_mode === this.SQL_EDITOR_MODES.TXT_EDITOR
        },
        isDDLEditor() {
            return this.curr_editor_mode === this.SQL_EDITOR_MODES.DDL_EDITOR
        },
    },
    watch: {
        sidebarPct() {
            this.$nextTick(() => {
                this.setTxtEditorPaneDim()
            })
        },
        is_sidebar_collapsed() {
            this.handleSetSidebarPct()
        },
        ctrDim: {
            deep: true,
            handler(v, oV) {
                if (oV) {
                    this.$nextTick(() => {
                        this.setTxtEditorPaneDim()
                    })
                }
            },
        },
        isTxtEditor(v) {
            if (v)
                this.$nextTick(() => {
                    this.setTxtEditorPaneDim()
                })
        },
    },
    activated() {
        this.$help.doubleRAF(() => {
            this.handleSetSidebarPct()
            this.setTxtEditorPaneDim()
        })
    },
    methods: {
        // panes dimension/percentages calculation functions
        handleSetSidebarPct() {
            const containerWidth = this.ctrDim.width
            if (this.is_sidebar_collapsed) {
                this.minSidebarPct = this.$help.pxToPct({ px: 40, containerPx: containerWidth })
                this.sidebarPct = this.minSidebarPct
            } else {
                this.minSidebarPct = this.$help.pxToPct({ px: 200, containerPx: containerWidth })
                this.sidebarPct = this.$help.pxToPct({ px: 240, containerPx: containerWidth })
            }
        },
        setTxtEditorPaneDim() {
            if (this.$refs.txtEditorPane.$el) {
                const { clientHeight: height, clientWidth: width } = this.$refs.txtEditorPane.$el
                this.txtEditorPaneDim = { width, height }
            }
        },
    },
}
</script>
