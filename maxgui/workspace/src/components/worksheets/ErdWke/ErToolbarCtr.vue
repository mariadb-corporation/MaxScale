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
            @click="$emit('on-create-table')"
        >
            <template v-slot:btn-content>
                <v-icon size="22" color="primary">mdi-table-plus</v-icon>
            </template>
            {{ $mxs_t('createTable') }}
        </mxs-tooltip-btn>

        <v-spacer />
        <mxs-tooltip-btn
            btnClass="er-toolbar__btn toolbar-square-btn"
            text
            :disabled="!Boolean(activeErdConn.id)"
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
import QueryConn from '@wsModels/QueryConn'
import { LINK_SHAPES } from '@wsSrc/components/worksheets/ErdWke/config'
import ConnectionBtn from '@wkeComps/ConnectionBtn.vue'
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
            return QueryConn.getters('getActiveErdConn')
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
