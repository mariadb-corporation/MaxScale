<template>
    <div
        ref="queryViewCtr"
        v-resize.quiet="setDim"
        v-shortkey="QUERY_SHORTCUT_KEYS"
        class="query-editor fill-height"
        @shortkey="eventBus.$emit('shortkey', $event.srcKey)"
    >
        <div
            class="fill-height d-flex flex-column"
            :class="{ 'query-editor--fullscreen': is_fullscreen }"
        >
            <div v-if="$slots['query-editor-top']" ref="queryEditorTopSlot">
                <slot name="query-editor-top" />
            </div>
            <v-progress-linear v-if="is_validating_conn" indeterminate color="primary" />
            <template v-else>
                <wke-nav-ctr
                    v-if="!hidden_comp.includes('wke-nav-ctr')"
                    :height="wkeNavCtrHeight"
                />
                <keep-alive v-for="wke in allWorksheets" :key="wke.id" max="15">
                    <wke-ctr
                        v-if="activeWkeId === wke.id && ctrDim.height"
                        ref="wke"
                        :ctrDim="ctrDim"
                    >
                        <slot v-for="(_, slot) in $slots" :slot="slot" :name="slot" />
                    </wke-ctr>
                </keep-alive>
            </template>
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
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapState } from 'vuex'
import Worksheet from '@queryEditorSrc/store/orm/models/Worksheet'
import '@queryEditorSrc/styles/queryEditor.scss'
import WkeCtr from '@queryEditorSrc/components/WkeCtr.vue'
import WkeNavCtr from '@queryEditorSrc/components/WkeNavCtr.vue'
import { EventBus } from '@queryEditorSrc/components/EventBus'

export default {
    name: 'query-editor',
    components: {
        WkeCtr,
        WkeNavCtr,
    },
    data() {
        return {
            dim: {},
            queryEditorTopSlotHeight: 0,
        }
    },
    computed: {
        ...mapState({
            is_fullscreen: state => state.queryPersisted.is_fullscreen,
            is_validating_conn: state => state.queryConns.is_validating_conn,
            QUERY_SHORTCUT_KEYS: state => state.queryEditorConfig.config.QUERY_SHORTCUT_KEYS,
            hidden_comp: state => state.queryEditorConfig.hidden_comp,
        }),
        allWorksheets() {
            return Worksheet.all()
        },
        activeWkeId() {
            return Worksheet.getters('getActiveWkeId')
        },
        wkeNavCtrHeight() {
            return this.hidden_comp.includes('wke-nav-ctr') ? 0 : 32
        },
        ctrDim() {
            return {
                width: this.dim.width,
                height: this.dim.height - this.wkeNavCtrHeight - this.queryEditorTopSlotHeight,
            }
        },
        eventBus() {
            return EventBus
        },
    },
    watch: {
        is_fullscreen(v) {
            if (v)
                this.dim = {
                    width: document.body.clientWidth,
                    height: document.body.clientHeight,
                }
            else this.$helpers.doubleRAF(() => this.setDim())
        },
    },
    created() {
        this.handleAutoClearQueryHistory()
    },
    mounted() {
        this.$nextTick(() => this.setDim(), this.setQueryEditorTopSlotHeight())
    },

    methods: {
        ...mapActions({
            handleAutoClearQueryHistory: 'queryPersisted/handleAutoClearQueryHistory',
        }),
        setDim() {
            const { width, height } = this.$refs.queryViewCtr.getBoundingClientRect()
            this.dim = { width, height }
        },
        setQueryEditorTopSlotHeight() {
            if (this.$refs.queryEditorTopSlot) {
                const { height } = this.$refs.queryEditorTopSlot.getBoundingClientRect()
                this.queryEditorTopSlotHeight = height
            }
        },
    },
}
</script>
<style lang="scss" scoped>
.query-editor {
    &--fullscreen {
        background: white;
        z-index: 7;
        position: fixed;
        top: 0px;
        right: 0px;
        bottom: 0px;
        left: 0px;
    }
}
</style>
