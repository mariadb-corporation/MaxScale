<template>
    <div class="d-flex justify-start er-toolbar-ctr pt-1 px-3">
        <!-- TODO: Add button to generate ERD from existing db  -->

        <mxs-tooltip-btn
            btnClass="mr-2 toolbar-square-btn"
            :color="config.link.isAttrToAttr ? 'primary' : '#e8eef1'"
            elevation="0"
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
                        class="link-shape-select mr-2 vuetify-input--override v-select--mariadb error--text__bottom"
                        :menu-props="{
                            contentClass: 'v-select--menu-mariadb',
                            bottom: true,
                            offsetY: true,
                        }"
                        dense
                        :height="28"
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
import { LINK_SHAPES } from '@share/components/common/MxsSvgGraphs/config'

export default {
    name: 'er-toolbar-ctr',
    props: {
        value: { type: Object, required: true },
    },
    computed: {
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
    },
    methods: {
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
    },
}
</script>
<style lang="scss" scoped>
.er-toolbar-ctr {
    width: 100%;
    height: 32px;
    top: 0;
    z-index: 4;
}
</style>
<style lang="scss">
.link-shape-select {
    max-width: 64px;
    .v-input__slot {
        padding-left: 8px !important;
        padding-right: 0px !important;
    }
}
</style>
