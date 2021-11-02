/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/**
 *  To use this mixin, a mousedown event needs to be listened on drag target element and do the followings assign
 *  assign true to `isDragging` and event.target to `dragTarget`
 *  @mousedown
 */
export default {
    data() {
        return {
            isDragging: false,
            dragTarget: null,
            dragTargetId: 'target-drag',
        }
    },
    watch: {
        isDragging(v) {
            if (v) this.addDragEvts()
            else this.removeDragEvts()
        },
    },
    beforeDestroy() {
        this.removeDragEvts()
    },
    methods: {
        addDragEvts() {
            document.addEventListener('mousemove', this.onDragging)
            document.addEventListener('mouseup', this.onDragEnd)
        },
        removeDragEvts() {
            document.removeEventListener('mousemove', this.onDragging)
            document.removeEventListener('mouseup', this.onDragEnd)
        },
        onDragging(e) {
            e.preventDefault()
            if (this.isDragging) {
                this.$help.removeTargetDragEle(this.dragTargetId)
                this.$help.addDragTargetEle({
                    e,
                    dragTarget: this.dragTarget,
                    dragTargetId: this.dragTargetId,
                })
                this.$emit('on-dragging', e)
            }
        },
        onDragEnd(e) {
            e.preventDefault()
            if (this.isDragging) {
                this.$help.removeTargetDragEle(this.dragTargetId)
                this.$emit('on-dragend', e)
                this.isDragging = false
            }
        },
    },
}
