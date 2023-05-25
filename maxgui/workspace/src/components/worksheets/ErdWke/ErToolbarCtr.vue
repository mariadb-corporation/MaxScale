<template>
    <div class="er-toolbar-ctr d-flex pr-3 white mxs-color-helper border-bottom-table-border">
        <mxs-tooltip-btn
            btnClass="toolbar-square-btn"
            :color="config.link.isAttrToAttr ? 'primary' : '#e8eef1'"
            depressed
            @click="toggleIsAttrToAttr"
        >
            <template v-slot:btn-content>
                <v-icon size="22" :color="config.link.isAttrToAttr ? 'white' : 'blue-azure'">
                    mdi-key-link
                </v-icon>
            </template>
            {{ $mxs_t('info.drawFkLinks') }}
        </mxs-tooltip-btn>
        <v-tooltip top transition="slide-y-transition">
            <template v-slot:activator="{ on }">
                <div v-on="on">
                    <v-select
                        :value="config.linkShape.type"
                        :items="allLinkShapes"
                        item-text="text"
                        item-value="id"
                        name="linkShapeType"
                        outlined
                        class="link-shape-select vuetify-input--override v-select--mariadb error--text__bottom"
                        :menu-props="{
                            contentClass: 'v-select--menu-mariadb',
                            bottom: true,
                            offsetY: true,
                        }"
                        dense
                        :height="height"
                        hide-details
                        @change="onChangeLinkShape"
                    >
                        <template v-slot:selection="{ item }">
                            <v-icon size="28" color="blue-azure">
                                {{ `$vuetify.icons.mxs_${$helpers.lodash.camelCase(item)}Shape` }}
                            </v-icon>
                        </template>
                        <template v-slot:item="{ item }">
                            <v-icon size="28" color="blue-azure">
                                {{ `$vuetify.icons.mxs_${$helpers.lodash.camelCase(item)}Shape` }}
                            </v-icon>
                        </template>
                    </v-select>
                </div>
            </template>
            {{ $mxs_t('shapeOfLinks') }}
        </v-tooltip>
        <v-spacer />
        <mxs-tooltip-btn
            btnClass="toolbar-square-btn"
            icon
            small
            :disabled="!Boolean(activeErdConn.id)"
            :color="activeErdConn ? 'primary' : ''"
            @click="genErd"
        >
            <template v-slot:btn-content>
                <v-icon size="20">
                    $vuetify.icons.mxs_reports
                </v-icon>
            </template>
            {{ $mxs_t('genErd') }}
        </mxs-tooltip-btn>
        <connection-btn
            btnClass="connection-btn"
            depressed
            :height="height"
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
import { LINK_SHAPES } from '@share/components/common/MxsSvgGraphs/config'
import ConnectionBtn from '@wkeComps/ConnectionBtn.vue'
import { mapMutations, mapState } from 'vuex'

export default {
    name: 'er-toolbar-ctr',
    components: { ConnectionBtn },
    props: {
        value: { type: Object, required: true },
        height: { type: Number, required: true },
    },
    computed: {
        ...mapState({
            QUERY_CONN_BINDING_TYPES: state => state.mxsWorkspace.config.QUERY_CONN_BINDING_TYPES,
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
    },
}
</script>
<style lang="scss" scoped>
.er-toolbar-ctr {
    width: 100%;
    top: 0;
    z-index: 4;
}
</style>
<style lang="scss">
.er-toolbar-ctr {
    .link-shape-select {
        max-width: 64px;
        fieldset {
            border-radius: 0;
        }
        .v-input__slot {
            padding-left: 8px !important;
            padding-right: 0px !important;
        }
    }
    .connection-btn {
        border-radius: 0px !important;
        &:hover {
            border-radius: 0px !important;
        }
    }
}
</style>
