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
                <keep-alive v-for="wke in worksheets_arr" :key="wke.id" max="15">
                    <wke-ctr
                        v-if="active_wke_id === wke.id && ctrDim.height"
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
 * Change Date: 2025-09-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapState, mapGetters } from 'vuex'
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
            active_wke_id: state => state.wke.active_wke_id,
            is_fullscreen: state => state.wke.is_fullscreen,
            is_validating_conn: state => state.queryConn.is_validating_conn,
            QUERY_SHORTCUT_KEYS: state => state.queryEditorConfig.config.QUERY_SHORTCUT_KEYS,
            worksheets_arr: state => state.wke.worksheets_arr,
            hidden_comp: state => state.queryEditorConfig.hidden_comp,
        }),
        ...mapGetters({
            getActiveWke: 'wke/getActiveWke',
            getIsTxtEditor: 'editor/getIsTxtEditor',
        }),
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
        active_wke_id: {
            immediate: true,
            handler(v) {
                if (v) this.handleSyncWke(this.getActiveWke)
            },
        },
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
            handleSyncWke: 'wke/handleSyncWke',
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
