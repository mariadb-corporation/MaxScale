<template>
    <div
        class="wke-toolbar d-flex align-center flex-grow-1 mxs-color-helper border-bottom-table-border px-2"
    >
        <div ref="toolbarLeft" class="d-flex align-center left-buttons fill-height">
            <v-btn small class="float-left add-wke-btn" icon @click="add">
                <v-icon size="18" color="blue-azure">mdi-plus</v-icon>
            </v-btn>
        </div>
        <v-spacer />
        <div ref="toobarRight" class="d-flex align-center right-buttons fill-height">
            <mxs-tooltip-btn
                btnClass="query-setting-btn"
                icon
                small
                color="primary"
                @click="isPrefDlgOpened = !isPrefDlgOpened"
            >
                <template v-slot:btn-content>
                    <v-icon size="16">$vuetify.icons.mxs_settings</v-icon>
                    <pref-dlg v-model="isPrefDlgOpened" />
                </template>
                {{ $mxs_t('pref') }}
            </mxs-tooltip-btn>
            <mxs-tooltip-btn
                btnClass="min-max-btn"
                icon
                small
                color="primary"
                @click="SET_IS_FULLSCREEN(!is_fullscreen)"
            >
                <template v-slot:btn-content>
                    <v-icon size="22"> mdi-fullscreen{{ is_fullscreen ? '-exit' : '' }} </v-icon>
                </template>
                {{ is_fullscreen ? $mxs_t('minimize') : $mxs_t('maximize') }}
            </mxs-tooltip-btn>
        </div>
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
 * Emits
 * $emit('get-total-btn-width', v:number)
 */
import Worksheet from '@wsModels/Worksheet'
import { mapMutations, mapState } from 'vuex'
import PrefDlg from '@wsComps/PrefDlg'

export default {
    name: 'wke-toolbar',
    components: { PrefDlg },
    data() {
        return {
            isPrefDlgOpened: false,
            leftBtnsWidth: 0,
            rightBtnsWidth: 0,
        }
    },
    computed: {
        ...mapState({ is_fullscreen: state => state.prefAndStorage.is_fullscreen }),
        totalWidth() {
            return this.rightBtnsWidth + this.leftBtnsWidth
        },
    },
    watch: {
        totalWidth: {
            immediate: true,
            handler(v) {
                this.$emit('get-total-btn-width', v)
            },
        },
    },
    mounted() {
        this.$nextTick(() => {
            this.leftBtnsWidth = this.$refs.toolbarLeft.clientWidth
            this.rightBtnsWidth = this.$refs.toobarRight.clientWidth
        })
    },
    methods: {
        ...mapMutations({ SET_IS_FULLSCREEN: 'prefAndStorage/SET_IS_FULLSCREEN' }),
        add() {
            Worksheet.dispatch('insertBlankWke')
        },
    },
}
</script>
